// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// Beebium is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. Beebium is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with Beebium.
// If not, see <https://www.gnu.org/licenses/>.

#if defined(BEEBIUM_HAS_BONJOUR) || defined(BEEBIUM_HAS_BONJOUR_DYNAMIC)

#include <beebium/discovery/Advertiser.hpp>

#include "DnssdApi.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#endif

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace beebium::discovery {

/// macOS Bonjour implementation of Advertiser using dns_sd.h.
///
/// Uses DNSServiceRegister() to advertise the service via mDNS.
/// The event loop runs in a background thread to process DNS-SD callbacks.
class BonjourAdvertiser final : public Advertiser {
public:
    BonjourAdvertiser() = default;

    ~BonjourAdvertiser() override { stop(); }

    bool start(const ServiceInfo& info) override {
        // Stop any existing advertisement
        stop();

        const DnssdApi* dnssd = dnssd_api();
        if (!dnssd) {
            return false;  // DNS-SD runtime not available
        }

        // Store info for state reporting
        {
            std::lock_guard lock(mutex_);
            info_ = info;
        }

#ifdef _WIN32
        // event_loop() calls select() on the DNS-SD socket, which needs Winsock
        // initialised in this process. Refcounted, so this is safe even when the
        // host app already called WSAStartup.
        if (!wsa_inited_) {
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
                wsa_inited_ = true;
            }
        }
#endif

        // Build TXT record
        TXTRecordRef txt_ref;
        dnssd->TXTRecordCreate(&txt_ref, 0, nullptr);

        for (const auto& [key, value] : info.txt_records) {
            dnssd->TXTRecordSetValue(&txt_ref, key.c_str(),
                                     static_cast<uint8_t>(value.size()),
                                     value.c_str());
        }

        // Register service
        DNSServiceErrorType err = dnssd->DNSServiceRegister(
            &service_ref_,
            0,  // flags
            kDNSServiceInterfaceIndexAny,
            info.instance_name.c_str(),
            info.service_type.c_str(),
            nullptr,  // domain (default = .local)
            nullptr,  // host (default = this machine)
            htons(info.port),
            dnssd->TXTRecordGetLength(&txt_ref),
            dnssd->TXTRecordGetBytesPtr(&txt_ref),
            register_callback,
            this);

        dnssd->TXTRecordDeallocate(&txt_ref);

        if (err != kDNSServiceErr_NoError) {
            service_ref_ = nullptr;
            return false;
        }

        // Start event loop thread
        running_ = true;
        event_thread_ = std::thread([this] { event_loop(); });

        return true;
    }

    void stop() override {
        // Signal the event loop to stop, then JOIN the event thread before
        // touching service_ref_. The event thread may be inside
        // DNSServiceProcessResult on service_ref_; deallocating a DNSServiceRef
        // while another thread is inside a DNS-SD call on it is forbidden by
        // dnssd and aborts the process on recent macOS ("API MISUSE:
        // Resurrection of an object" in libdispatch). The event loop blocks in
        // select() with a short timeout and re-checks running_, so the join
        // completes promptly once running_ is cleared.
        running_ = false;

        if (event_thread_.joinable()) {
            event_thread_.join();
        }

        // The event thread is gone: now nothing else races service_ref_, so it
        // is safe to deallocate it here.
        if (service_ref_) {
            dnssd_api()->DNSServiceRefDeallocate(service_ref_);
            service_ref_ = nullptr;
        }

        // Reset state
        registered_ = false;
        {
            std::lock_guard lock(mutex_);
            actual_name_.clear();
        }

#ifdef _WIN32
        if (wsa_inited_) {
            WSACleanup();
            wsa_inited_ = false;
        }
#endif
    }

    AdvertiserState state() const override {
        std::lock_guard lock(mutex_);
        return AdvertiserState{
            .available = true,  // Bonjour is always available on macOS
            .advertising = registered_.load(),
            .actual_name = actual_name_,
        };
    }

private:
    DNSServiceRef service_ref_ = nullptr;
    std::atomic<bool> registered_{false};
    std::atomic<bool> running_{false};
    std::thread event_thread_;
#ifdef _WIN32
    bool wsa_inited_ = false;
#endif

    mutable std::mutex mutex_;
    ServiceInfo info_;
    std::string actual_name_;

    void event_loop() {
        // Drive the service ref via select() on its DNS-SD socket with a short
        // timeout, calling DNSServiceProcessResult only when the socket is
        // readable. This lets stop() shut the loop down by clearing running_
        // and joining this thread BEFORE deallocating service_ref_: we never
        // deallocate the ref while this thread might be inside a DNS-SD call on
        // it (which aborts on recent macOS). service_ref_ is set once before
        // this thread starts and only cleared after it is joined, so reading it
        // here without a lock is safe.
        const DnssdApi* dnssd = dnssd_api();
        while (running_) {
            int fd = static_cast<int>(dnssd->DNSServiceRefSockFD(service_ref_));
            if (fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(fd, &readfds);

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            int n = ::select(fd + 1, &readfds, nullptr, nullptr, &tv);
            if (!running_) break;
            if (n <= 0) continue;

            if (FD_ISSET(fd, &readfds)) {
                DNSServiceErrorType err = dnssd->DNSServiceProcessResult(service_ref_);
                if (err != kDNSServiceErr_NoError) {
                    break;
                }
            }
        }
    }

    static void register_callback(DNSServiceRef /*sdRef*/,
                                  DNSServiceFlags /*flags*/,
                                  DNSServiceErrorType errorCode,
                                  const char* name, const char* /*regtype*/,
                                  const char* /*domain*/, void* context) {
        auto* self = static_cast<BonjourAdvertiser*>(context);

        if (errorCode == kDNSServiceErr_NoError) {
            self->registered_ = true;
            // The name may differ from what we requested if there was a
            // collision (DNS-SD appends " (2)", " (3)", etc.)
            std::lock_guard lock(self->mutex_);
            self->actual_name_ = name;
        } else {
            self->registered_ = false;
        }
    }
};

std::unique_ptr<Advertiser> make_bonjour_advertiser() {
    return std::make_unique<BonjourAdvertiser>();
}

#ifdef BEEBIUM_HAS_BONJOUR
// On macOS the Bonjour advertiser is the only provider, so it is the factory.
// On Windows the factory (WindowsDiscovery.cpp) chooses between Bonjour and the
// native DnsService advertiser at run time and calls make_bonjour_advertiser()
// directly.
std::unique_ptr<Advertiser> create_advertiser() {
    return make_bonjour_advertiser();
}
#endif

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_BONJOUR || BEEBIUM_HAS_BONJOUR_DYNAMIC
