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

// test_econet_fileserver.cpp
//
// Integration tests for Econet communication with an external file server.
// These tests are discretionary: they SKIP unless the BEEBIUM_FILESERVER
// environment variable is set to point at a running AUN file server.
//
// Example:
//   BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101
//     ./tests/test_econet_fileserver
//
// The file server can be:
//   - BeebEm running an Acorn Level 3 file server (AUNMODE 1)
//   - PiEconetBridge
//   - Real Econet hardware with an AUN bridge
//
// Station number defaults to 101 but can be overridden with BEEBIUM_STATION.
// Local AUN port must match the station's entry in the file server's Econet.cfg
// (e.g. port 10101 for station 101). Set via BEEBIUM_LOCAL_PORT.
// No disc controller is installed, so NFS is the active filing system.

#include <catch2/catch_test_macros.hpp>

#include <beebium/Machines.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/econet/AunBackend.hpp>
#include <beebium/econet/EconetSocket.hpp>
#include <beebium/econet/FourWayHandshake.hpp>
#include <beebium/econet/Mc6854.hpp>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "test_econet_helpers.hpp"
#include "test_keyboard_helpers.hpp"

using namespace beebium;
using namespace beebium::test;

namespace {

// Cross-platform getenv wrapper (avoids MSVC C4996 deprecation warning).
std::optional<std::string> get_env(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) == 0 && value != nullptr) {
        std::string result(value);
        free(value);
        return result;
    }
    return std::nullopt;
#else
    if (const char* value = std::getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
#endif
}

// --- Diagnostic backend decorator ---
// Wraps an AunBackend to count and log all packets sent/received.
class LoggingBackend : public NetworkBackend {
public:
    explicit LoggingBackend(std::unique_ptr<AunBackend> inner)
        : inner_(std::move(inner)) {}

    void send_frame(const NetworkFrame& frame) override {
        ++send_count;
        char buf[256];
        snprintf(buf, sizeof(buf),
            "  TX: type=%d port=%02X ctrl=%02X dest=%d.%d src=%d.%d data=%zu bytes",
            static_cast<int>(frame.type), frame.port, frame.control_byte,
            frame.dest_net, frame.dest_stn, frame.src_net, frame.src_stn,
            frame.data.size());
        log += buf;
        log += hex_dump(frame.data);
        log += "\n";
        inner_->send_frame(frame);
    }

    std::optional<NetworkFrame> receive_frame() override {
        auto result = inner_->receive_frame();
        if (result) {
            ++recv_count;
            char buf[256];
            snprintf(buf, sizeof(buf),
                "  RX: type=%d port=%02X ctrl=%02X dest=%d.%d src=%d.%d data=%zu bytes",
                static_cast<int>(result->type), result->port, result->control_byte,
                result->dest_net, result->dest_stn, result->src_net, result->src_stn,
                result->data.size());
            log += buf;
            log += hex_dump(result->data);
            log += "\n";
        }
        return result;
    }

    static std::string hex_dump(const std::vector<uint8_t>& data) {
        if (data.empty()) return "";
        std::string result = " [";
        char hex[4];
        for (size_t i = 0; i < data.size() && i < 64; ++i) {
            if (i > 0) result += " ";
            snprintf(hex, sizeof(hex), "%02X", data[i]);
            result += hex;
        }
        if (data.size() > 64) result += " ...";
        result += "]";
        return result;
    }

    bool is_connected() const override { return inner_->is_connected(); }

    uint16_t local_port() const override { return inner_->local_port(); }
    void add_peer(uint8_t net, uint8_t stn, uint32_t ip, uint16_t port) {
        inner_->add_peer(net, stn, ip, port);
    }

    int send_count = 0;
    int recv_count = 0;
    std::string log;

private:
    std::unique_ptr<AunBackend> inner_;
};

// --- File server configuration from environment ---

struct FileserverConfig {
    uint8_t net = 0;
    uint8_t stn = 254;
    uint32_t ip_addr = 0;      // network byte order
    uint16_t port = 32768;     // host byte order
    uint8_t station = 101;     // our station number
    uint16_t local_port = 0;   // our AUN port (0 = ephemeral)
    bool valid = false;
};

