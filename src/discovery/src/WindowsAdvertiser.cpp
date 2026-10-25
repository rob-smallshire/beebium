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

#ifdef BEEBIUM_HAS_WINDOWS_MDNS

#include <beebium/discovery/Advertiser.hpp>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windns.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "dnsapi.lib")

namespace beebium::discovery {

namespace {

// Set BEEBIUM_MDNS_TRACE=1 to log the native DnsServiceRegister state machine
// (and the DNS_STATUS of any failure) to stderr.
bool mdns_trace_enabled() {
    static const bool enabled = std::getenv("BEEBIUM_MDNS_TRACE") != nullptr;
    return enabled;
}
#define MDNS_TRACE(...)                                          \
    do {                                                         \
        if (mdns_trace_enabled()) {                              \
            std::fprintf(stderr, "[windns-advert] " __VA_ARGS__); \
            std::fprintf(stderr, "\n");                          \
        }                                                        \
    } while (0)

// Convert UTF-8 std::string to wide string
std::wstring to_wide(const std::string& s) {
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                                  static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(),
                        static_cast<int>(s.size()), result.data(), len);
    return result;
}

// Convert wide string to UTF-8 std::string
std::string to_utf8(const std::wstring& ws) {
    if (ws.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(),
                                  static_cast<int>(ws.size()),
                                  nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(),
                        static_cast<int>(ws.size()),
                        result.data(), len, nullptr, nullptr);
    return result;
}

// Extract instance name from full service name. The format is
// "MyService._service._proto.local" -- the instance is everything
// before the first ".<service-type-prefix>" segment, where the
// service-type-prefix begins with "_" (the DNS-SD convention).
std::wstring extract_instance_name(const std::wstring& full_name) {
    auto pos = full_name.find(L"._");
    if (pos != std::wstring::npos) {
        return full_name.substr(0, pos);
    }
    return full_name;
}

}  // namespace

/// Windows implementation of Advertiser using windns.h.
///
/// Uses DnsServiceRegister() to advertise the service via mDNS.
/// Requires Windows 10 version 1903 (Build 18362) or later.
class WindowsAdvertiser final : public Advertiser {
public:
    WindowsAdvertiser() = default;

    ~WindowsAdvertiser() override { stop(); }

    AdvertiserState state() const override {
        std::lock_guard lock(mutex_);
        return AdvertiserState{
            .available = true,
            .advertising = registered_.load(),
            .actual_name = actual_name_,
            .unavailable_reason = {},
        };
    }

private:
    static std::string status_error(DWORD status) {
        return "registration failed (DnsServiceRegister status " +
               std::to_string(static_cast<unsigned long>(status)) + ")";
    }

