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

#ifdef BEEBIUM_HAS_AVAHI

#include <beebium/discovery/Advertiser.hpp>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/alternative.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/strlst.h>
#include <avahi-common/thread-watch.h>

#include <dlfcn.h>

#include <atomic>
#include <mutex>
#include <string>

namespace beebium::discovery {

namespace {

/// Function pointers into libavahi-client / libavahi-common, resolved at
/// runtime with dlopen/dlsym rather than link-time.
///
/// This keeps the self-contained Linux server bundle depending only on the
/// base C/C++ runtime libraries: the .deb's auto-derived shared-library
/// dependencies stay limited to libc6/libgcc-s1/libstdc++6, and the server
/// advertises when Avahi is installed but silently degrades to a no-op when it
/// is not. The Avahi headers are still included at build time for the type and
/// enum definitions; only the symbols are late-bound.
///
/// The member types are pinned to the real prototypes via decltype, so a
/// signature mismatch is a compile error.
struct AvahiApi {
    decltype(&avahi_threaded_poll_new) threaded_poll_new;
    decltype(&avahi_threaded_poll_free) threaded_poll_free;
    decltype(&avahi_threaded_poll_get) threaded_poll_get;
    decltype(&avahi_threaded_poll_start) threaded_poll_start;
    decltype(&avahi_threaded_poll_stop) threaded_poll_stop;
    decltype(&avahi_threaded_poll_quit) threaded_poll_quit;

    decltype(&avahi_client_new) client_new;
    decltype(&avahi_client_free) client_free;
    decltype(&avahi_client_errno) client_errno;

    decltype(&avahi_entry_group_new) entry_group_new;
    decltype(&avahi_entry_group_add_service_strlst) entry_group_add_service_strlst;
    decltype(&avahi_entry_group_commit) entry_group_commit;
    decltype(&avahi_entry_group_reset) entry_group_reset;
    decltype(&avahi_entry_group_is_empty) entry_group_is_empty;
    decltype(&avahi_entry_group_free) entry_group_free;

    decltype(&avahi_string_list_add_pair) string_list_add_pair;
    decltype(&avahi_string_list_free) string_list_free;

    decltype(&avahi_alternative_service_name) alternative_service_name;
    decltype(&avahi_free) free;
    decltype(&avahi_strerror) strerror;
};

/// Resolve the Avahi symbol table once. Returns nullptr if libavahi-client is
/// not installed or any expected symbol is missing, in which case the
/// advertiser reports unavailable and no-ops.
const AvahiApi* load_avahi_api() {
    static const AvahiApi* const cached = [] () -> const AvahiApi* {
        // libavahi-client depends on libavahi-common, so resolving symbols from
        // both against the client handle works via its dependency scope. The
        // handle is intentionally never dlclosed: the process keeps it mapped
        // for its lifetime.
        void* handle = dlopen("libavahi-client.so.3", RTLD_NOW);
        if (!handle) {
            return nullptr;
        }

        auto* api = new AvahiApi{};
        bool ok = true;

#define BEEBIUM_LOAD_AVAHI_SYM(field, symbol)                              \
    api->field = reinterpret_cast<decltype(api->field)>(dlsym(handle, symbol)); \
    ok = ok && (api->field != nullptr)

        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_new, "avahi_threaded_poll_new");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_free, "avahi_threaded_poll_free");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_get, "avahi_threaded_poll_get");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_start, "avahi_threaded_poll_start");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_stop, "avahi_threaded_poll_stop");
        BEEBIUM_LOAD_AVAHI_SYM(threaded_poll_quit, "avahi_threaded_poll_quit");
        BEEBIUM_LOAD_AVAHI_SYM(client_new, "avahi_client_new");
        BEEBIUM_LOAD_AVAHI_SYM(client_free, "avahi_client_free");
        BEEBIUM_LOAD_AVAHI_SYM(client_errno, "avahi_client_errno");
        BEEBIUM_LOAD_AVAHI_SYM(entry_group_new, "avahi_entry_group_new");
        BEEBIUM_LOAD_AVAHI_SYM(entry_group_add_service_strlst,
                               "avahi_entry_group_add_service_strlst");
        BEEBIUM_LOAD_AVAHI_SYM(entry_group_commit, "avahi_entry_group_commit");
        BEEBIUM_LOAD_AVAHI_SYM(entry_group_reset, "avahi_entry_group_reset");
        BEEBIUM_LOAD_AVAHI_SYM(entry_group_is_empty, "avahi_entry_group_is_empty");
        BEEBIUM_LOAD_AVAHI_SYM(entry_group_free, "avahi_entry_group_free");
        BEEBIUM_LOAD_AVAHI_SYM(string_list_add_pair, "avahi_string_list_add_pair");
        BEEBIUM_LOAD_AVAHI_SYM(string_list_free, "avahi_string_list_free");
        BEEBIUM_LOAD_AVAHI_SYM(alternative_service_name,
                               "avahi_alternative_service_name");
        BEEBIUM_LOAD_AVAHI_SYM(free, "avahi_free");
        BEEBIUM_LOAD_AVAHI_SYM(strerror, "avahi_strerror");