// Parse BEEBIUM_FILESERVER=net.stn:ip:port (e.g. "0.254:127.0.0.1:32768")
FileserverConfig parse_fileserver_env() {
    FileserverConfig config;

    auto env = get_env("BEEBIUM_FILESERVER");
    if (!env) return config;

    const std::string& str = *env;

    // Parse net.stn
    auto colon1 = str.find(':');
    if (colon1 == std::string::npos) return config;
    std::string addr_part = str.substr(0, colon1);
    auto dot = addr_part.find('.');
    if (dot == std::string::npos) return config;
    config.net = static_cast<uint8_t>(std::stoi(addr_part.substr(0, dot)));
    config.stn = static_cast<uint8_t>(std::stoi(addr_part.substr(dot + 1)));

    // Parse ip:port — IP may contain dots, so find the last colon
    std::string rest = str.substr(colon1 + 1);
    auto last_colon = rest.rfind(':');
    std::string ip_str;
    if (last_colon != std::string::npos) {
        ip_str = rest.substr(0, last_colon);
        config.port = static_cast<uint16_t>(std::stoi(rest.substr(last_colon + 1)));
    } else {
        ip_str = rest;
    }

    if (::inet_pton(AF_INET, ip_str.c_str(), &config.ip_addr) != 1) return config;

    // Optional station number override
    auto stn_env = get_env("BEEBIUM_STATION");
    if (stn_env) {
        config.station = static_cast<uint8_t>(std::stoi(*stn_env));
    }

    // Local AUN port — must match the station's entry in the file server's
    // Econet.cfg so that FindHost() can identify our packets.
    auto port_env = get_env("BEEBIUM_LOCAL_PORT");
    if (port_env) {
        config.local_port = static_cast<uint16_t>(std::stoi(*port_env));
    }

    config.valid = true;
    return config;
}

// --- ADLC / handshake state dump for debugging ---

const char* stage_name(FourWayHandshake::Stage stage) {
    switch (stage) {
        case FourWayHandshake::Stage::Idle:              return "Idle";
        case FourWayHandshake::Stage::ScoutSent:         return "ScoutSent";
        case FourWayHandshake::Stage::ScoutAckReceived:  return "ScoutAckReceived";
        case FourWayHandshake::Stage::DataSent:          return "DataSent";
        case FourWayHandshake::Stage::ScoutReceived:     return "ScoutReceived";
        case FourWayHandshake::Stage::ScoutAckSent:      return "ScoutAckSent";
        case FourWayHandshake::Stage::DataReceived:      return "DataReceived";
        case FourWayHandshake::Stage::ImmediateSent:     return "ImmediateSent";
        case FourWayHandshake::Stage::ImmediateReceived: return "ImmediateReceived";
        case FourWayHandshake::Stage::WaitForIdle:       return "WaitForIdle";
        default:                                          return "Unknown";
    }
}

std::string dump_adlc_state(ModelB& machine) {
    auto& socket = machine.state().memory.econet_socket;
    auto* adlc = socket.adlc();
    if (!adlc) return "(no ADLC)\n";

    std::string result;
    char buf[256];

    snprintf(buf, sizeof(buf), "CR1=%02X CR2=%02X CR3=%02X CR4=%02X\n",
             adlc->cr1(), adlc->cr2(), adlc->cr3(), adlc->cr4());
    result += buf;

    snprintf(buf, sizeof(buf), "SR1=%02X SR2=%02X\n", adlc->sr1(), adlc->sr2());
    result += buf;

    snprintf(buf, sizeof(buf), "  SR1: RDA=%d S2RQ=%d FD=%d CTS=%d TXU=%d TDRA=%d IRQ=%d\n",
             (adlc->sr1() & Mc6854::SR1_RDA) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_S2RQ) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_FD) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_CTS) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_TXU) ? 1 : 0,
             (adlc->sr1() & Mc6854::SR1_TDRA) ? 1 : 0,
             (adlc->sr1() & 0x80) ? 1 : 0);
    result += buf;

    snprintf(buf, sizeof(buf), "  SR2: AP=%d FV=%d INACTIVE=%d ABT=%d ERR=%d DCD=%d OVRN=%d RDA=%d\n",
             (adlc->sr2() & Mc6854::SR2_AP) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_FV) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_INACTIVE) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_ABT) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_ERR) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_DCD) ? 1 : 0,
             (adlc->sr2() & Mc6854::SR2_OVRN) ? 1 : 0,
             (adlc->sr2() & 0x80) ? 1 : 0);
    result += buf;

    snprintf(buf, sizeof(buf), "TX FIFO: empty=%d full=%d  RX FIFO: empty=%d full=%d\n",
             adlc->tx_fifo_empty() ? 1 : 0, adlc->tx_fifo_full() ? 1 : 0,
             adlc->rx_fifo_empty() ? 1 : 0, adlc->rx_fifo_full() ? 1 : 0);
    result += buf;

    snprintf(buf, sizeof(buf), "IRQ output: %d  NMI enable: %d  NMI pending: %d\n",
             adlc->irq_output() ? 1 : 0,
             socket.nmi_enable_ff() ? 1 : 0,
             socket.nmi_pending() ? 1 : 0);
    result += buf;

    auto* hs = socket.handshake();
    if (hs) {
        snprintf(buf, sizeof(buf), "Handshake: stage=%s flag_fill=%d\n",
                 stage_name(hs->stage()), hs->flag_fill_active() ? 1 : 0);
        result += buf;
    }

    return result;
}