    std::string begin(const ServiceInfo& info) override {
        // Store info for state reporting
        {
            std::lock_guard lock(mutex_);
            info_ = info;
        }

        // Build full service instance name.
        // Format: "InstanceName._<service>._<proto>.local"
        // info.service_type is e.g. "_beebium._tcp" or "_aun._udp"; we
        // prepend the instance name with a dot separator and append
        // ".local" to land in the standard DNS-SD scope.
        instance_name_wide_ = to_wide(info.instance_name) + L"."
                            + to_wide(info.service_type) + L".local";

        // Build TXT record keys and values as separate arrays
        txt_keys_.clear();
        txt_values_.clear();
        txt_key_ptrs_.clear();
        txt_value_ptrs_.clear();

        for (const auto& [key, value] : info.txt_records) {
            txt_keys_.push_back(to_wide(key));
            txt_values_.push_back(to_wide(value));
        }

        // Build pointer arrays (must point to stable memory)
        for (auto& key : txt_keys_) {
            txt_key_ptrs_.push_back(const_cast<PWSTR>(key.c_str()));
        }
        for (auto& value : txt_values_) {
            txt_value_ptrs_.push_back(const_cast<PWSTR>(value.c_str()));
        }

        // Populate DNS_SERVICE_INSTANCE
        ZeroMemory(&service_instance_, sizeof(service_instance_));
        service_instance_.pszInstanceName =
            const_cast<PWSTR>(instance_name_wide_.c_str());
        service_instance_.pszHostName = nullptr;  // Use default hostname
        service_instance_.ip4Address = nullptr;   // Auto-detect
        service_instance_.ip6Address = nullptr;   // Auto-detect
        service_instance_.wPort = info.port;
        service_instance_.wPriority = 0;
        service_instance_.wWeight = 0;
        service_instance_.dwPropertyCount =
            static_cast<DWORD>(txt_key_ptrs_.size());
        service_instance_.keys = txt_key_ptrs_.empty() ? nullptr :
                                 txt_key_ptrs_.data();
        service_instance_.values = txt_value_ptrs_.empty() ? nullptr :
                                   txt_value_ptrs_.data();
        service_instance_.dwInterfaceIndex = 0;

        // Prepare registration request
        ZeroMemory(&request_, sizeof(request_));
        request_.Version = DNS_QUERY_REQUEST_VERSION1;
        request_.InterfaceIndex = 0;  // All interfaces
        request_.pServiceInstance = &service_instance_;
        request_.pRegisterCompletionCallback = register_callback;
        request_.pQueryContext = this;
        request_.hCredentials = nullptr;
        request_.unicastEnabled = FALSE;  // Use mDNS, not unicast DNS

        // Initialize cancel structure
        ZeroMemory(&cancel_, sizeof(cancel_));

        DWORD status = DnsServiceRegister(&request_, &cancel_);
        MDNS_TRACE("DnsServiceRegister(%ls) -> status=%lu (%s)",
                   instance_name_wide_.c_str(),
                   static_cast<unsigned long>(status),
                   status == DNS_REQUEST_PENDING ? "PENDING" : "error");
        if (status != DNS_REQUEST_PENDING) {
            return status_error(status);
        }

        return {};
    }

    void withdraw() override {
        // Deregister if registered
        if (registered_.load()) {
            DnsServiceDeRegister(&request_, nullptr);
        }

        // Reset state
        registered_ = false;
        {
            std::lock_guard lock(mutex_);
            actual_name_.clear();
        }

        // Clear stored strings
        instance_name_wide_.clear();
        txt_keys_.clear();
        txt_values_.clear();
        txt_key_ptrs_.clear();
        txt_value_ptrs_.clear();
    }

    // Registration state
    DNS_SERVICE_INSTANCE service_instance_{};
    DNS_SERVICE_REGISTER_REQUEST request_{};
    DNS_SERVICE_CANCEL cancel_{};
    std::atomic<bool> registered_{false};

    // String storage (must outlive registration)
    std::wstring instance_name_wide_;
    std::vector<std::wstring> txt_keys_;
    std::vector<std::wstring> txt_values_;
    std::vector<PWSTR> txt_key_ptrs_;
    std::vector<PWSTR> txt_value_ptrs_;

    // Thread-safe state access
    mutable std::mutex mutex_;
    ServiceInfo info_;
    std::string actual_name_;

    static void WINAPI register_callback(
        DWORD status,
        PVOID context,
        PDNS_SERVICE_INSTANCE instance
    ) {
        auto* self = static_cast<WindowsAdvertiser*>(context);
        MDNS_TRACE("register_callback status=%lu", static_cast<unsigned long>(status));

        if (status == ERROR_SUCCESS && instance != nullptr) {
            self->registered_ = true;

            // Extract actual name (may differ due to collision)
            std::string actual_name;
            {
                std::lock_guard lock(self->mutex_);
                if (instance->pszInstanceName != nullptr) {
                    std::wstring full_name(instance->pszInstanceName);
                    self->actual_name_ = to_utf8(extract_instance_name(full_name));
                } else {
                    self->actual_name_ = self->info_.instance_name;
                }
                actual_name = self->actual_name_;
            }
            self->registered(actual_name);
        } else {
            self->registered_ = false;
            self->registration_failed(status_error(status));
        }

        // Free the instance returned by the callback
        if (instance != nullptr) {
            DnsServiceFreeInstance(instance);
        }
    }
};

// The Windows factory (WindowsDiscovery.cpp) decides between this native
// DnsService advertiser and the Bonjour advertiser at run time, so this file
// exposes a maker rather than defining create_advertiser() directly.
std::unique_ptr<Advertiser> make_windows_native_advertiser() {
    return std::make_unique<WindowsAdvertiser>();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_WINDOWS_MDNS