#undef BEEBIUM_LOAD_AVAHI_SYM

        if (!ok) {
            delete api;
            return nullptr;
        }
        return api;
    }();
    return cached;
}

}  // namespace

/// Linux Avahi implementation of Advertiser using libavahi-client.
///
/// The Avahi client library is loaded at runtime via dlopen (see AvahiApi), so
/// this advertiser degrades gracefully to a no-op on systems where Avahi is not
/// installed. When it is installed, a threaded poll drives the client's
/// asynchronous state machine: the entry group is (re)created whenever the
/// client reaches the running state, and name collisions are resolved by
/// retrying under an alternative instance name.
class AvahiAdvertiser final : public Advertiser {
public:
    AvahiAdvertiser() : api_(load_avahi_api()) {}

    ~AvahiAdvertiser() override { stop(); }

    AdvertiserState state() const override {
        std::lock_guard lock(mutex_);
        return AdvertiserState{
            .available = (api_ != nullptr),
            .advertising = established_.load(),
            .actual_name = actual_name_,
            .unavailable_reason = api_ ? std::string() : kUnavailableReason,
        };
    }

private:
    static constexpr const char* kUnavailableReason = "avahi client library not found";

    std::string avahi_error(int error) const {
        return std::string("registration failed (") + api_->strerror(error) + ")";
    }

    std::string begin(const ServiceInfo& info) override {
        if (!api_) {
            // libavahi-client is not installed; advertisement is unavailable.
            return kUnavailableReason;
        }

        {
            std::lock_guard lock(mutex_);
            info_ = info;
            actual_name_ = info.instance_name;
        }

        threaded_poll_ = api_->threaded_poll_new();
        if (!threaded_poll_) {
            return "registration failed (cannot create the avahi poll)";
        }

        // AVAHI_CLIENT_NO_FAIL lets the client wait for the daemon rather than
        // failing outright if avahi-daemon is not yet running. The entry group
        // is created from client_callback once the client reaches S_RUNNING.
        //
        // Note: avahi_client_new may invoke client_callback synchronously
        // before returning, so create_services() must use the client pointer
        // it is passed rather than client_.
        int error = 0;
        client_ = api_->client_new(api_->threaded_poll_get(threaded_poll_),
                                   AVAHI_CLIENT_NO_FAIL, client_callback, this,
                                   &error);
        if (!client_) {
            api_->threaded_poll_free(threaded_poll_);
            threaded_poll_ = nullptr;
            return avahi_error(error);
        }

        if (api_->threaded_poll_start(threaded_poll_) < 0) {
            api_->client_free(client_);
            client_ = nullptr;
            api_->threaded_poll_free(threaded_poll_);
            threaded_poll_ = nullptr;
            return "registration failed (cannot start the avahi poll)";
        }

        return {};
    }

    void withdraw() override {
        if (!api_) {
            return;
        }

        // Stop the poll thread first so no callbacks run while we tear down.
        if (threaded_poll_) {
            api_->threaded_poll_stop(threaded_poll_);
        }
        if (group_) {
            api_->entry_group_free(group_);
            group_ = nullptr;
        }
        if (client_) {
            api_->client_free(client_);
            client_ = nullptr;
        }
        if (threaded_poll_) {
            api_->threaded_poll_free(threaded_poll_);
            threaded_poll_ = nullptr;
        }

        established_ = false;
        {
            std::lock_guard lock(mutex_);
            actual_name_.clear();
        }
    }

    const AvahiApi* api_ = nullptr;
    AvahiThreadedPoll* threaded_poll_ = nullptr;
    AvahiClient* client_ = nullptr;
    AvahiEntryGroup* group_ = nullptr;
    std::atomic<bool> established_{false};

    mutable std::mutex mutex_;
    ServiceInfo info_;
    std::string actual_name_;

