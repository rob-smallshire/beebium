// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#if defined(BEEBIUM_HAS_BONJOUR_BROWSE) || defined(BEEBIUM_HAS_BONJOUR_BROWSE_DYNAMIC)

#include <beebium/discovery/Browser.hpp>

#include "DnssdApi.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#endif

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace beebium::discovery {

/// macOS Bonjour implementation of Browser using dns_sd.h.
///
/// The DNS-SD discovery flow is three-stage: DNSServiceBrowse finds
/// service instances, DNSServiceResolve resolves an instance to a
/// hostname + port + TXT record, and DNSServiceGetAddrInfo turns the
/// hostname into IPv4/IPv6 addresses. We chain these per instance
/// and only fire on_added once we have a complete DiscoveredService.
///
/// Threading: a single background thread drives DNSServiceProcessResult
/// for the browse ref. The resolve / getaddrinfo refs are processed
/// inline within the browse callback chain via shared_ptr-managed
/// helpers; this keeps the threading footprint to one thread (the
/// callbacks fire on it). All consumer state is taken under
/// mutex_; user callbacks are invoked outside the lock so consumer
/// code can call back into the browser without deadlocking.
class BonjourBrowser final : public Browser {
public:
    BonjourBrowser() = default;

    ~BonjourBrowser() override { stop(); }

    bool start(const std::string& service_type,
               BrowserCallbacks callbacks) override {
        stop();

        {
            std::lock_guard lock(mutex_);
            callbacks_ = std::move(callbacks);
            instances_.clear();
        }

#ifdef _WIN32
        if (!wsa_inited_) {
            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0) {
                wsa_inited_ = true;
            }
        }
#endif

        DNSServiceErrorType err = dnssd_api()->DNSServiceBrowse(
            &browse_ref_,
            0,                                // flags
            kDNSServiceInterfaceIndexAny,
            service_type.c_str(),
            nullptr,                          // domain (default = .local)
            browse_callback,
            this);

        if (err != kDNSServiceErr_NoError) {
            browse_ref_ = nullptr;
            return false;
        }

        running_ = true;
        browsing_ = true;
        event_thread_ = std::thread([this] { event_loop(); });
        return true;
    }

    void stop() override {
        // JOIN the event thread before deallocating browse_ref_. The event
        // loop services browse_ref_ with DNSServiceProcessResult; deallocating
        // it from here while that thread is inside the call is forbidden by
        // dnssd and aborts on recent macOS. The loop selects with a short
        // timeout and re-checks running_, so the join is prompt.
        running_ = false;

        if (event_thread_.joinable()) {
            event_thread_.join();
        }

        if (browse_ref_) {
            dnssd_api()->DNSServiceRefDeallocate(browse_ref_);
            browse_ref_ = nullptr;
        }

        // Tear down any pending resolve/address refs.
        std::lock_guard lock(mutex_);
        for (auto& [name, inst] : instances_) {
            if (inst.resolve_ref) {
                dnssd_api()->DNSServiceRefDeallocate(inst.resolve_ref);
                inst.resolve_ref = nullptr;
            }
            if (inst.addr_ref) {
                dnssd_api()->DNSServiceRefDeallocate(inst.addr_ref);
                inst.addr_ref = nullptr;
            }
        }
        instances_.clear();
        browsing_ = false;

#ifdef _WIN32
        if (wsa_inited_) {
            WSACleanup();
            wsa_inited_ = false;
        }
#endif
    }

    BrowserState state() const override {
        return BrowserState{
            .available = true,
            .browsing = browsing_.load(),
        };
    }

private:
    // Per-instance state we accumulate as the resolve / addrinfo
    // callbacks fire. We only fire on_added when we've seen at least
    // one address resolution.
    struct Instance {
        DNSServiceRef resolve_ref = nullptr;
        DNSServiceRef addr_ref = nullptr;
        DiscoveredService service;
        bool announced = false;
    };

    DNSServiceRef browse_ref_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> browsing_{false};
    std::thread event_thread_;
#ifdef _WIN32
    // Our event_loop() calls select() on the DNS-SD sockets directly, which
    // requires Winsock to be initialised in this process. Refcounted, so this
    // is safe even when the host app already called WSAStartup.
    bool wsa_inited_ = false;
#endif

    mutable std::mutex mutex_;
    BrowserCallbacks callbacks_;
    std::map<std::string, Instance> instances_;

    void event_loop() {
        // Drive both the browse ref and any resolve/addr refs that
        // were attached to it. We use select() across the union of
        // file descriptors so all in-flight DNS-SD operations make
        // progress on this single thread.
        while (running_) {
            fd_set readfds;
            FD_ZERO(&readfds);
            int max_fd = -1;

            if (browse_ref_) {
                int fd = static_cast<int>(dnssd_api()->DNSServiceRefSockFD(browse_ref_));
                if (fd >= 0) {
                    FD_SET(fd, &readfds);
                    if (fd > max_fd) max_fd = fd;
                }
            }

            // Snapshot resolve/addr fds under lock so we don't race
            // with stop()'s teardown.
            std::vector<std::pair<DNSServiceRef, int>> aux_refs;
            {
                std::lock_guard lock(mutex_);
                for (auto& [name, inst] : instances_) {
                    if (inst.resolve_ref) {
                        int fd = static_cast<int>(dnssd_api()->DNSServiceRefSockFD(inst.resolve_ref));
                        if (fd >= 0) {
                            FD_SET(fd, &readfds);
                            if (fd > max_fd) max_fd = fd;
                            aux_refs.emplace_back(inst.resolve_ref, fd);
                        }
                    }
                    if (inst.addr_ref) {
                        int fd = static_cast<int>(dnssd_api()->DNSServiceRefSockFD(inst.addr_ref));
                        if (fd >= 0) {
                            FD_SET(fd, &readfds);
                            if (fd > max_fd) max_fd = fd;
                            aux_refs.emplace_back(inst.addr_ref, fd);
                        }
                    }
                }
            }

            if (max_fd < 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            timeval tv{};
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            int n = ::select(max_fd + 1, &readfds, nullptr, nullptr, &tv);
            if (!running_) break;
            if (n <= 0) continue;

            if (browse_ref_ &&
                FD_ISSET(dnssd_api()->DNSServiceRefSockFD(browse_ref_), &readfds)) {
                dnssd_api()->DNSServiceProcessResult(browse_ref_);
                if (!running_) break;
            }

            // Processing the browse ref above may have started new
            // resolves or, on a Remove, deallocated resolve/addr refs
            // that are still in our pre-select snapshot. Re-collect the
            // set of refs that are still live and only service those, so
            // we never call DNSServiceProcessResult on freed memory.
            std::set<DNSServiceRef> live;
            {
                std::lock_guard lock(mutex_);
                for (auto& [name, inst] : instances_) {
                    if (inst.resolve_ref) live.insert(inst.resolve_ref);
                    if (inst.addr_ref) live.insert(inst.addr_ref);
                }
            }
            for (auto& [ref, fd] : aux_refs) {
                if (FD_ISSET(fd, &readfds) && live.count(ref)) {
                    dnssd_api()->DNSServiceProcessResult(ref);
                    if (!running_) break;
                }
            }

            // Reap address queries that have done their job. Once an
            // instance is announced we have its IPv4 endpoint and don't
            // need the getaddrinfo query open any longer; leaving them
            // open accumulates active DNS-SD operations (one per peer
            // ever seen) that bloat the select set and contend with new
            // resolutions. Safe here: we've finished servicing aux_refs
            // for this iteration, so we won't process a ref after freeing.
            {
                std::lock_guard lock(mutex_);
                for (auto& [name, inst] : instances_) {
                    if (inst.announced && inst.addr_ref) {
                        dnssd_api()->DNSServiceRefDeallocate(inst.addr_ref);
                        inst.addr_ref = nullptr;
                    }
                }
            }
        }
    }

    static void browse_callback(DNSServiceRef /*sdRef*/,
                                DNSServiceFlags flags,
                                uint32_t interfaceIndex,
                                DNSServiceErrorType errorCode,
                                const char* serviceName,
                                const char* regtype,
                                const char* replyDomain,
                                void* context) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError || !serviceName) return;

        if (flags & kDNSServiceFlagsAdd) {
            self->begin_resolve(serviceName, regtype, replyDomain,
                                interfaceIndex);
        } else {
            self->handle_remove(serviceName);
        }
    }

    void begin_resolve(const std::string& name, const char* regtype,
                       const char* domain, uint32_t interfaceIndex) {
        // A browse delivers the same service once per network interface,
        // so we receive several Add callbacks for a single instance.
        // Resolve each instance only once: starting a second resolve
        // would deallocate the first, still-in-flight ref -- which the
        // event loop may be holding in its current select() snapshot --
        // and then process freed memory. Skip the duplicate if a resolve
        // is already in flight, an address query is in flight, or the
        // instance has already been announced.
        {
            std::lock_guard lock(mutex_);
            auto it = instances_.find(name);
            if (it != instances_.end() &&
                (it->second.resolve_ref || it->second.addr_ref ||
                 it->second.announced)) {
                return;
            }
        }

        DNSServiceRef ref = nullptr;
        DNSServiceErrorType err = dnssd_api()->DNSServiceResolve(
            &ref,
            0,
            interfaceIndex,
            name.c_str(),
            regtype,
            domain,
            resolve_callback,
            this);
        if (err != kDNSServiceErr_NoError) return;

        std::lock_guard lock(mutex_);
        Instance& inst = instances_[name];
        if (inst.resolve_ref || inst.addr_ref || inst.announced) {
            // Raced with another Add for the same instance between the
            // check above and now; drop this duplicate rather than
            // leaking or replacing the in-flight ref.
            dnssd_api()->DNSServiceRefDeallocate(ref);
            return;
        }
        inst.resolve_ref = ref;
        inst.service.instance_name = name;
    }

    void handle_remove(const std::string& name) {
        BrowserCallbacks cbs;
        bool announced = false;
        {
            std::lock_guard lock(mutex_);
            auto it = instances_.find(name);
            if (it == instances_.end()) return;
            announced = it->second.announced;
            if (it->second.resolve_ref) {
                dnssd_api()->DNSServiceRefDeallocate(it->second.resolve_ref);
            }
            if (it->second.addr_ref) {
                dnssd_api()->DNSServiceRefDeallocate(it->second.addr_ref);
            }
            instances_.erase(it);
            cbs = callbacks_;
        }
        if (announced && cbs.on_removed) {
            cbs.on_removed(name);
        }
    }

    static void resolve_callback(DNSServiceRef sdRef,
                                 DNSServiceFlags /*flags*/,
                                 uint32_t /*interfaceIndex*/,
                                 DNSServiceErrorType errorCode,
                                 const char* fullname,
                                 const char* hosttarget,
                                 uint16_t port,            // network byte order
                                 uint16_t txtLen,
                                 const unsigned char* txtRecord,
                                 void* context) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError || !fullname || !hosttarget) {
            return;
        }

        // Extract the instance name from the fullname; DNS-SD
        // fullnames look like "Beebium\0320\0560\046254._aun._udp.local.";
        // the instance-name portion ends at the first ".<service-type>"
        // segment. We split on the first occurrence of "._" since both
        // "_aun._udp" and "_beebium._tcp" begin that way.
        std::string full(fullname);
        std::string name;
        auto pos = full.find("._");
        if (pos != std::string::npos) {
            name = full.substr(0, pos);
            // dns_sd.h emits backslash-escaped octal for special chars
            // (e.g. spaces as "\\032"); collapse those so consumers see
            // the original instance name.
            name = unescape_dns_sd(name);
        } else {
            name = full;
        }

        std::map<std::string, std::string> txt;
        uint16_t count = dnssd_api()->TXTRecordGetCount(txtLen, txtRecord);
        for (uint16_t i = 0; i < count; ++i) {
            char key[256];
            uint8_t value_len = 0;
            const void* value_ptr = nullptr;
            DNSServiceErrorType e = dnssd_api()->TXTRecordGetItemAtIndex(
                txtLen, txtRecord, i, sizeof(key), key, &value_len, &value_ptr);
            if (e != kDNSServiceErr_NoError) continue;
            std::string value;
            if (value_ptr && value_len) {
                value.assign(static_cast<const char*>(value_ptr), value_len);
            }
            txt.emplace(std::string(key), std::move(value));
        }

        // Update the per-instance state, then kick off an addrinfo
        // resolution. Replacing the resolve ref tears down this one
        // (we're done with it once the callback returned).
        //
        // Resolve the address on ANY interface rather than the single
        // interface this resolve arrived on. A browse delivers each
        // service once per interface, and begin_resolve deliberately
        // resolves an instance only once (to avoid churning the
        // in-flight ref); but the one interface we happen to pick first
        // may be one on which the host's address record never answers
        // (e.g. a utun/VPN interface), and then the getaddrinfo query
        // would hang forever and the peer would never be reported. Using
        // kDNSServiceInterfaceIndexAny lets the daemon answer from
        // whichever interface actually has the record, which is what we
        // want -- we only need one reachable address for the UDP peer.
        DNSServiceRef addr_ref = nullptr;
        DNSServiceErrorType ae = dnssd_api()->DNSServiceGetAddrInfo(
            &addr_ref,
            0,
            kDNSServiceInterfaceIndexAny,
            kDNSServiceProtocol_IPv4,
            hosttarget,
            addrinfo_callback,
            self);
        if (ae != kDNSServiceErr_NoError) {
            addr_ref = nullptr;
        }

        std::lock_guard lock(self->mutex_);
        auto it = self->instances_.find(name);
        if (it == self->instances_.end()) return;
        it->second.service.instance_name = name;
        it->second.service.hostname = hosttarget;
        it->second.service.port = ntohs(port);
        it->second.service.txt_records = std::move(txt);
        if (it->second.resolve_ref == sdRef) {
            dnssd_api()->DNSServiceRefDeallocate(it->second.resolve_ref);
            it->second.resolve_ref = nullptr;
        }
        if (it->second.addr_ref) {
            dnssd_api()->DNSServiceRefDeallocate(it->second.addr_ref);
        }
        it->second.addr_ref = addr_ref;
    }

    static void addrinfo_callback(DNSServiceRef sdRef,
                                  DNSServiceFlags /*flags*/,
                                  uint32_t /*interfaceIndex*/,
                                  DNSServiceErrorType errorCode,
                                  const char* /*hostname*/,
                                  const sockaddr* address,
                                  uint32_t /*ttl*/,
                                  void* context) {
        auto* self = static_cast<BonjourBrowser*>(context);
        if (errorCode != kDNSServiceErr_NoError || !address) return;
        if (address->sa_family != AF_INET) return;
        const auto* sin = reinterpret_cast<const sockaddr_in*>(address);
        uint32_t ipv4 = sin->sin_addr.s_addr;

        DiscoveredService announce_payload;
        BrowserCallbacks cbs;
        {
            std::lock_guard lock(self->mutex_);
            // Match by sdRef: each resolve fires its own getaddrinfo
            // ref, so the ref uniquely identifies the instance --
            // matching by hostname wouldn't, since two announcements
            // on the same machine share the host.
            for (auto& [name, inst] : self->instances_) {
                if (inst.addr_ref == sdRef) {
                    inst.service.ipv4_addr_net_byte_order = ipv4;
                    announce_payload = inst.service;
                    inst.announced = true;
                    break;
                }
            }
            cbs = self->callbacks_;
        }

        if (!announce_payload.instance_name.empty() && cbs.on_added) {
            cbs.on_added(announce_payload);
        }
    }

    static std::string unescape_dns_sd(const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ) {
            if (s[i] == '\\' && i + 3 < s.size() &&
                std::isdigit(static_cast<unsigned char>(s[i + 1])) &&
                std::isdigit(static_cast<unsigned char>(s[i + 2])) &&
                std::isdigit(static_cast<unsigned char>(s[i + 3]))) {
                int v = (s[i + 1] - '0') * 100
                      + (s[i + 2] - '0') * 10
                      + (s[i + 3] - '0');
                out.push_back(static_cast<char>(v));
                i += 4;
            } else if (s[i] == '\\' && i + 1 < s.size()) {
                out.push_back(s[i + 1]);
                i += 2;
            } else {
                out.push_back(s[i]);
                ++i;
            }
        }
        return out;
    }
};

std::unique_ptr<Browser> make_bonjour_browser() {
    return std::make_unique<BonjourBrowser>();
}

#ifdef BEEBIUM_HAS_BONJOUR_BROWSE
// On macOS the Bonjour browser is the only provider, so it is the factory. On
// Windows the factory (WindowsDiscovery.cpp) chooses between Bonjour and the
// native DnsService browser at run time and calls make_bonjour_browser().
std::unique_ptr<Browser> create_browser() {
    return make_bonjour_browser();
}
#endif

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_BONJOUR_BROWSE || BEEBIUM_HAS_BONJOUR_BROWSE_DYNAMIC