// --- Machine setup helper ---

// Set up a Model B with MOS + BASIC + NFS ROM and an AUN backend connected
// to the file server. Returns the LoggingBackend pointer for diagnostics.
LoggingBackend* setup_fileserver_machine(ModelB& machine,
                                         const FileserverConfig& config) {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_TEST_ROM_DIR);
    auto mos = load_rom(rom_dirpath / "acorn-mos_1_20.rom");
    auto basic = load_rom(rom_dirpath / "bbc-basic_2.rom");
    auto nfs = load_rom(rom_dirpath / "acorn-nfs_3_34.rom");
    machine.memory().load_mos(mos.data(), mos.size());
    machine.memory().load_basic(basic.data(), basic.size());
    machine.memory().load_sideways_rom(10, nfs.data(), nfs.size());
    machine.memory().enable_video_output();

    auto aun = std::make_unique<AunBackend>(
        0, config.station, config.local_port);
    if (!aun->is_connected()) return nullptr;
    auto logging = std::make_unique<LoggingBackend>(std::move(aun));
    auto* log_ptr = logging.get();
    log_ptr->add_peer(config.net, config.stn, config.ip_addr, config.port);
    machine.state().memory.econet_socket.enable(
        config.station, std::move(logging), true);
    return log_ptr;
}

// Boot the machine, verify startup, select NFS, and log out any stale session.
void boot_and_select_nfs(ModelB& machine, FrameRenderer& renderer) {
    machine.reset();
    run_cycles(machine, renderer, 4'000'000);
    type_string_with_shift(machine, renderer, "*NET\r", 100000);
    run_cycles(machine, renderer, 2'000'000);
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(machine, renderer, 10'000'000);
}

} // namespace

// =============================================================================
// File Server Integration Tests (discretionary — need BEEBIUM_FILESERVER)
// =============================================================================

TEST_CASE("Econet *. command to file server", "[econet][fileserver]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    auto fs_config = parse_fileserver_env();
    if (!fs_config.valid) SKIP("BEEBIUM_FILESERVER not set "
        "(e.g. BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101)");

    ModelB machine;
    auto* log_ptr = setup_fileserver_machine(machine, fs_config);
    REQUIRE(log_ptr != nullptr);
    INFO("Bound to UDP port " << log_ptr->local_port());

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    boot_and_select_nfs(machine, renderer);
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    // *. — catalogue without logging in first
    type_string_with_shift(machine, renderer, "*.\r", 100000);
    run_cycles(machine, renderer, 20'000'000);

    INFO("Screen after *. command:\n" << dump_screen(machine));
    INFO("ADLC state:\n" << dump_adlc_state(machine));
    INFO("AUN packet log (send=" << log_ptr->send_count
         << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log);

    bool line_jammed   = screen_contains(machine, "Line Jammed");
    bool who_are_you   = screen_contains(machine, "Who are you");
    bool no_reply      = screen_contains(machine, "No reply");
    bool not_listening = screen_contains(machine, "Not listening");
    bool net_error     = screen_contains(machine, "Net Error");

    INFO("Outcomes: LineJammed=" << line_jammed
         << " WhoAreYou=" << who_are_you
         << " NoReply=" << no_reply
         << " NotListening=" << not_listening
         << " NetError=" << net_error);

    CHECK_FALSE(line_jammed);
    // Unauthenticated station: file server should respond with "Who are you?"
    CHECK(who_are_you);
}