    /// Create (or recreate) the entry group and add our service to it. Runs on
    /// the Avahi poll thread, or synchronously during avahi_client_new.
    void create_services(AvahiClient* client) {
        if (!group_) {
            group_ = api_->entry_group_new(client, entry_group_callback, this);
            if (!group_) {
                registration_failed(avahi_error(api_->client_errno(client)));
                api_->threaded_poll_quit(threaded_poll_);
                return;
            }
        }

        // Nothing to do if the group already holds our service.
        if (!api_->entry_group_is_empty(group_)) {
            return;
        }

        ServiceInfo info;
        std::string name;
        {
            std::lock_guard lock(mutex_);
            info = info_;
            name = actual_name_;
        }

        AvahiStringList* txt = nullptr;
        for (const auto& [key, value] : info.txt_records) {
            txt = api_->string_list_add_pair(txt, key.c_str(), value.c_str());
        }

        int ret = api_->entry_group_add_service_strlst(
            group_, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
            static_cast<AvahiPublishFlags>(0), name.c_str(),
            info.service_type.c_str(), nullptr, nullptr, info.port, txt);

        if (txt) {
            api_->string_list_free(txt);
        }

        if (ret < 0) {
            if (ret == AVAHI_ERR_COLLISION) {
                // A local service of this name already exists; pick a new name
                // and retry. Each alternative differs, so this terminates.
                pick_alternative_name();
                api_->entry_group_reset(group_);
                create_services(client);
                return;
            }
            registration_failed(avahi_error(ret));
            api_->threaded_poll_quit(threaded_poll_);
            return;
        }

        if (int commit = api_->entry_group_commit(group_); commit < 0) {
            registration_failed(avahi_error(commit));
            api_->threaded_poll_quit(threaded_poll_);
        }
    }

    /// Replace actual_name_ with an Avahi-suggested alternative (e.g. appends
    /// " #2") after a name collision.
    void pick_alternative_name() {
        std::lock_guard lock(mutex_);
        char* alternative = api_->alternative_service_name(actual_name_.c_str());
        actual_name_ = alternative;
        api_->free(alternative);
    }

    static void client_callback(AvahiClient* client, AvahiClientState state,
                                void* userdata) {
        auto* self = static_cast<AvahiAdvertiser*>(userdata);

        switch (state) {
        case AVAHI_CLIENT_S_RUNNING:
            // The daemon is up and our host name is registered; publish.
            self->create_services(client);
            break;

        case AVAHI_CLIENT_S_COLLISION:
        case AVAHI_CLIENT_S_REGISTERING:
            // The host name is being (re)registered. Drop our entry group; it
            // is recreated when the client returns to S_RUNNING.
            if (self->group_) {
                self->api_->entry_group_reset(self->group_);
            }
            self->established_ = false;
            break;

        case AVAHI_CLIENT_CONNECTING:
            // AVAHI_CLIENT_NO_FAIL: the client waits for the daemon and
            // publishes once it reaches S_RUNNING.
            self->registration_failed("avahi-daemon not running");
            break;

        case AVAHI_CLIENT_FAILURE:
            self->established_ = false;
            self->registration_failed(self->avahi_error(self->api_->client_errno(client)));
            self->api_->threaded_poll_quit(self->threaded_poll_);
            break;

        default:
            break;
        }
    }

    static void entry_group_callback(AvahiEntryGroup* /*group*/,
                                     AvahiEntryGroupState state,
                                     void* userdata) {
        auto* self = static_cast<AvahiAdvertiser*>(userdata);

        switch (state) {
        case AVAHI_ENTRY_GROUP_ESTABLISHED: {
            self->established_ = true;
            std::string name;
            {
                std::lock_guard lock(self->mutex_);
                name = self->actual_name_;
            }
            self->registered(name);
            break;
        }

        case AVAHI_ENTRY_GROUP_COLLISION:
            // Another host claimed our instance name; pick an alternative and
            // republish.
            self->established_ = false;
            self->pick_alternative_name();
            self->create_services(self->client_);
            break;

        case AVAHI_ENTRY_GROUP_FAILURE:
            self->established_ = false;
            self->registration_failed(
                self->avahi_error(self->api_->client_errno(self->client_)));
            self->api_->threaded_poll_quit(self->threaded_poll_);
            break;

        default:
            break;
        }
    }
};

std::unique_ptr<Advertiser> create_advertiser() {
    return std::make_unique<AvahiAdvertiser>();
}

}  // namespace beebium::discovery

#endif  // BEEBIUM_HAS_AVAHI
