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

#include "beebium/service/KeyboardService.hpp"
#include "beebium/KeyboardMapping.hpp"
#include "beebium/SystemViaPeripheral.hpp"
#include "beebium/TextTranslation.hpp"
#include "beebium/TypeAheadQueue.hpp"

#include <chrono>
#include <sstream>
#include <thread>

namespace beebium::service {

KeyboardServiceImpl::KeyboardServiceImpl(SystemViaPeripheral& keyboard)
    : keyboard_(keyboard)
    , break_callbacks_{} {
}

KeyboardServiceImpl::KeyboardServiceImpl(SystemViaPeripheral& keyboard, BreakCallbacks break_callbacks)
    : keyboard_(keyboard)
    , break_callbacks_(std::move(break_callbacks)) {
}

KeyboardServiceImpl::~KeyboardServiceImpl() = default;

grpc::Status KeyboardServiceImpl::KeyDown(
    grpc::ServerContext* /*context*/,
    const KeyRequest* request,
    KeyResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t ik_number = request->ik_number();
    uint32_t row = (ik_number >> 4) & 0x0F;
    uint32_t column = ik_number & 0x0F;

    if (row >= 10 || column >= 10) {
        response->set_accepted(false);
        return grpc::Status::OK;
    }

    keyboard_.key_down(static_cast<uint8_t>(row), static_cast<uint8_t>(column));
    response->set_accepted(true);

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::KeyUp(
    grpc::ServerContext* /*context*/,
    const KeyRequest* request,
    KeyResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t ik_number = request->ik_number();
    uint32_t row = (ik_number >> 4) & 0x0F;
    uint32_t column = ik_number & 0x0F;

    if (row >= 10 || column >= 10) {
        response->set_accepted(false);
        return grpc::Status::OK;
    }

    keyboard_.key_up(static_cast<uint8_t>(row), static_cast<uint8_t>(column));
    response->set_accepted(true);

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetState(
    grpc::ServerContext* /*context*/,
    const GetStateRequest* /*request*/,
    KeyboardState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    // Get keyboard matrix state from peripheral
    for (uint8_t row = 0; row < 10; ++row) {
        response->add_pressed_rows(keyboard_.get_row_state(row));
    }

    return grpc::Status::OK;
}

// =============================================================================
// Keyboard Links (raw byte access)
// =============================================================================

grpc::Status KeyboardServiceImpl::SetLinks(
    grpc::ServerContext* /*context*/,
    const SetLinksRequest* request,
    SetLinksResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t value = request->value();
    if (value > 255) {
        response->set_success(false);
        response->set_error("value must be 0-255");
        return grpc::Status::OK;
    }

    keyboard_.keyboard().set_startup_options(static_cast<uint8_t>(value));
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetLinks(
    grpc::ServerContext* /*context*/,
    const GetLinksRequest* /*request*/,
    LinksState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_value(keyboard_.keyboard().startup_options());
    return grpc::Status::OK;
}

// =============================================================================
// Semantic accessors
// =============================================================================

grpc::Status KeyboardServiceImpl::SetStartupScreenMode(
    grpc::ServerContext* /*context*/,
    const SetStartupScreenModeRequest* request,
    SetStartupScreenModeResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t mode = request->mode();
    if (mode > 7) {
        response->set_success(false);
        response->set_error("mode must be 0-7");
        return grpc::Status::OK;
    }

    keyboard_.keyboard().set_screen_mode(static_cast<uint8_t>(mode));
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetStartupScreenMode(
    grpc::ServerContext* /*context*/,
    const GetStartupScreenModeRequest* /*request*/,
    StartupScreenModeState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_mode(keyboard_.keyboard().screen_mode());
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::SetStartupAutoBoot(
    grpc::ServerContext* /*context*/,
    const SetStartupAutoBootRequest* request,
    SetStartupAutoBootResponse* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    keyboard_.keyboard().set_auto_boot(request->enabled());
    response->set_success(true);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetStartupAutoBoot(
    grpc::ServerContext* /*context*/,
    const GetStartupAutoBootRequest* /*request*/,
    StartupAutoBootState* response) {

    std::lock_guard<std::mutex> lock(mutex_);

    response->set_enabled(keyboard_.keyboard().auto_boot());
    return grpc::Status::OK;
}

// =============================================================================
// Type-ahead (TypeQuickly)
// =============================================================================

namespace {

// Describe why text could not be typed, naming the character at fault.
//
// Rejecting a whole paste because of one character is unhelpful unless the
// client can say which character it was, so the message carries the codepoint.
std::string describe_untypeable(std::string_view text) {
    const auto offender = first_untypeable(text);
    if (!offender) {
        // enqueue() declined for some reason other than an untypeable
        // character. Say so rather than inventing a cause.
        return "Text was not accepted for typing";
    }

    std::ostringstream message;
    message << "Text contains a character the BBC keyboard cannot type: U+"
            << std::hex << std::uppercase << static_cast<uint32_t>(*offender);
    return message.str();
}

} // namespace

grpc::Status KeyboardServiceImpl::TypeQuickly(
    grpc::ServerContext* /*context*/,
    const TypeQuicklyRequest* request,
    TypeQuicklyResponse* response) {

    // Translation defaults on: interactive pasting wants characters, not
    // bytes. Clients type text verbatim by setting translate to false.
    const bool translate = request->has_translate() ? request->translate() : true;
    const std::string text = translate ? translate_for_typing(request->text())
                                       : request->text();

    // Pacing is the server's responsibility; the queue applies its reliable
    // default hold/gap. Clients needing custom timing use KeyDown/KeyUp.
    bool accepted = keyboard_.type_ahead().enqueue(text);

    if (!accepted) {
        response->set_accepted(false);
        response->set_error(describe_untypeable(text));
        response->set_pending_characters(
            static_cast<uint32_t>(keyboard_.type_ahead().pending_characters()));
        return grpc::Status::OK;
    }

    response->set_accepted(true);
    response->set_pending_characters(
        static_cast<uint32_t>(keyboard_.type_ahead().pending_characters()));
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetTypingStatus(
    grpc::ServerContext* /*context*/,
    const GetTypingStatusRequest* /*request*/,
    TypingStatus* response) {

    response->set_idle(keyboard_.type_ahead().empty());
    response->set_pending_characters(
        static_cast<uint32_t>(keyboard_.type_ahead().pending_characters()));
    response->set_strings_queued(
        static_cast<uint32_t>(keyboard_.type_ahead().strings_queued()));
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::WatchTypingStatus(
    grpc::ServerContext* context,
    const WatchTypingStatusRequest* request,
    grpc::ServerWriter<TypingStatus>* writer) {

    const auto interval = std::chrono::milliseconds(
        request->min_interval_ms() > 0 ? request->min_interval_ms() : 20);

    auto snapshot = [this](TypingStatus& status) {
        auto& type_ahead = keyboard_.type_ahead();
        status.set_idle(type_ahead.empty());
        status.set_pending_characters(
            static_cast<uint32_t>(type_ahead.pending_characters()));
        status.set_strings_queued(
            static_cast<uint32_t>(type_ahead.strings_queued()));
    };

    // Send a snapshot immediately, so a client that subscribes after the queue
    // has already drained is told so rather than waiting for a change that
    // will never come.
    uint64_t last_sequence = keyboard_.type_ahead().status_sequence();
    {
        TypingStatus status;
        snapshot(status);
        if (!writer->Write(status)) {
            return grpc::Status::OK;  // Client went away during the first push.
        }
    }

    while (!context->IsCancelled()) {
        std::this_thread::sleep_for(interval);

        const uint64_t sequence = keyboard_.type_ahead().status_sequence();
        if (sequence == last_sequence) {
            continue;
        }
        last_sequence = sequence;

        TypingStatus status;
        snapshot(status);
        if (!writer->Write(status)) {
            break;  // Client disconnected.
        }
    }

    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::ClearTyping(
    grpc::ServerContext* /*context*/,
    const ClearTypingRequest* /*request*/,
    ClearTypingResponse* response) {

    size_t cleared = keyboard_.type_ahead().clear();
    response->set_characters_cleared(static_cast<uint32_t>(cleared));
    return grpc::Status::OK;
}

// =============================================================================
// Character-to-key mapping
// =============================================================================

grpc::Status KeyboardServiceImpl::GetKeyMapping(
    grpc::ServerContext* /*context*/,
    const GetKeyMappingRequest* request,
    KeyMappingEntry* response) {

    const std::string& char_str = request->character();

    // Extract single codepoint from UTF-8 string
    if (char_str.empty()) {
        response->set_found(false);
        return grpc::Status::OK;
    }

    // Decode first UTF-8 character
    char32_t codepoint;
    uint8_t byte = static_cast<uint8_t>(char_str[0]);

    if ((byte & 0x80) == 0) {
        codepoint = byte;
    } else if ((byte & 0xE0) == 0xC0 && char_str.size() >= 2) {
        codepoint = ((byte & 0x1F) << 6) |
                    (static_cast<uint8_t>(char_str[1]) & 0x3F);
    } else if ((byte & 0xF0) == 0xE0 && char_str.size() >= 3) {
        codepoint = ((byte & 0x0F) << 12) |
                    ((static_cast<uint8_t>(char_str[1]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(char_str[2]) & 0x3F);
    } else if ((byte & 0xF8) == 0xF0 && char_str.size() >= 4) {
        codepoint = ((byte & 0x07) << 18) |
                    ((static_cast<uint8_t>(char_str[1]) & 0x3F) << 12) |
                    ((static_cast<uint8_t>(char_str[2]) & 0x3F) << 6) |
                    (static_cast<uint8_t>(char_str[3]) & 0x3F);
    } else {
        response->set_found(false);
        return grpc::Status::OK;
    }

    auto mapping = char_to_key(codepoint);
    if (!mapping) {
        response->set_found(false);
        return grpc::Status::OK;
    }

    response->set_found(true);
    response->set_character(char_str);
    response->set_ik_number(mapping->ik_number);
    response->set_needs_shift(mapping->needs_shift);
    response->set_name(mapping->name);
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetAllKeyMappings(
    grpc::ServerContext* /*context*/,
    const GetAllKeyMappingsRequest* /*request*/,
    AllKeyMappingsResponse* response) {

    for (const auto& mapping : get_all_mappings()) {
        auto* entry = response->add_mappings();

        // Convert codepoint to UTF-8 string
        char32_t cp = mapping.codepoint;
        std::string utf8;
        if (cp == 0) {
            // Named-only key (no character)
            utf8 = "";
        } else if (cp < 0x80) {
            utf8 = static_cast<char>(cp);
        } else if (cp < 0x800) {
            utf8 += static_cast<char>(0xC0 | (cp >> 6));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            utf8 += static_cast<char>(0xE0 | (cp >> 12));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            utf8 += static_cast<char>(0xF0 | (cp >> 18));
            utf8 += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            utf8 += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (cp & 0x3F));
        }

        entry->set_found(true);
        entry->set_character(utf8);
        entry->set_ik_number(mapping.ik_number);
        entry->set_needs_shift(mapping.needs_shift);
        entry->set_name(mapping.name);
    }

    return grpc::Status::OK;
}

// =============================================================================
// Break key operations
// =============================================================================

grpc::Status KeyboardServiceImpl::BreakDown(
    grpc::ServerContext* /*context*/,
    const BreakDownRequest* /*request*/,
    BreakDownResponse* response) {

    if (break_callbacks_.break_down) {
        break_callbacks_.break_down();
        response->set_success(true);
    } else {
        response->set_success(false);
    }
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::BreakUp(
    grpc::ServerContext* /*context*/,
    const BreakUpRequest* /*request*/,
    BreakUpResponse* response) {

    if (break_callbacks_.break_up) {
        break_callbacks_.break_up();
        response->set_success(true);
    } else {
        response->set_success(false);
    }
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetBreakState(
    grpc::ServerContext* /*context*/,
    const GetBreakStateRequest* /*request*/,
    BreakKeyState* response) {

    if (break_callbacks_.is_in_reset) {
        response->set_is_held(break_callbacks_.is_in_reset());
    } else {
        response->set_is_held(false);
    }
    return grpc::Status::OK;
}

grpc::Status KeyboardServiceImpl::GetLockState(
    grpc::ServerContext* /*context*/,
    const GetLockStateRequest* /*request*/,
    LockKeyState* response) {

    // The addressable-latch lock bits are the MOS's logical CAPS/SHIFT LOCK
    // state (active-low; AddressableLatch corrects for that). Reading them here
    // gives the exact logical state at any emulation speed, with none of the
    // wall-clock duty-cycle settling the lock-LED indicator deliberately adds.
    const auto& latch = keyboard_.latch();
    response->set_caps_lock_on(latch.caps_lock_led());
    response->set_shift_lock_on(latch.shift_lock_led());
    return grpc::Status::OK;
}

} // namespace beebium::service