TEST_CASE("Econet *I AM SYST then *. lists directory", "[econet][fileserver]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    auto fs_config = parse_fileserver_env();
    if (!fs_config.valid) SKIP("BEEBIUM_FILESERVER not set "
        "(e.g. BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101)");

    ModelB machine;
    auto* log_ptr = setup_fileserver_machine(machine, fs_config);
    REQUIRE(log_ptr != nullptr);
    INFO("Bound to UDP port " << log_ptr->local_port());

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    boot_and_select_nfs(machine, renderer);
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    // *I AM SYST — log in as the SYST user
    type_string_with_shift(machine, renderer, "*I AM SYST\r", 100000);
    run_cycles(machine, renderer, 10'000'000);
    INFO("Screen after *I AM SYST:\n" << dump_screen(machine, 16));

    // *. — catalogue the current directory
    type_string_with_shift(machine, renderer, "*.\r", 100000);
    run_cycles(machine, renderer, 20'000'000);

    INFO("Screen after *.:\n" << dump_screen(machine));
    INFO("ADLC state:\n" << dump_adlc_state(machine));
    INFO("AUN packet log (send=" << log_ptr->send_count
         << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log);

    bool line_jammed   = screen_contains(machine, "Line Jammed");
    bool who_are_you   = screen_contains(machine, "Who are you");
    bool no_reply      = screen_contains(machine, "No reply");
    bool not_listening = screen_contains(machine, "Not listening");
    bool net_error     = screen_contains(machine, "Net Error");
    bool library        = screen_contains(machine, "Library");

    INFO("Outcomes: LineJammed=" << line_jammed
         << " WhoAreYou=" << who_are_you
         << " NoReply=" << no_reply
         << " NotListening=" << not_listening
         << " NetError=" << net_error
         << " Library=" << library);

    CHECK_FALSE(line_jammed);
    CHECK_FALSE(who_are_you);
    CHECK_FALSE(no_reply);
    CHECK_FALSE(not_listening);
    CHECK_FALSE(net_error);
    CHECK(library);

    // Log out so the file server forgets this station.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(machine, renderer, 10'000'000);
}

TEST_CASE("Econet *DATE runs library program from file server", "[econet][fileserver]") {
    if (!base_roms_available()) SKIP("Base ROMs not available");
    if (!nfs_rom_available()) SKIP("NFS ROM not available");

    auto fs_config = parse_fileserver_env();
    if (!fs_config.valid) SKIP("BEEBIUM_FILESERVER not set "
        "(e.g. BEEBIUM_FILESERVER=0.254:127.0.0.1:32768 BEEBIUM_LOCAL_PORT=10101)");

    ModelB machine;
    auto* log_ptr = setup_fileserver_machine(machine, fs_config);
    REQUIRE(log_ptr != nullptr);
    INFO("Bound to UDP port " << log_ptr->local_port());

    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);
    FrameRenderer renderer(&fb);

    boot_and_select_nfs(machine, renderer);
    REQUIRE(screen_contains(machine, "BBC Computer 32K"));
    REQUIRE(screen_contains(machine, "Econet Station"));

    // *I AM SYST — log in as the privileged user
    type_string_with_shift(machine, renderer, "*I AM SYST\r", 100000);
    run_cycles(machine, renderer, 10'000'000);
    INFO("Screen after *I AM SYST:\n" << dump_screen(machine, 16));

    // *DATE — run the DATE utility from the Library directory.
    // The file server loads and executes the program, which prints
    // "Today is ..." with the server's date.
    type_string_with_shift(machine, renderer, "*DATE\r", 100000);
    run_cycles(machine, renderer, 20'000'000);

    INFO("Screen after *DATE:\n" << dump_screen(machine));
    INFO("ADLC state:\n" << dump_adlc_state(machine));
    INFO("AUN packet log (send=" << log_ptr->send_count
         << " recv=" << log_ptr->recv_count << "):\n" << log_ptr->log);

    bool line_jammed   = screen_contains(machine, "Line Jammed");
    bool no_reply      = screen_contains(machine, "No reply");
    bool not_listening = screen_contains(machine, "Not listening");
    bool net_error     = screen_contains(machine, "Net Error");
    bool bad_command   = screen_contains(machine, "Bad command");
    bool today         = screen_contains(machine, "Today");

    INFO("Outcomes: LineJammed=" << line_jammed
         << " NoReply=" << no_reply
         << " NotListening=" << not_listening
         << " NetError=" << net_error
         << " BadCommand=" << bad_command
         << " Today=" << today);

    CHECK_FALSE(line_jammed);
    CHECK_FALSE(no_reply);
    CHECK_FALSE(not_listening);
    CHECK_FALSE(net_error);
    CHECK_FALSE(bad_command);
    CHECK(today);

    // Log out so the file server forgets this station.
    type_string_with_shift(machine, renderer, "*BYE\r", 100000);
    run_cycles(machine, renderer, 10'000'000);
}
