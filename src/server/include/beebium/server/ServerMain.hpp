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

#ifndef BEEBIUM_SERVER_SERVER_MAIN_HPP
#define BEEBIUM_SERVER_SERVER_MAIN_HPP

#include "BuiltinExtensions.hpp"
#include "CliArgParsers.hpp"
#include "SidewaysValidation.hpp"
#include "beebium/extension/AttachmentPointCatalogue.hpp"
#include "beebium/extension/AttachmentPlan.hpp"
#include "beebium/serial/EnumeratePorts.hpp"
#include "beebium/extension/EconetTransportRegistry.hpp"
#include "beebium/service/EconetTransportService.hpp"
#include "beebium/service/ExtensionRpcService.hpp"
#include "beebium/service/ExtensionUiService.hpp"
#include "beebium/extension/ExtensionArgParser.hpp"
#include "beebium/extension/ExtensionContext.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionResolver.hpp"
#include "beebium/extension/OneMHzBusPort.hpp"
#include "beebium/extension/PluginLoader.hpp"
#include "beebium/service/PeripheralExtensionService.hpp"
#include "beebium/extension/CoprocessorExtension.hpp"
#include "beebium/extension/CpuDebugTarget.hpp"
#include "CoprocessorDebuggerAdapter.hpp"
#include "beebium/Machines.hpp"
#include "beebium/SidewaysRomHeader.hpp"
#include "beebium/PacingClock.hpp"
#include "beebium/SleepQuantum.hpp"
#include "beebium/disc/DiscLoader.hpp"
#include "beebium/disc/DiscDrive.hpp"
#include "beebium/disc/DiscControllerRegistry.hpp"
#include "beebium/disc/DiscConcepts.hpp"
#include "beebium/econet/EconetConcepts.hpp"
#include "beebium/econet/AunBackend.hpp"
#include "beebium/econet/StationSelection.hpp"
#include "beebium/econet/TestBackend.hpp"
#include "beebium/tube/TubeConcepts.hpp"
#include "beebium/service/Server.hpp"
#include "beebium/server/PresetLoader.hpp"
#include "beebium/server/PresetResolution.hpp"
#include "beebium/server/ServerConfig.hpp"
#include "beebium/server/DiscPaths.hpp"
#include "beebium/server/PresetPaths.hpp"
#include "beebium/server/RomPaths.hpp"
#include "beebium/server/ImageResample.hpp"
#include "beebium/server/ScreenshotCrop.hpp"
#include "beebium/PlatformUtils.hpp"
// The AUN map-file library (parser + writer), for the aun-map subcommands.
#include "AunMapFile.hpp"
#include "AunMapWriter.hpp"

#include <nlohmann/json.hpp>
#include "stb_image_write.h"

#ifdef _WIN32
#include <ws2tcpip.h>
#include <io.h>
#else
#include <arpa/inet.h>
#include <poll.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cerrno>
#include <chrono>
#include "beebium/server/Platform.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace beebium::server {

// Whether stderr is an interactive terminal. The pacing log is human-readable
// diagnostic output only worth streaming when someone is watching a terminal;
// when stderr is a pipe or file (a front end that launched the server, a
// redirect) it is just noise -- and the very thing that fills an undrained pipe
// -- so it is not written there. Structured pacing data is on the gRPC API
// regardless. Evaluated per log tick; cheap.
inline bool stderr_is_terminal() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return ::isatty(STDERR_FILENO) != 0;
#endif
}

// True only when a short line can be written to stderr without blocking. Even a
// terminal can block -- output flow-controlled with Ctrl-S (XOFF) fills its
// buffer -- and the emulation thread must never stall on I/O, so this is a
// belt-and-braces check alongside stderr_is_terminal: poll for writability and
// drop the line rather than block. A writable terminal/file reports POLLOUT; a
// full one, an error or an ambiguous result is treated as "would block".
inline bool stderr_accepts_nonblocking_write() {
#ifdef _WIN32
    return true;  // no server-launching front end pipes stderr on Windows yet
#else
    struct pollfd pfd{};
    pfd.fd = STDERR_FILENO;
    pfd.events = POLLOUT;
    int result;
    // Retry a signal-interrupted poll rather than reading EINTR as "would
    // block": a stray signal must not make the emulation loop drop its log.
    do {
        result = ::poll(&pfd, 1, 0);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (pfd.revents & POLLOUT) != 0;
#endif
}


// WaitMode, OutputFormat, SidewaysSlotType, SidewaysConfig and the
// inline arg parsers (parse_int, parse_floppy_arg, parse_sideways_arg,
// parse_wait_arg, parse_format_arg) live in CliArgParsers.hpp so that
// they can be unit-tested without pulling in the full server template
// stack.

// Exit codes following sysexits.h conventions
namespace ExitCode {
    constexpr int OK = 0;           // Success
    constexpr int USAGE = 64;       // Command line usage error (EX_USAGE)
    constexpr int DATAERR = 65;     // Data format error (EX_DATAERR)
    constexpr int NOINPUT = 66;     // Cannot open input file (EX_NOINPUT)
    constexpr int SOFTWARE = 70;    // Internal software error (EX_SOFTWARE)
    constexpr int IOERR = 74;       // I/O error (EX_IOERR)
    constexpr int CONFIG = 78;      // Configuration error (EX_CONFIG)
}

// Configuration parsed from global (pre-subcommand) arguments
struct GlobalConfig {
    std::string subcommand_name;    // Empty = default to "start"
    int subcommand_argv_start = 1;  // Index where subcommand args begin
    bool help_requested = false;    // --help before subcommand
    OutputFormat output_format = OutputFormat::Auto;  // --format option
};

// Forward declaration for subcommand base class
template<typename MachineType> class Subcommand;

// Abstract base class for subcommands
template<typename MachineType>
class Subcommand {
public:
    virtual ~Subcommand() = default;

    // Subcommand identifier (e.g., "start", "list-fdcs")
    virtual std::string_view name() const = 0;

    // Short description for help listing
    virtual std::string_view description() const = 0;

    // Print subcommand-specific help
    virtual void help(const char* program_name) const = 0;

    // Execute the subcommand
    // Returns exit code
    virtual int invoke(int argc, char* argv[], const GlobalConfig& global) const = 0;
};

// Anonymous namespace for internal implementation details
namespace {

std::atomic<bool> g_running{true};

// Callbacks the shutdown handler uses to interrupt blocking waits. They
// capture objects that live on the main thread's stack (the machine, the
// pacing clock, the gRPC server), but run on the platform's shutdown dispatch
// thread, which may fire at any moment -- so the mutex guards both the
// concurrent access and the objects' lifetimes: clearing a callback blocks
// until any in-flight invocation of it has finished.
std::mutex g_shutdown_callbacks_mutex;
std::function<void()> g_request_machine_shutdown;
std::function<void()> g_request_pacing_stop;
std::function<void()> g_notify_clients_shutdown;

// Shutdown handler logic invoked by platform::install_shutdown_handler()
inline void invoke_shutdown() {
    g_running = false;
    std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
    // Notify connected clients that shutdown is starting
    if (g_notify_clients_shutdown) {
        g_notify_clients_shutdown();
    }
    // Interrupt any blocked wait_if_paused() or wait_for_tick()
    if (g_request_machine_shutdown) {
        g_request_machine_shutdown();
    }
    if (g_request_pacing_stop) {
        g_request_pacing_stop();
    }
}

/// Registers the machine's shutdown callback for as long as it is in scope,
/// so a signal arriving before (or instead of) the emulation loop can still
/// stop a paused machine. Declared alongside the machine it refers to: on
/// destruction it removes the platform handler first, which waits out any
/// callback still running, so the machine cannot die under one.
template<typename MachineType>
class ScopedShutdownHandler {
public:
    explicit ScopedShutdownHandler(MachineType& machine) {
        {
            std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
            g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
        }
        platform::install_shutdown_handler(invoke_shutdown);
    }

    ~ScopedShutdownHandler() {
        platform::remove_shutdown_handler();
        std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
        g_request_machine_shutdown = nullptr;
    }

    ScopedShutdownHandler(const ScopedShutdownHandler&) = delete;
    ScopedShutdownHandler& operator=(const ScopedShutdownHandler&) = delete;
};

inline std::vector<uint8_t> load_file(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + filepath.string());
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(size);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Cannot read file: " + filepath.string());
    }

    return data;
}

// Validate UUID string format (RFC 4122: 8-4-4-4-12 hex digits with hyphens)
// e.g., "550e8400-e29b-41d4-a716-446655440000"
inline bool is_valid_uuid(const std::string& s) {
    if (s.length() != 36) return false;
    for (size_t i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (s[i] != '-') return false;
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(s[i]))) return false;
        }
    }
    return true;
}

// Generate a random UUID v4
inline std::string generate_uuid_v4() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    uint64_t a = dis(gen);
    uint64_t b = dis(gen);

    // Set version 4 (0100) in bits 12-15 of time_hi_and_version
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant (10) in bits 6-7 of clock_seq_hi_and_reserved
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
             static_cast<uint32_t>(a >> 32),
             static_cast<uint16_t>(a >> 16),
             static_cast<uint16_t>(a),
             static_cast<uint16_t>(b >> 48),
             static_cast<unsigned long long>(b & 0x0000FFFFFFFFFFFFULL));
    return std::string(buf);
}

// parse_int has moved to CliArgParsers.hpp.

// Complete a colon-terminated argument value with the next argv element.
// This enables shell tab completion for arguments like "--floppy 0: game.ssd"
// where the shell inserts a space after tab-completing the filepath.
//
// If value ends with ':' and next argv exists and doesn't look like an option,
// appends the next argv to value and advances i to consume it.
//
// Example: "--floppy 0:" followed by "game.ssd" becomes "0:game.ssd"
inline void complete_colon_arg(std::string& value, int& i, int argc, char* argv[]) {
    if (value.empty() || value.back() != ':') {
        return;  // Doesn't end with colon, no completion needed
    }

    int next_i = i + 1;
    if (next_i >= argc) {
        return;  // No next argument available
    }

    std::string_view next_arg = argv[next_i];
    if (next_arg.empty() || next_arg[0] == '-') {
        return;  // Next arg looks like an option, not a continuation
    }

    // Append next arg to value and consume it
    value += std::string(next_arg);
    i = next_i;
}

// parse_floppy_arg, parse_sideways_arg, parse_wait_arg moved to
// CliArgParsers.hpp. SidewaysSlotType / SidewaysConfig live there too.


} // anonymous namespace

namespace {

// Convert a URL or filepath to a canonical file:// URL.
// If the input is already a URL, returns it unchanged.
// If it's a filepath, resolves to canonical absolute path (no . or ..) and prepends file://
inline std::string to_absolute_file_url(const std::string& url_or_filepath) {
    // If it's already a URL, return as-is
    if (url_or_filepath.find("://") != std::string::npos) {
        return url_or_filepath;
    }
    // Convert filepath to canonical absolute path (resolves . and .. and symlinks)
    auto canonical_path = std::filesystem::canonical(url_or_filepath);
    return "file://" + canonical_path.string();
}

// Resolve Auto format to actual format based on TTY detection
inline OutputFormat resolve_output_format(OutputFormat format) {
    if (format == OutputFormat::Auto) {
        return platform::is_stdout_tty() ? OutputFormat::Pretty : OutputFormat::Tsv;
    }
    return format;
}

// parse_format_arg moved to CliArgParsers.hpp.

// Parse global arguments that appear before the subcommand.
// Returns:
//   - std::nullopt on success (continue with subcommand dispatch)
//   - ExitCode::USAGE for errors
inline std::optional<int> parse_global_arguments(int argc, char* argv[], GlobalConfig& global) {
    // Scan arguments looking for global options or subcommand
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        // Check for help flag before subcommand
        if (arg == "--help" || arg == "-h") {
            global.help_requested = true;
            // Continue scanning to see if there's a subcommand after --help
            continue;
        }

        // Check for --format option
        if (arg == "--format" && i + 1 < argc) {
            try {
                global.output_format = parse_format_arg(argv[++i]);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            continue;
        }

        // Check for --format=value form
        if (arg.rfind("--format=", 0) == 0) {
            std::string format_value(arg.substr(9));  // Skip "--format="
            try {
                global.output_format = parse_format_arg(format_value);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            continue;
        }

        // First non-option argument is the subcommand
        if (!arg.empty() && arg[0] != '-') {
            global.subcommand_name = std::string(arg);
            global.subcommand_argv_start = i + 1;
            return std::nullopt;  // Success
        }

        // Argument starting with '-' but not a recognized global option: this is a subcommand option.
        // Stop parsing global args and let the default subcommand handle it.
        // (This enables "beebium --port 8080" to work as "beebium start --port 8080")
        global.subcommand_argv_start = i;  // Subcommand-specific args start here
        return std::nullopt;
    }

    // No subcommand found, will default to "start"
    global.subcommand_argv_start = argc;  // No subcommand-specific args
    return std::nullopt;
}

} // anonymous namespace

template<typename MachineType>
void print_usage(const char* program_name) {
    using Memory = typename MachineType::Memory;
    std::cerr << "Usage: " << program_name << " [options]\n"
              << "\n"
              << "Machine: " << Memory::MACHINE_DISPLAY_NAME << "\n"
              << "\n"
              << "Optional:\n"
              << "  --preset <id|filepath>   Load configuration from a preset file, or from\n"
              << "                           a system or user preset by id (see list-presets;\n"
              << "                           CLI options override preset values)\n"
              << "  --mos <filepath>         MOS ROM filepath (default: " << Memory::DEFAULT_MOS_ROM << ")\n"
              << "  --language-rom <filepath> Language ROM image for this machine's default\n"
              << "                           language slot (slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT)
              << "; default: " << Memory::DEFAULT_LANGUAGE_ROM << ")\n"
              << "  --sideways slot=<0-15>:type=<rom|ram|empty>[:image=<path>][:write-protect]\n"
              << "                           Configure a sideways slot (repeatable). Colon-\n"
              << "                           separated key=value (quote a value containing ':').\n"
              << "                           image required for rom, optional for ram, none for empty.\n"
              << "                           write-protect (ram only) engages the write-protect\n"
              << "                           switch at startup; runtime-toggleable via\n"
              << "                           SidewaysService.SetSlotProtection.\n"
              << "  --write-protect <group>  Engage a protection group's write-protect switch at\n"
              << "                           startup (repeatable). <group> is a protection-group\n"
              << "                           id (e.g. slot-15, board; a running machine\n"
              << "                           lists them in SidewaysService.GetSlotStatus).\n"
              << "  --hide <group>           Engage a protection group's hide switch at startup:\n"
              << "                           its slots read as a constant (repeatable; e.g. the\n"
              << "                           Watford slot-14 / S1 switch).\n"
              << "  --rom-dir <dirpath>      ROM directory (auto-detected if not specified)\n"
              << "  --port <port>            gRPC port (default: " << DEFAULT_GRPC_PORT << ")\n"
              << "  --floppy <drive>:<filepath|url>\n"
              << "                           Load disc image into floppy drive (0 or 1)\n";
    if constexpr (requires { Memory::BOARD_RTC_OPTION; }) {
        std::cerr << "  --" << Memory::BOARD_RTC_OPTION
                  << " clock=<host|emulated>[:time=<YYYY-MM-DDThh:mm[:ss]>|:offset=<e.g. -10y>]\n"
                  << "                           The board's real-time clock. clock=host (default)\n"
                  << "                           follows the host's local time; clock=emulated\n"
                  << "                           advances only with emulated time (deterministic).\n"
                  << "                           time/offset set the start (offset: y M d h m s).\n";
    }

    // Show --fdc option only for machines with disc controller sockets
    if constexpr (HasDiscControllerSocket<Memory>) {
        std::cerr << "  --fdc <type>             Disc controller to install in socket:\n"
                  << "                           acorn-1770 - Acorn WD1770 controller\n"
                  << "                           none - leave socket empty (no disc)\n";
    }

    if constexpr (HasEconetSocket<Memory>) {
        std::cerr << "  --station <n>|auto[:lo-hi]  Econet station number (enables Econet);\n"
                  << "                           'auto' picks a free number in lo-hi (default 1-253)\n";
    }


    std::cerr << "  --screen-mode <0-7>      Startup screen mode (default: 7)\n"
              << "  --auto-boot              Reverse SHIFT-BREAK action (SHIFT-BREAK boots)\n"
              << "  --links <0-255>          Raw startup options byte (mutually exclusive\n"
              << "                           with --screen-mode and --auto-boot)\n";

    // --motherboard-link KEY=VALUE: only relevant on machines whose slot
    // mapping depends on physical jumpers (Model B+ S13 today; others to
    // come). For machines with no such links the option is rejected at
    // parse time, so we hide it from --help to avoid confusion.
    if constexpr (Memory::MotherboardLinks::has_slot_links) {
        std::cerr << "  --motherboard-link KEY=VALUE\n"
                  << "                           Set a motherboard jumper position\n"
                  << "                           (case-insensitive). Repeat for multiple\n"
                  << "                           links. Available on this machine:\n";
        for (const auto& line : Memory::MotherboardLinks::help_lines()) {
            std::cerr << "                             " << line << "\n";
        }
    }

    std::cerr
              << "  --wait[=<mode>]          Wait before starting emulation:\n"
              << "                           cli - wait for RETURN keypress (default if TTY)\n"
              << "                           api - wait for Run() RPC (default if not TTY)\n"
              << "  --speed <multiplier>     Emulation speed as a multiple of real-time\n"
              << "                           (1.0 = real-time, 2.0 = double; 'unlimited' to\n"
              << "                           run flat out)\n"
              << "  --provenance-type <type> Provenance type (e.g., python-client, macos-gui)\n"
              << "  --provenance-uuid <uuid> Provenance instance UUID (RFC 4122)\n"
              << "  --provenance-version <v> Provenance version string\n"
              << "  --machine-uuid <uuid>    Machine identity UUID (RFC 4122, default: auto-generated)\n"
              << "  --machine-name <template> Machine name: text with {key} placeholders\n"
              << "                           filled from the machine's state, e.g.\n"
              << "                           'Station {econet-station}'; see\n"
              << "                           list-name-placeholders (default: from model)\n"
              << "  --machine-ordinal <n>    This machine's instance number from its\n"
              << "                           launcher, shown by {machine-ordinal} as '#n'\n"
              << "  --allow-shutdown         Allow any client to shut down the server\n"
              << "  --advertise              Enable mDNS service advertisement\n"
              << "  --extension-dir <path>   Add an extension search directory. Repeatable;\n"
              << "                           later paths override earlier (and the auto-\n"
              << "                           detected <exe-dir>/extensions) for matching\n"
              << "                           --<cli-name>. Use list-extensions to see what\n"
              << "                           a given combination resolves to.\n"
              << "  --help                   Show this help message\n";

    // Auto-generated extensions section. Built-ins + auto-default extension
    // dir; user --extension-dir paths aren't scanned here (they may not have
    // been parsed yet, and we want --help to be cheap and side-effect-free).
    {
        ServerConfig<MachineType> tmp;
        if (!populate_extension_resolver(program_name, {}, tmp)) {
            const auto& entries = tmp.extension_resolver.entries();
            if (!entries.empty()) {
                std::cerr << "\n"
                          << "Extensions -- configure as: --<name> key=value:key=value ...\n"
                          << "(run 'describe-extension <name>' for each one's parameters):\n";
                for (const auto& e : entries) {
                    std::cerr << "  --" << e.manifest->effective_cli_name();
                    if (!e.manifest->description.empty()) {
                        std::cerr << "\n      " << e.manifest->description;
                    }
                    std::cerr << "\n";
                }
            }
        }
    }

    std::cerr << "\n"
              << "Default sideways ROMs:\n"
              << "  Slot " << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT) << ": "
              << Memory::DEFAULT_LANGUAGE_ROM << " (language ROM)\n";

    // Show DFS default if machine has it
    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  Slot " << static_cast<int>(Memory::DEFAULT_DFS_SLOT) << ": "
                  << Memory::DEFAULT_DFS_ROM << " (disc filing system)\n";
    }

    // Show the expansion board's own ROM if the machine has one
    if constexpr (requires { Memory::DEFAULT_BOARD_ROM; }) {
        std::cerr << "  Slot " << static_cast<int>(Memory::DEFAULT_BOARD_ROM_SLOT) << ": "
                  << Memory::DEFAULT_BOARD_ROM << " (expansion board ROM)\n";
    }

    std::cerr << "\n"
              << "Examples:\n"
              << "  " << program_name << "                                  # Use all defaults\n"
              << "  " << program_name << " --sideways slot=" << static_cast<int>(Memory::DEFAULT_LANGUAGE_SLOT)
              << ":type=rom:image=forth.rom   # Replace BASIC with Forth\n"
              << "  " << program_name << " --floppy 0:game.ssd              # Load disc image in floppy 0\n";

    if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
        std::cerr << "  " << program_name << " --sideways slot=" << static_cast<int>(Memory::DEFAULT_DFS_SLOT)
                  << ":type=empty       # No DFS (leave that slot empty)\n";
    }

    if constexpr (HasDiscControllerSocket<Memory>) {
        std::cerr << "  " << program_name << " --fdc acorn-1770               # Install disc controller\n";
    }
}

// The command-line flag for a machine's built-in real-time clock (e.g.
// "--integra-rtc"), or empty if the machine has none.
template<typename Memory>
std::string board_rtc_flag() {
    if constexpr (requires { Memory::BOARD_RTC_OPTION; }) {
        return "--" + std::string(Memory::BOARD_RTC_OPTION);
    } else {
        return {};
    }
}

// Log an extension instance's configuration and create it: from its plugin
// library, or from the built-in factory for one with no library. The instance's
// config is moved into the extension. Returns nullptr, having said why on
// stderr, when there is nothing to create it from. `what` names the kind for
// the log ("extension", "transport").
template<typename Instance>
std::unique_ptr<beebium::Extension> instantiate_extension(
        const beebium::ExtensionManifest& manifest, Instance& inst,
        beebium::PluginLoader& plugin_loader, std::string_view what) {
    std::cout << "Loading " << what << ": " << manifest.name << " (id=" << inst.config["id"]
              << ")\n";
    for (const auto& [key, value] : inst.config) {
        if (key != "id" && key != "machine_uuid") {
            std::cout << "  " << key << "=" << value << "\n";
        }
    }
    for (const auto& [key, values] : inst.list_config) {
        for (const auto& v : values) {
            std::cout << "  " << key << "=" << v << "\n";
        }
    }

    if (!manifest.library_stem.empty()) {
        return plugin_loader.load_extension(manifest, std::move(inst.config),
                                            std::move(inst.list_config));
    }
    const auto* builtin_entry = beebium::builtin_extensions::find(inst.name);
    if (!builtin_entry) {
        std::cerr << "Error: Extension '" << inst.name
                  << "' has no library and no built-in factory.\n";
        return nullptr;
    }
    auto loaded = builtin_entry->factory();
    loaded->set_manifest(manifest);
    loaded->set_config(std::move(inst.config));
    loaded->set_list_config(std::move(inst.list_config));
    return loaded;
}

// Parse command-line arguments for the 'start' subcommand into a ServerConfig struct.
// Returns:
//   - std::nullopt on success (continue execution)
//   - ExitCode::OK for --help (exit successfully)
//   - ExitCode::USAGE or ExitCode::CONFIG for errors
//
// Preset merge semantics: If --preset is specified, the preset file is loaded first
// and its values populate the config. Then CLI arguments are parsed and can override
// any preset value. This allows presets to provide defaults while CLI provides overrides.
// Resolve the default extension directory <exe-dir>/extensions if it
// exists. beebium_finalize_plugin() (cmake/BeebiumPlugin.cmake) post-
// build-copies every dynamic plugin to this layout so the same rule
// works on every platform and every build configuration -- including
// MSBuild / Xcode multi-config where the exe sits in a $(Configuration)
// subdirectory. The canonical install layout mirrors this: plugins
// ship alongside the server binary. Absent on stripped-down installs.
inline std::optional<std::string> resolve_default_extension_dirpath(const char* argv0) {
    // Resolve the real executable directory from the OS (/proc/self/exe and
    // platform equivalents), which is robust to a bare argv[0]. This matters
    // for the installed layout: a /usr/bin/beebium-model-b symlink invoked by
    // name gives argv[0] == "beebium-model-b", which canonical() cannot resolve
    // against the cwd -- so a plain canonical(argv0) silently finds no
    // extensions. Fall back to argv0 only if the OS lookup fails.
    auto exe_dirpath = beebium::platform::executable_directory();
    if (!exe_dirpath) {
        std::error_code ec;
        auto exe_path = std::filesystem::canonical(std::filesystem::path(argv0), ec);
        if (ec) {
            return std::nullopt;
        }
        exe_dirpath = exe_path.parent_path();
    }
    auto candidate = *exe_dirpath / "extensions";
    if (!std::filesystem::exists(candidate)) {
        return std::nullopt;
    }
    return candidate.string();
}

// Populate the four extension-resolution fields on a ServerConfig. The
// resolver is rebuilt with built-ins (lowest priority), the auto-default
// extension directory if it exists, and each user_dirs entry in order
// (highest priority last). User-supplied paths use the Throw policy --
// a typo is reported up the stack as ExitCode::CONFIG.
template<typename MachineType>
std::optional<int> populate_extension_resolver(
        const char* argv0,
        const std::vector<std::string>& user_dirs,
        ServerConfig<MachineType>& config) {
    config.default_extension_dirpath = resolve_default_extension_dirpath(argv0);

    const auto& builtin_entries = beebium::builtin_extensions::entries();
    config.builtin_manifests.clear();
    config.builtin_manifests.reserve(builtin_entries.size());
    for (const auto& e : builtin_entries) {
        config.builtin_manifests.push_back(e.manifest);
    }

    config.extension_resolver = beebium::ExtensionResolver{};
    config.extension_resolver.add_source("built-in", config.builtin_manifests);
    config.extension_scan_results.clear();

    auto scan = [&](const std::string& path,
                    beebium::PluginLoader::MissingDirPolicy policy) -> std::optional<int> {
        beebium::PluginLoader scanner;
        try {
            config.extension_scan_results.push_back(scanner.scan_manifests(path, policy));
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::CONFIG;
        }
        config.extension_resolver.add_source(path, config.extension_scan_results.back());
        return std::nullopt;
    };

    if (config.default_extension_dirpath) {
        if (auto err = scan(*config.default_extension_dirpath,
                beebium::PluginLoader::MissingDirPolicy::ReturnEmpty)) {
            return err;
        }
    }
    for (const auto& user_path : user_dirs) {
        if (auto err = scan(user_path,
                beebium::PluginLoader::MissingDirPolicy::Throw)) {
            return err;
        }
    }
    return std::nullopt;
}

template<typename MachineType>
std::optional<int> parse_start_arguments(int argc, char* argv[], int start_index,
                                          ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    // First pass: find --preset and --extension-dir before other options
    // --preset is loaded first so CLI can override; --extension-dir is needed
    // to scan manifests so extension CLI flags can be recognised in second pass
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--extension-dir" && i + 1 < argc) {
            config.extension_dirpaths.push_back(argv[i + 1]);
            ++i;
            continue;
        }
        if (arg == "--preset" && i + 1 < argc) {
            if (config.preset_filepath) {
                std::cerr << "Error: --preset may be given only once\n";
                return ExitCode::USAGE;
            }
            // The argument is a preset file path or, failing that, a preset id
            // resolved like show-preset: system presets, then user presets.
            std::string preset_arg = argv[++i];
            config.preset_filepath = locate_preset(preset_arg);
            if (!config.preset_filepath) {
                std::cerr << "Error: " << preset_not_found_message(preset_arg) << "\n";
                return ExitCode::NOINPUT;
            }
            auto result = load_preset(*config.preset_filepath);
            if (!result) {
                std::cerr << "Error: " << result.error()->message << "\n";
                return result.unreadable ? ExitCode::NOINPUT : ExitCode::CONFIG;
            }

            if (auto err = preset_model_error<Memory>(*result.config)) {
                std::cerr << "Error: " << *err << "\n";
                return ExitCode::CONFIG;
            }

            // Apply preset as baseline configuration
            apply_preset(config, *result.config);
        }
    }

    // Build the extension resolver: built-ins (lowest priority), the
    // auto-resolved default <exe-dir>/extensions, then each user-
    // supplied --extension-dir in CLI order (later wins).
    if (auto err = populate_extension_resolver(argv[0], config.extension_dirpaths, config)) {
        return err;
    }

    // Lowercase CLI flag map driven by the resolved manifest set.
    auto cli_name_to_manifest = config.extension_resolver.cli_name_map();

    auto to_lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return s;
    };

    // Second pass: parse all CLI arguments (including --preset which we skip now)
    for (int i = start_index; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--preset" && i + 1 < argc) {
            // Already handled in first pass, skip
            ++i;
            continue;
        } else if (arg == "--help" || arg == "-h") {
            print_usage<MachineType>(argv[0]);
            return ExitCode::OK;
        } else if (arg == "--mos" && i + 1 < argc) {
            config.mos_filepath = argv[++i];
        } else if (arg == "--language-rom" && i + 1 < argc) {
            config.language_rom_filepath = argv[++i];
        } else if (arg == "--write-protect" && i + 1 < argc) {
            config.launch_protections.emplace_back(argv[++i],
                                                   ProtectionKind::WriteProtect);
        } else if (arg == "--hide" && i + 1 < argc) {
            config.launch_protections.emplace_back(argv[++i],
                                                   ProtectionKind::Hide);
        } else if (arg == board_rtc_flag<typename MachineType::Memory>() && i + 1 < argc) {
            try {
                config.board_rtc = parse_board_rtc_arg(argv[++i], arg);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--sideways" && i + 1 < argc) {
            std::string value = argv[++i];
            SidewaysConfig sideways_config;
            try {
                sideways_config = parse_sideways_arg(value);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            config.sideways_configs.push_back(sideways_config);
            // Add ALL slot types to rom_slots to prevent default ROM loading
            // (e.g. type=empty or type=ram prevents the default BASIC loading)
            if (sideways_config.type == SidewaysSlotType::Rom) {
                config.rom_slots[sideways_config.slot] = sideways_config.image_filepath;
            } else if (sideways_config.type == SidewaysSlotType::Empty) {
                config.rom_slots[sideways_config.slot] = EMPTY_SLOT_MARKER;
            } else if (sideways_config.type == SidewaysSlotType::Ram) {
                config.rom_slots[sideways_config.slot] = RAM_SLOT_MARKER;
            }
        } else if (arg == "--rom-dir" && i + 1 < argc) {
            config.rom_dirpath = argv[++i];
        } else if (arg == "--motherboard-link" && i + 1 < argc) {
            // KEY=VALUE; key/value strings interpreted by the machine
            // variant's MotherboardLinks::parse method.
            std::string spec = argv[++i];
            auto eq = spec.find('=');
            if (eq == std::string::npos) {
                std::cerr << "Error: --motherboard-link expects KEY=VALUE, got '"
                          << spec << "'\n";
                return ExitCode::USAGE;
            }
            std::string key = spec.substr(0, eq);
            std::string value = spec.substr(eq + 1);
            if (auto err = config.motherboard_links.parse(key, value)) {
                std::cerr << "Error: " << *err << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--port" && i + 1 < argc) {
            try {
                config.port = static_cast<uint16_t>(parse_int(argv[++i], "--port"));
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--floppy" && i + 1 < argc) {
            std::string value = argv[++i];
            complete_colon_arg(value, i, argc, argv);
            try {
                auto [drive, filepath] = parse_floppy_arg(value);
                config.floppy_filepaths[drive] = filepath;
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--screen-mode" && i + 1 < argc) {
            try {
                config.screen_mode = parse_int(argv[++i], "--screen-mode");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            if (config.screen_mode < 0 || config.screen_mode > 7) {
                std::cerr << "Error: --screen-mode must be 0-7\n";
                return ExitCode::USAGE;
            }
            config.screen_mode_set = true;
        } else if (arg == "--auto-boot") {
            config.auto_boot = true;
            config.auto_boot_set = true;
        } else if (arg == "--links" && i + 1 < argc) {
            try {
                config.raw_links = parse_int(argv[++i], "--links");
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
            if (config.raw_links < 0 || config.raw_links > 255) {
                std::cerr << "Error: --links must be 0-255\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--wait") {
            // Bare --wait: use TTY detection to choose default
            config.wait_mode = platform::is_stdin_tty() ? WaitMode::Cli : WaitMode::Api;
        } else if (arg.rfind("--wait=", 0) == 0) {
            // --wait=cli or --wait=api
            std::string wait_value = arg.substr(7);  // Skip "--wait="
            try {
                config.wait_mode = parse_wait_arg(wait_value);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--speed" || arg.rfind("--speed=", 0) == 0) {
            std::string speed_value;
            if (arg == "--speed") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --speed requires a value "
                                 "(a positive multiplier or 'unlimited')\n";
                    return ExitCode::USAGE;
                }
                speed_value = argv[++i];
            } else {
                speed_value = arg.substr(8);  // Skip "--speed="
            }
            try {
                config.speed_multiplier_override = parse_speed_arg(speed_value);
            } catch (const std::runtime_error& e) {
                std::cerr << "Error: " << e.what() << "\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--fdc" && i + 1 < argc) {
            config.fdc_type = argv[++i];
        } else if (arg == "--station" && i + 1 < argc) {
            std::string error;
            auto spec = beebium::econet::parse_station_spec(argv[++i], error);
            if (!spec.has_value()) {
                std::cerr << "Error: --station " << error << "\n";
                return ExitCode::USAGE;
            }
            if (spec->is_auto) {
                config.station_auto_range = spec->range;
                config.station_number = -1;  // chosen at launch, in install_econet
            } else {
                config.station_number = spec->fixed;
                config.station_auto_range.reset();
            }
        } else if (arg == "--provenance-type" && i + 1 < argc) {
            config.provenance_type = argv[++i];
        } else if (arg == "--provenance-uuid" && i + 1 < argc) {
            config.provenance_uuid = argv[++i];
        } else if (arg == "--provenance-version" && i + 1 < argc) {
            config.provenance_version = argv[++i];
        } else if (arg == "--machine-uuid" && i + 1 < argc) {
            config.machine_uuid = argv[++i];
            if (!is_valid_uuid(config.machine_uuid)) {
                std::cerr << "Error: --machine-uuid must be a valid UUID\n";
                return ExitCode::USAGE;
            }
        } else if (arg == "--machine-name" && i + 1 < argc) {
            config.machine_name = argv[++i];
        } else if (arg == "--machine-ordinal") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --machine-ordinal requires a positive integer\n";
                return ExitCode::USAGE;
            }
            const std::string value = argv[++i];
            unsigned long ordinal = 0;
            const bool digits = !value.empty() &&
                value.find_first_not_of("0123456789") == std::string::npos &&
                value.size() <= 9;
            if (digits) {
                ordinal = std::stoul(value);
            }
            if (!digits || ordinal == 0) {
                std::cerr << "Error: --machine-ordinal must be a positive integer, got '"
                          << value << "'\n";
                return ExitCode::USAGE;
            }
            config.machine_ordinal = static_cast<unsigned>(ordinal);
        } else if (arg == "--allow-shutdown") {
            config.allow_shutdown = true;
        } else if (arg == "--advertise") {
            config.advertise = true;
        } else if (arg == "--extension-dir" && i + 1 < argc) {
            // Already handled in first pass, skip
            ++i;
            continue;
        } else {
            // Check if this is an extension CLI flag (e.g. --acorn-scsi, --scsi-hdd)
            auto manifest_it = cli_name_to_manifest.find(to_lower(arg));
            if (manifest_it != cli_name_to_manifest.end()) {
                const auto* manifest = manifest_it->second;
                std::string arg_string;
                // Consume the next argv as the colon-separated argument string
                // (if it exists and doesn't start with --)
                if (i + 1 < argc && std::string_view(argv[i + 1]).substr(0, 2) != "--") {
                    arg_string = argv[++i];
                }
                auto parse_result = beebium::parse_extension_args(
                    manifest->effective_cli_name(), arg_string, manifest->parameters);
                if (!parse_result.ok) {
                    std::cerr << "Error: " << parse_result.error << "\n";
                    return ExitCode::USAGE;
                }
                config.extension_instances.push_back({
                    manifest->name,
                    std::move(parse_result.config),
                    std::move(parse_result.list_config),
                    /*from_preset=*/false,
                    std::move(parse_result.explicit_keys)
                });
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                print_usage<MachineType>(argv[0]);
                return ExitCode::USAGE;
            }
        }
    }

    // Fold preset-supplied sideways slots in as a baseline now that all CLI
    // --sideways flags have been parsed (CLI wins per slot).
    if (auto errors = fold_in_preset(config); !errors.empty()) {
        for (const auto& error : errors) {
            std::cerr << "Error: " << error.message << "\n";
        }
        return ExitCode::CONFIG;
    }

    return std::nullopt;  // Success, continue execution
}

// Validate configuration for consistency.
// Returns error message if invalid, nullopt if valid.
// Load MOS ROM and sideways ROMs into machine memory.
// Applies defaults to config where not specified.
// Throws on file I/O errors.
template<typename MachineType>
void load_roms(MachineType& machine, ServerConfig<MachineType>& config) {
    // Apply motherboard link state to memory wiring before any ROM is
    // loaded. On Model B+ this drops the IC71 binding from the slot pair
    // S13 has routed away from, so loaded BASIC only appears at the
    // currently-active pair. Other variants don't supply this hook.
    if constexpr (requires (typename MachineType::Memory& m,
                            const typename MachineType::Memory::MotherboardLinks& l) {
                      m.apply_motherboard_links(l);
                  }) {
        machine.state().memory.apply_motherboard_links(config.motherboard_links);
    }

    apply_rom_defaults(config);

    // Load MOS ROM
    auto mos_path = RomPaths::find_rom(config.mos_filepath);
    std::cout << "Loading MOS ROM: " << mos_path << "\n";
    auto mos_data = load_file(mos_path);
    if (mos_data.size() != 16384) {
        std::cerr << "Warning: MOS ROM is " << mos_data.size()
                  << " bytes, expected 16384\n";
    }

    // Load MOS ROM into machine
    std::copy(mos_data.begin(), mos_data.end(),
              machine.state().memory.mos_rom.data());

    // Process all slot configurations using the unified API. The
    // validation step above (validate_sideways_configs) already rejected
    // unsupported (slot, type) pairs against the topology, so we can
    // pass each request straight through.
    for (const auto& [slot, marker_or_filepath] : config.rom_slots) {
        if (marker_or_filepath == EMPTY_SLOT_MARKER) {
            std::cout << "Slot " << static_cast<int>(slot) << ": empty\n";
            machine.state().memory.configure_slot_as_empty(slot);
            continue;
        }

        if (marker_or_filepath == RAM_SLOT_MARKER) {
            std::cout << "Slot " << static_cast<int>(slot) << ": RAM\n";
            machine.state().memory.configure_slot_as_ram(slot);
            continue;
        }

        // Regular ROM loading - use unified API
        auto rom_path = RomPaths::find_rom(marker_or_filepath);
        std::cout << "Loading ROM into slot " << static_cast<int>(slot) << ": " << rom_path << "\n";
        auto rom_data = load_file(rom_path);

        if (rom_data.empty() || rom_data.size() > 16384
            || !std::has_single_bit(rom_data.size())) {
            std::cerr << "Warning: ROM is " << rom_data.size()
                      << " bytes, expected a power-of-two size up to 16384\n";
        }

        // Unified API handles slot type configuration and loading together.
        // Record the resolved filepath as image_name so gRPC clients (the
        // Memory sidebar in particular) can report and act on it - Copy Path
        // and Reveal in Finder both need the absolute path.
        machine.state().memory.load_sideways_rom(
            slot, rom_data.data(), rom_data.size(), rom_path.string());
    }

    // Handle RAM slots with pre-loaded images
    // These are processed after rom_slots to ensure the slot is already configured as RAM
    for (const auto& sideways_config : config.sideways_configs) {
        if (sideways_config.type == SidewaysSlotType::Ram && !sideways_config.image_filepath.empty()) {
            auto ram_path = RomPaths::find_rom(sideways_config.image_filepath);
            auto ram_data = load_file(ram_path);
            std::cout << "Pre-loading RAM slot " << static_cast<int>(sideways_config.slot)
                      << " from " << ram_path << "\n";
            // Use load_sideways_data to load without changing slot type.
            // Record the resolved filepath as image_name (see load_sideways_rom).
            machine.state().memory.load_sideways_data(
                sideways_config.slot, ram_data.data(), ram_data.size(),
                ram_path.string());
        }
    }

    // Engage the RAM write-protect switch on any slot whose --sideways spec
    // asked for it, now that every slot's type is settled. validate_config
    // already ensured the machine has the control and the slots have a switch.
    if constexpr (requires(typename MachineType::Memory m) {
                      m.set_slot_write_protected(uint8_t{0}, bool{});
                  }) {
        auto& memory = machine.state().memory;
        for (const auto& sw : config.sideways_configs) {
            if (!sw.write_protected) continue;
            if (memory.sideways.bank_type(sw.slot) == SlotType::Ram) {
                std::cout << "Slot " << static_cast<int>(sw.slot)
                          << ": write-protected\n";
                memory.set_slot_write_protected(sw.slot, true);
            } else {
                std::cerr << "Warning: --sideways write-protect: slot "
                          << static_cast<int>(sw.slot)
                          << " is not RAM; ignoring\n";
            }
        }
    }

    // Engage the group-addressed protection switches from --write-protect
    // <group> / --hide <group>, now that every slot's type is settled. These
    // reach the board-wide / hide switches the per-slot flag above cannot.
    if constexpr (requires(typename MachineType::Memory m, std::string_view id) {
                      m.set_protection(id, ProtectionKind::WriteProtect, bool{});
                  }) {
        auto& memory = machine.state().memory;
        for (const auto& [group_id, kind] : config.launch_protections) {
            if (memory.set_protection(group_id, kind, true)) {
                std::cout << (kind == ProtectionKind::Hide ? "Hide" : "Write-protect")
                          << " group '" << group_id << "': engaged\n";
            } else {
                throw std::runtime_error(
                    std::string("--")
                    + (kind == ProtectionKind::Hide ? "hide" : "write-protect")
                    + ": no protection group '" + group_id
                    + "' with that control on this machine variant");
            }
        }
    } else if (!config.launch_protections.empty()) {
        throw std::runtime_error(
            "--write-protect/--hide: this machine variant has no protection "
            "controls");
    }

    // The board's built-in real-time clock: time source and start time.
    if constexpr (requires { MachineType::Memory::BOARD_RTC_OPTION; }) {
        if (config.board_rtc) {
            auto& rtc = machine.state().memory.rtc;
            const auto& spec = *config.board_rtc;
            const int64_t host_now =
                Mc146818Rtc::host_local_civil_now().count() / 1'000'000;
            std::optional<int64_t> start = spec.time_civil_seconds;
            if (spec.offset) start = apply_rtc_offset(host_now, *spec.offset);
            if (spec.clock == BoardRtcClock::Emulated) {
                rtc.use_emulated_clock(start.value_or(host_now));
                std::cout << "Real-time clock: emulated time\n";
            } else if (start) {
                rtc.set_time(*start);
                std::cout << "Real-time clock: host time, shifted\n";
            }
        }
    }
}

// Install disc controller for machines with sockets.
// Returns exit code on error, nullopt on success.
template<typename MachineType>
std::optional<int> install_disc_controller(MachineType& machine, const ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    if constexpr (HasDiscControllerSocket<Memory>) {
        if (!config.fdc_type.empty()) {
            if (config.fdc_type == "none") {
                std::cout << "Disc controller: none (socket empty)\n";
                // Do nothing - leave socket empty
            } else if (auto controller = DiscControllerRegistry::create(config.fdc_type)) {
                auto* info = DiscControllerRegistry::find(config.fdc_type);
                std::cout << "Installing disc controller: " << info->display_name
                          << " (" << info->fdc_chip << ")\n";
                machine.state().memory.install_disc_controller(std::move(controller), config.fdc_type);
            } else {
                std::cerr << "Error: Unknown disc controller type: " << config.fdc_type << "\n"
                          << "Use the list-fdcs command to see available disc controllers.\n";
                return 1;
            }
        }
        // If --fdc not specified, socket remains empty (no disc controller)
    } else if (!config.fdc_type.empty()) {
        // Model B+ has built-in controller, --fdc option is not applicable
        std::cerr << "Warning: --fdc option ignored (machine has built-in disc controller)\n";
    }

    return std::nullopt;
}

// Resolve a --station auto request to a concrete number using the transport.
// A fixed station passes through unchanged. `offline` (build-time boots:
// capture-screenshot, and any future non-`start` path that boots from a preset)
// skips the browse and announce entirely and takes the range's first number, so
// preset generation and thumbnails stay deterministic and never touch the LAN
// (the determinism rule: tests, capture and preset generation never touch
// shared or network state). Returns an exit code on error (auto with no
// transport, several transports, or a transport that cannot choose), else
// nullopt, writing the chosen number and whether it was auto-assigned. The
// `start` path calls this AFTER the gRPC server is listening and its port is
// printed, so a slow browse never delays the port line a launcher waits for.
template<typename MachineType>
std::optional<int> resolve_auto_station(
    const ServerConfig<MachineType>& config,
    beebium::EconetTransportRegistry& transport_registry, bool offline,
    int& resolved_station, bool& auto_assigned) {
    resolved_station = config.station_number;
    auto_assigned = false;
    if (!config.station_auto_range.has_value()) {
        return std::nullopt;  // a fixed station (or none)
    }
    if (transport_registry.empty()) {
        std::cerr << "Error: --station auto needs an Econet transport; pass --aun ...\n";
        return 1;
    }
    if (transport_registry.size() > 1) {
        std::cerr << "Error: BBC machines support at most one Econet transport (got "
                  << transport_registry.size() << ").\n";
        return 1;
    }
    auto_assigned = true;
    if (offline) {
        resolved_station = config.station_auto_range->lo;  // deterministic
        return std::nullopt;
    }
    auto& transport = *transport_registry.extensions().front();
    auto outcome = transport.select_auto_station(*config.station_auto_range);
    using Status = beebium::EconetTransportExtension::AutoStationOutcome::Status;
    if (outcome.status == Status::Unsupported) {
        std::cerr << "Error: " << outcome.report << "\n";
        return 1;
    }
    // Exhaustion and a still-contested fall-back carry a report; print it (the
    // design reports the range-exhausted case on stderr, not fail the launch).
    if (!outcome.report.empty()) {
        std::cerr << outcome.report << "\n";
    }
    resolved_station = outcome.station;
    return std::nullopt;
}

// Install Econet hardware for an already-resolved station number (-1 = Econet
// not fitted), on machines with an Econet socket. transport_registry holds the
// econet-transport extension instances (--aun, --piconet, or the preset's
// econet.transport); with one, its create_backend supplies the backend, and
// with none the hardware is fitted with no network. `auto_assigned` only tunes
// the log line. --station auto is resolved by resolve_auto_station beforehand,
// so this does no network I/O beyond the transport's own bring-up. Returns an
// exit code on error, nullopt on success.
template<typename MachineType>
std::optional<int> install_econet(MachineType& machine,
                                   const ServerConfig<MachineType>& config,
                                   beebium::EconetTransportRegistry& transport_registry,
                                   int resolved_station, bool auto_assigned) {
    using Memory = typename MachineType::Memory;

    if constexpr (HasEconetSocket<Memory>) {
        if (resolved_station >= 1) {
            auto station = static_cast<uint8_t>(resolved_station);
            // Appended to the "Econet station N" lines so the log shows when a
            // number was chosen automatically rather than given.
            const char* auto_note = auto_assigned ? " (auto-assigned)" : "";

            // Prefer an econet-transport extension if one was configured.
            // BBC machine policy: at most one transport. (Future Acorn
            // Econet Bridge machine type would relax this.)
            if (!transport_registry.empty()) {
                if (transport_registry.size() > 1) {
                    std::cerr << "Error: BBC machines support at most one Econet "
                                 "transport (got " << transport_registry.size() << ").\n";
                    return 1;
                }
                auto& transport = *transport_registry.extensions().front();
                auto backend = transport.create_backend(station);
                if (backend) {
                    // A transport may hand back a backend that is present but
                    // not yet connected (e.g. Piconet discovery found no
                    // device -- it still returns a disconnected backend so
                    // the UI can Retry it live). Report the state truthfully:
                    // "via transport" only when actually connected, otherwise
                    // the same "configured but unavailable -- no network"
                    // narrative the null-backend path below emits.
                    const bool connected = backend->is_connected();
                    if (connected) {
                        std::cout << "Econet station " << resolved_station
                                  << auto_note << " via transport extension '"
                                  << transport.name() << "'\n";
                    } else {
                        std::cout << "Econet station " << resolved_station
                                  << auto_note << " (transport '" << transport.name()
                                  << "' configured but unavailable -- no network)\n";
                    }
                    machine.state().memory.econet_socket.enable(
                        station, std::move(backend),
                        true,  // FourWayHandshake active
                        transport.requires_real_time_pacing());
                } else {
                    // Transport configured but unavailable (e.g. port=none,
                    // bind failed): install a disconnected TestBackend so
                    // the ADLC reports "no clock" cleanly.
                    auto stub = std::make_unique<beebium::TestBackend>();
                    stub->set_connected(false);
                    machine.state().memory.econet_socket.enable(
                        station, std::move(stub), true,
                        transport.requires_real_time_pacing());
                    std::cout << "Econet station " << resolved_station
                              << auto_note << " (transport '" << transport.name()
                              << "' configured but unavailable -- no network)\n";
                }
                return std::nullopt;
            }

            // No transport configured: Econet hardware fitted but no
            // network connection. The ADLC reports "no clock" so the
            // NFS ROM behaves as if the cable is unplugged.
            auto backend = std::make_unique<beebium::TestBackend>();
            backend->set_connected(false);
            machine.state().memory.econet_socket.enable(
                station, std::move(backend),
                true);  // aun_mode = true (FourWayHandshake wraps disconnected backend)

            std::cout << "Econet station " << resolved_station << auto_note
                      << " (no network -- pass --aun port=N or --piconet device_path=... to enable a transport)\n";
        }
    } else if (config.station_number >= 1 || config.station_auto_range.has_value()) {
        std::cerr << "Warning: --station option ignored (machine has no Econet socket)\n";
    }

    return std::nullopt;
}

// Load a single disc image into a floppy drive.
// Returns an exit code on error, or std::nullopt on success.
inline std::optional<int> load_single_disc(DiscDrive& drive, int drive_num,
                                            const std::string& filepath_or_url) {
    // Resolve filepath to canonical path, checking existence
    std::filesystem::path filepath(filepath_or_url);
    if (filepath_or_url.find("://") == std::string::npos) {
        // Bare filepath: check existence before trying to canonicalize
        std::error_code ec;
        if (!std::filesystem::exists(filepath, ec)) {
            std::cerr << "Error: disc image not found: " << filepath_or_url << "\n";
            return ExitCode::NOINPUT;
        }
        filepath = std::filesystem::canonical(filepath, ec);
        if (ec) {
            std::cerr << "Error: cannot resolve disc image path: " << filepath_or_url
                      << " (" << ec.message() << ")\n";
            return ExitCode::NOINPUT;
        }
    }

    auto source_url = "file://" + filepath.string();
    std::cout << "Loading disc into floppy " << drive_num << ": " << filepath.string() << "\n";

    auto result = load_disc_from_url_or_filepath(filepath.string());
    if (!result) {
        std::cerr << "Error: " << result.error << "\n";
        return ExitCode::DATAERR;
    }

    if (result.disc->is_write_protected()) {
        std::cout << "  (write-protected)\n";
    }
    std::cout << "  format: " << result.disc->format_name() << "\n";

    drive.insert(std::move(result.disc), source_url);
    return std::nullopt;
}

// Flush any pending disc writes to the host image files.
//
// In normal operation each drive flushes itself when the head steps off a
// dirty track and on the controller's short write-inactivity timer. This is
// the shutdown backstop: if the server is asked to stop within that brief
// window after a write, flush whatever remains so no saved data is lost on
// exit.
template<typename MachineType>
void flush_disc_drives(MachineType& machine) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        machine.state().memory.disc_drive_0.flush_if_dirty();
        machine.state().memory.disc_drive_1.flush_if_dirty();
    }
}

// Advance any pending safe eject on the floppy drives.
//
// This belongs to the emulation loop because the emulation thread owns the
// drives: completing an eject frees the disc the disc controller reads pulses
// from, so it must not happen underneath a running machine. It is also the
// only thread guaranteed to exist -- an eject driven from a streaming RPC
// handler stops progressing the moment nobody is listening.
template<typename MachineType>
void tick_disc_drives(MachineType& machine) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        machine.state().memory.disc_drive_0.tick_eject();
        machine.state().memory.disc_drive_1.tick_eject();
    }
}

// Load disc images into floppy drives.
// Returns an exit code on error, or std::nullopt on success.
template<typename MachineType>
std::optional<int> load_disc_images(MachineType& machine, const ServerConfig<MachineType>& config) {
    if constexpr (requires { machine.state().memory.disc_drive_0; }) {
        if (!config.floppy_filepaths[0].empty()) {
            if (auto exit_code = load_single_disc(
                    machine.state().memory.disc_drive_0, 0, config.floppy_filepaths[0])) {
                return exit_code;
            }
        }

        if (!config.floppy_filepaths[1].empty()) {
            if (auto exit_code = load_single_disc(
                    machine.state().memory.disc_drive_1, 1, config.floppy_filepaths[1])) {
                return exit_code;
            }
        }
    }
    return std::nullopt;
}

// Apply startup options (keyboard links) to machine.
// Must be called before machine.reset().
template<typename MachineType>
void apply_startup_options(MachineType& machine, const ServerConfig<MachineType>& config) {
    if (config.raw_links >= 0) {
        // Raw byte overrides everything
        machine.state().memory.set_startup_options(static_cast<uint8_t>(config.raw_links));
        std::cout << "Startup links: 0x" << std::hex << config.raw_links << std::dec << "\n";
    } else {
        // Apply semantic options (modifies specific bits, preserves others)
        if (config.screen_mode_set) {
            machine.state().memory.set_screen_mode(static_cast<uint8_t>(config.screen_mode));
            std::cout << "Startup screen mode: " << config.screen_mode << "\n";
        }
        if (config.auto_boot_set) {
            machine.state().memory.set_auto_boot(config.auto_boot);
            std::cout << "Auto-boot: " << (config.auto_boot ? "enabled" : "disabled") << "\n";
        }
    }
}

// Enter the --wait=api paused-at-first-instruction state: complete the reset
// sequence (7 cycles) so PC holds the real reset vector (read from $FFFC/$FFFD
// during cycles 4-6), then pause. The emulation loop then parks here until a
// Run() RPC resumes. Factored out so the `start` path can enter this state
// BEFORE a deferred --station auto selection, so a client that connects the
// moment the port appears and calls Run() finds the machine paused (and Run
// waits for the selection) rather than racing it. See issue #67.
template<typename MachineType>
void enter_api_wait_state(MachineType& machine) {
    machine.run(7);
    machine.pause();
    std::cout << "Paused at first instruction (PC=$"
              << std::hex << std::uppercase
              << machine.state().cpu.pc.w
              << std::dec << "). Waiting for Run() RPC...\n";
}

// Handle wait mode for controlled startup.
// Must be called after machine.reset() and before the main emulation loop.
// Returns false if a shutdown signal arrived while waiting, in which case the
// caller must not start the emulation loop.
template<typename MachineType>
bool handle_wait_mode(MachineType& machine, WaitMode wait_mode) {
    switch (wait_mode) {
        case WaitMode::Cli:
            // Wait for user to press RETURN before starting emulation
            // "Now press RETURN." is a reference to Roger McGough's electronic poem
            // from the original BBC Micro Welcome cassette.
            std::cout << "Now press RETURN." << std::flush;
            if (!platform::wait_for_line_or_abandon(g_running)) {
                std::cout << "\n";
                return false;
            }
            std::cout << "\n";
            break;

        case WaitMode::Api:
            enter_api_wait_state(machine);
            break;

        case WaitMode::None:
            // Start immediately
            break;
    }
    return g_running.load();
}

// What one call to step_emulation did, for the caller's pacing/stats.
struct StepResult {
    bool parked = false;   // parked on a debugger pause this iteration
    bool ran = false;      // a cycle chunk actually executed
    std::chrono::steady_clock::duration run_duration{};  // time in machine.run()
};

// One iteration of the emulation thread's per-cycle work, shared by EVERY
// subcommand that runs the machine (the serving loop and the screenshot
// capture loop). This is the single place the machine is advanced, so no
// subcommand can acquire a divergent run behaviour (see
// docs/emulation-thread-ownership.md).
//
// It parks while the debugger has paused (ticking the drives as on_wake), then
// -- unless shutting down or a quiescer slipped in -- ticks the drives, updates
// the Econet speed gate and runs one cycle chunk under the busy scope. The
// caller owns pacing, stats and shutdown.
//
//   on_parked : invoked immediately after a debugger-pause park returns, before
//               the budget is taken (the serving loop rebases the pacing clock
//               here, which must happen before wait_for_tick). Pass a no-op
//               (`[]{}`) when there is nothing to do (the screenshot loop).
//   budget    : returns the cycle count for this chunk. The serving loop's
//               budget waits for the next pacing tick inside it -- a sleep that
//               must stay outside the busy scope and out of the run() timing --
//               and the screenshot loop's simply returns a fixed chunk.
template<typename MachineType, typename OnParked, typename Budget>
StepResult step_emulation(MachineType& machine, double speed_multiplier,
                          OnParked&& on_parked, Budget&& budget) {
    using Memory = typename MachineType::Memory;
    StepResult result;

    // A pending safe eject completes on this thread; ticking it as on_wake keeps
    // it progressing even while the machine is parked (a standing-still drive is
    // exactly when a disc may leave).
    result.parked = machine.wait_if_paused([&] { tick_disc_drives(machine); });
    if (result.parked) {
        on_parked();
    }
    if (machine.shutdown_requested()) {
        return result;
    }

    // Decide the cycle budget before entering the busy scope: a pacing
    // wait_for_tick() sleeps here and must not hold the emulation-busy guard.
    const uint64_t cycles = budget();

    // Everything below touches state the emulation thread owns. The busy scope
    // makes a quiescing caller wait for all of it, not just run(); if a pause
    // landed in the gap since wait_if_paused returned, the scope is inactive and
    // we skip the work so the caller re-parks rather than racing the quiescer.
    typename MachineType::EmulationBusyScope busy(machine);
    if (!busy.active()) {
        return result;
    }

    tick_disc_drives(machine);

    // Keep the Econet socket informed of the current speed so a transport that
    // requires real time (Piconet) is gated whenever speed != 1x.
    if constexpr (HasEconetSocket<Memory>) {
        machine.memory().econet_socket.set_emulation_speed(speed_multiplier);
    }

    auto run_start = std::chrono::steady_clock::now();
    machine.run(cycles);
    result.run_duration = std::chrono::steady_clock::now() - run_start;
    result.ran = true;
    return result;
}

// Register the name placeholders `ext` offers, if any (#153). A plugin whose
// placeholders break the registration rules (a key outside its domains, a
// domain another provider owns) is reported and its placeholders left out;
// the machine still launches.
template<typename MachineType>
void register_extension_name_placeholders(beebium::service::Server<MachineType>& server,
                                          const beebium::Extension& ext) {
    try {
        server.name_placeholders().add_extension(ext);
    } catch (const beebium::NamePlaceholderError& e) {
        std::cerr << "Warning: " << e.what() << "; its name placeholders are unavailable\n";
    }
}

// Run the main emulation loop with pacing.
// This function blocks until g_running becomes false (signal handler sets it).
// Sets up shutdown callbacks for clean signal handling.
template<typename MachineType>
void run_emulation_loop(MachineType& machine,
                        beebium::service::Server<MachineType>& server,
                        const ServerConfig<MachineType>& config) {
    using Memory = typename MachineType::Memory;

    // Check for BEEBIUM_NO_PACING environment variable for debugging
    bool use_pacing = !platform::get_env("BEEBIUM_NO_PACING").has_value();

    // Create platform-specific sleep and measure its quantum
    PlatformSleep sleeper;
    auto quantum = beebium::measure_sleep_quantum(sleeper);

    auto pacing_config = Memory::default_pacing_config();
    if (config.speed_multiplier_override.has_value()) {
        pacing_config.speed_multiplier = *config.speed_multiplier_override;
    }

    // Create and start pacing clock with measured quantum
    PacingClock pacing_clock(pacing_config, quantum, std::move(sleeper));

    if (use_pacing) {
        pacing_clock.start();
        std::cout << "Pacing: quantum " << quantum.count() / 1000 << " us"
                  << " (" << 1'000'000'000 / quantum.count() << " Hz)"
                  << ", nominal " << pacing_clock.config().base_clock_hz / 1'000'000 << " MHz"
                  << "\n";
    } else {
        std::cout << "Pacing: DISABLED (BEEBIUM_NO_PACING set)\n";
    }

    // Wire pacing clock to gRPC SystemService for stats monitoring
    if (use_pacing) {
        server.set_pacing_clock(&pacing_clock);
    }

    // Set up shutdown callbacks so the signal handler can interrupt blocked
    // waits and notify clients of impending shutdown. Both capture objects
    // owned by this frame, so they are cleared before it returns (below); the
    // mutex makes that clearing wait out any invocation already running on the
    // shutdown dispatch thread.
    {
        std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
        g_request_machine_shutdown = [&machine]() { machine.request_shutdown(); };
        g_request_pacing_stop = [&pacing_clock]() { pacing_clock.request_stop(); };
        g_notify_clients_shutdown = [&server]() { server.notify_shutdown(5000); };
    }
    // Clear them again however this function is left.
    struct CallbackScope {
        ~CallbackScope() {
            std::lock_guard<std::mutex> lock(g_shutdown_callbacks_mutex);
            g_request_pacing_stop = nullptr;
            g_notify_clients_shutdown = nullptr;
        }
    } callback_scope;

    // Main emulation loop
    constexpr uint64_t cycles_per_frame = 40000;  // For non-paced mode

    // The gRPC pacing throughput is published far more frequently than the
    // human-readable stderr log, so clients (e.g. a speed slider) see a
    // commanded speed change reflected within ~1s rather than waiting up to the
    // 5s log interval. The two windows are tracked independently.
    constexpr auto publish_interval = std::chrono::milliseconds(500);
    constexpr auto log_interval = std::chrono::seconds(5);
    auto loop_start = std::chrono::steady_clock::now();
    auto last_publish_time = loop_start;
    auto last_log_time = loop_start;
    uint64_t last_publish_cycle_count = machine.cycle_count();
    uint64_t last_log_cycle_count = machine.cycle_count();
    std::chrono::steady_clock::duration publish_run_duration{};
    std::chrono::steady_clock::duration log_run_duration{};
    // Reset VSYNC edge counter for frequency measurement
    machine.memory().system_via_peripheral.consume_vsync_rising_edges();
    while (g_running) {
        // The pacing timer ticks thousands of times a second; while the
        // debugger holds the machine paused (including --wait=api before the
        // first Run()) nothing consumes those ticks, so park the timer too and
        // let the process sit idle. It is restarted, re-anchored to the current
        // cycle count, once the machine runs again -- on the park release below
        // or, if a Run() lands before the machine parks, here.
        if (use_pacing) {
            if (machine.is_paused()) {
                pacing_clock.pause();
            } else if (pacing_clock.is_paused()) {
                pacing_clock.rebase(machine.cycle_count());
                pacing_clock.resume();
            }
        }

        // Advance the machine one chunk through the shared stepper. Pacing is
        // this loop's own concern:
        //  - on_parked rebases the pacing clock when the debugger pause released
        //    (the wall time spent paused must not be charged as owed emulation,
        //    or the machine races to "catch up"; a hard reset zeroes the cycle
        //    count, so the baseline must follow it or the deficit underflows and
        //    paces the machine down to a crawl). It runs before wait_for_tick.
        //  - the budget waits for the next pacing tick (a sleep kept out of the
        //    busy scope and the run() timing) and returns that tick's cycles.
        StepResult step = step_emulation(
            machine,
            use_pacing ? pacing_clock.speed_multiplier() : 1.0,
            [&] {
                if (use_pacing) {
                    pacing_clock.rebase(machine.cycle_count());
                    pacing_clock.resume();
                }
            },
            [&]() -> uint64_t {
                if (!use_pacing) return cycles_per_frame;
                pacing_clock.wait_for_tick();
                return pacing_clock.cycles_for_next_tick();
            });

        // Check if shutdown was requested during the wait/park.
        if (machine.shutdown_requested()) {
            break;
        }

        if (step.ran) {
            publish_run_duration += step.run_duration;
            log_run_duration += step.run_duration;
            if (use_pacing) {
                pacing_clock.report_cycles(machine.cycle_count());
            }
        }

        // Periodic pacing stats
        if (use_pacing) {
            auto now = std::chrono::steady_clock::now();
            double target_hz = static_cast<double>(Memory::default_pacing_config().base_clock_hz);

            // Publish a throughput sample to the gRPC pacing stats every ~1s so
            // a commanded speed change is reflected promptly.
            if (now - last_publish_time >= publish_interval) {
                uint64_t current_cycles = machine.cycle_count();
                double secs = std::chrono::duration<double>(now - last_publish_time).count();
                double cycles_delta = static_cast<double>(current_cycles - last_publish_cycle_count);
                double achieved_multiplier = secs > 0.0 ? cycles_delta / secs / target_hz : 0.0;
                double active_fraction = secs > 0.0
                    ? std::chrono::duration<double>(publish_run_duration).count() / secs
                    : 0.0;
                pacing_clock.publish_throughput(
                    achieved_multiplier,
                    estimate_max_speed_multiplier(achieved_multiplier, active_fraction));
                last_publish_time = now;
                last_publish_cycle_count = current_cycles;
                publish_run_duration = {};
            }

            // Human-readable pacing log (every 5s). Written only to an
            // interactive terminal, and only if that will not block (see
            // stderr_is_terminal / stderr_accepts_nonblocking_write): a front end
            // that piped stderr and stopped draining would otherwise fill the
            // pipe and freeze the emulation thread in write(). The vsync counter
            // is consumed and the interval advanced whether or not the line goes
            // out, so a dropped line never skews later stats.
            if (now - last_log_time >= log_interval) {
                auto stats = pacing_clock.timing_stats();
                uint64_t current_cycles = machine.cycle_count();
                uint64_t cycles_delta = current_cycles - last_log_cycle_count;
                double elapsed_secs = std::chrono::duration<double>(now - last_log_time).count();
                double actual_hz = static_cast<double>(cycles_delta) / elapsed_secs;
                uint32_t vsync_edges = machine.memory().system_via_peripheral.consume_vsync_rising_edges();
                double vsync_hz = static_cast<double>(vsync_edges) / elapsed_secs;
                double run_secs = std::chrono::duration<double>(log_run_duration).count();
                double run_pct = 100.0 * run_secs / elapsed_secs;
                std::ostringstream line;
                line << "Pacing: "
                     << std::fixed << std::setprecision(3)
                     << (actual_hz / 1e6) << " MHz"
                     << " (target " << (target_hz / 1e6) << " MHz, "
                     << std::setprecision(1)
                     << (100.0 * actual_hz / target_hz) << "%)"
                     << " | vsync " << std::setprecision(1) << vsync_hz << " Hz"
                     << " | io " << stats.ticks_io_woken
                     << " | deficit " << std::setprecision(0) << stats.controller_deficit
                     << " | run " << std::setprecision(1) << run_pct << "%"
                     << "\n";
                if (stderr_is_terminal() && stderr_accepts_nonblocking_write()) {
                    std::cerr << line.str();
                }
                last_log_time = now;
                last_log_cycle_count = current_cycles;
                log_run_duration = {};
            }
        }
    }

    if (use_pacing) {
        pacing_clock.stop();
    }
}

// ============================================================================
// Machine assembly (shared by every subcommand)
// ============================================================================

// A fully assembled, ready-to-run machine and everything whose lifetime it
// depends on. Produced by assemble_machine() and consumed by both `start`
// (which serves it over gRPC) and `capture-screenshot` (which runs it headless
// to a PNG). It is the ONLY place a MachineType is constructed for running, so
// the two subcommands cannot diverge in how the machine is built.
//
// Member order fixes destruction order (reverse). The machine is destroyed
// last; the registries and context, which hold references into the machine's
// memory, are torn down first. In particular the transport registry must die
// before the EconetSocket backend it references -- a transport's mDNS
// collaborators call back into that backend -- so it is declared after the
// machine. Moving it above the machine would be a use-after-free.
template<typename MachineType>
struct AssembledMachine {
    MachineType machine;
    beebium::EconetTransportRegistry transport_registry;
    beebium::ExtensionRegistry extension_registry;
    // Holds raw pointers into machine memory; only meaningful for the life of
    // the machine, hence declared after it. Emplaced during assembly.
    std::optional<beebium::ExtensionContext> extension_context;
    // The coprocessor extension resolved from the registry, or null. Non-owning
    // (extension_registry owns it).
    beebium::CoprocessorExtension* coprocessor = nullptr;

    // Set when --station auto was DEFERRED past assembly (the `start` path): the
    // range to choose from once the gRPC server is listening and its port is
    // printed, so a slow mDNS browse never delays the port line. Econet is not
    // enabled until then. Empty for a fixed station or an offline resolve.
    std::optional<beebium::econet::StationRange> pending_auto_station;

    AssembledMachine() = default;
    AssembledMachine(const AssembledMachine&) = delete;
    AssembledMachine& operator=(const AssembledMachine&) = delete;
};

// Result of assemble_machine: the machine on success, or an exit code to
// return when assembly failed (assembled == nullptr).
template<typename MachineType>
struct AssemblyOutcome {
    std::unique_ptr<AssembledMachine<MachineType>> assembled;
    int exit_code = ExitCode::OK;
};

// Build the machine from `config`: ROMs, disc controller, Econet (with its
// transport registry), disc images, output enablement, startup options,
// extension registration + resolve_and_init, the server-supplied quiescer
// wiring (bus quiescer for every dispatcher, execution quiescer for the
// coprocessor debug target -- see docs/emulation-thread-ownership.md), and
// reset. gRPC services, counterpart-stop wiring and pacing belong to serving,
// not assembly, and are done by the caller.
//
// `with_audio` enables audio output in addition to video (video is always on);
// the screenshot path passes false. Machine assembly runs on the calling
// thread before the emulation loop starts.
template<typename MachineType>
AssemblyOutcome<MachineType> assemble_machine(ServerConfig<MachineType>& config,
                                              bool with_audio,
                                              bool defer_auto_station = false) {
    using Memory = typename MachineType::Memory;

    std::cout << "Initializing " << Memory::MACHINE_DISPLAY_NAME << "...\n";

    auto am = std::make_unique<AssembledMachine<MachineType>>();
    auto& machine = am->machine;

    // Load ROMs
    load_roms(machine, config);

    // Install disc controller
    if (auto exit_code = install_disc_controller(machine, config)) {
        return {nullptr, *exit_code};
    }

    // Resolve the machine identity UUID up front so transport extensions can
    // pick it up via inst.config["machine_uuid"] before their create_backend
    // runs. The MachineIdentity proper is constructed later (serving), but the
    // UUID -- the only value extensions need -- is fixed once generated.
    if (config.machine_uuid.empty()) {
        config.machine_uuid = generate_uuid_v4();
    }

    beebium::PluginLoader plugin_loader;
    if (config.default_extension_dirpath) {
        std::cout << "Extension directory: " << *config.default_extension_dirpath << "\n";
    }
    for (const auto& p : config.extension_dirpaths) {
        std::cout << "Extension directory: " << p << "\n";
    }

    // Build the Econet transport registry from any econet-transport extension
    // instances, before install_econet so the transport is ready when the
    // machine first reads the ADLC. Transport-extension instances are removed
    // from config.extension_instances; the later peripheral load loop only sees
    // peripherals.
    {
        std::vector<typename ServerConfig<MachineType>::ExtensionInstance> remaining;
        remaining.reserve(config.extension_instances.size());
        for (auto& inst : config.extension_instances) {
            const beebium::ExtensionManifest* manifest =
                config.extension_resolver.find_by_name(inst.name);

            if (!manifest || manifest->extension_kind != "econet-transport") {
                remaining.push_back(std::move(inst));
                continue;
            }

            inst.config["machine_uuid"] = config.machine_uuid;
            auto loaded = instantiate_extension(*manifest, inst, plugin_loader, "transport");
            if (!loaded) {
                return {nullptr, ExitCode::CONFIG};
            }

            auto* transport = dynamic_cast<beebium::EconetTransportExtension*>(loaded.get());
            if (!transport) {
                std::cerr << "Error: extension '" << manifest->name
                          << "' has extension_kind=econet-transport but is "
                             "not an EconetTransportExtension instance.\n";
                return {nullptr, 1};
            }
            (void)loaded.release();
            am->transport_registry.add(
                std::unique_ptr<beebium::EconetTransportExtension>(transport));
        }
        config.extension_instances = std::move(remaining);
    }

    // Validate each transport's config before anything else (issue #158): an
    // invalid --aun discovery= value, say, is a clear launch error here rather
    // than a silent fallback or a failure deep in create_backend.
    for (const auto& transport : am->transport_registry.extensions()) {
        if (auto error = transport->config_error()) {
            std::cerr << "Error: " << transport->name() << " " << *error << "\n";
            return {nullptr, ExitCode::USAGE};
        }
    }

    // Install Econet hardware. --station auto is resolved to a concrete number
    // first. On the `start` path we DEFER that resolution (it may browse mDNS
    // for a few seconds) until the gRPC server is listening and its port is
    // printed, so a launcher's port-wait never times out; Econet is enabled
    // then, before the machine runs. Off that path (capture-screenshot), or for
    // a fixed station, resolve now -- offline (no browse/announce) so build-time
    // boots stay deterministic.
    if (config.station_auto_range.has_value() && defer_auto_station) {
        am->pending_auto_station = config.station_auto_range;
    } else {
        int resolved_station = config.station_number;
        bool auto_assigned = false;
        if (auto exit_code = resolve_auto_station(
                config, am->transport_registry, /*offline=*/true,
                resolved_station, auto_assigned)) {
            return {nullptr, *exit_code};
        }
        if (auto exit_code = install_econet(machine, config, am->transport_registry,
                                            resolved_station, auto_assigned)) {
            return {nullptr, *exit_code};
        }
    }

    // Load disc images
    if (auto exit_code = load_disc_images(machine, config)) {
        return {nullptr, *exit_code};
    }

    // Enable output. Video is always on; audio only when the caller wants it
    // (the screenshot path does not).
    machine.state().memory.enable_video_output();
    if (with_audio) {
        machine.state().memory.enable_audio_output();
    }

    // Apply startup options before reset
    apply_startup_options(machine, config);

    // Reset machine
    machine.reset();

    // Set up peripheral extension registry.
    beebium::ExtensionRegistry& extension_registry = am->extension_registry;
    for (const auto& point : machine_attachment_points<Memory>()) {
        extension_registry.register_extension_point(point);
    }

    // Load peripheral extensions (from --<cli-name> flags). Sort so providers
    // are loaded before consumers (RTLD_GLOBAL takes effect before children
    // resolve parent symbols); stable_partition preserves command-line order.
    std::stable_partition(
        config.extension_instances.begin(),
        config.extension_instances.end(),
        [&config](const auto& inst) {
            const auto* m = config.extension_resolver.find_by_name(inst.name);
            return m && !m->provides.empty();
        });

    for (auto& inst : config.extension_instances) {
        const beebium::ExtensionManifest* manifest =
            config.extension_resolver.find_by_name(inst.name);
        if (!manifest) {
            std::cerr << "Error: Extension '" << inst.name << "' not found";
            for (const auto& p : config.extension_dirpaths) {
                std::cerr << " in " << p;
            }
            if (config.default_extension_dirpath && config.extension_dirpaths.empty()) {
                std::cerr << " in " << *config.default_extension_dirpath;
            }
            std::cerr << "\n";
            return {nullptr, ExitCode::CONFIG};
        }

        // A bare name in a parameter the manifest declares bundled names a
        // shipped image. A hard disc resolves to a per-user working copy via
        // copy-on-first-use, so the shipped read-only master is never opened
        // writable (see DiscPaths); a ROM to the file in its plugin's roms/.
        // Explicit paths (absolute or with a directory component) pass through.
        for (const auto& p : manifest->parameters) {
            if (p.bundled.empty()) continue;
            auto it = inst.config.find(p.key);
            if (it == inst.config.end() || !is_bare_name(it->second)) continue;
            if (p.bundled == "hard-disc") {
                try {
                    it->second =
                        beebium::server::DiscPaths::resolve_disc_image(it->second).string();
                } catch (const std::exception& e) {
                    std::cerr << "Error: cannot resolve disc image for '"
                              << inst.name << "': " << e.what() << "\n";
                    return {nullptr, ExitCode::CONFIG};
                }
            } else {
                const auto images = bundled_images(config.extension_resolver);
                const auto* image = find_bundled_image(images, *manifest, p, it->second);
                if (!image) {
                    std::cerr << "Error: " << inst.name << " " << p.key << " '" << it->second
                              << "' is not a " << p.bundled << " this extension ships"
                              << " (for a file in the working directory, write ./"
                              << it->second << ")\n";
                    return {nullptr, ExitCode::CONFIG};
                }
                it->second = image->filepath.string();
            }
        }

        auto loaded = instantiate_extension(*manifest, inst, plugin_loader, "extension");
        if (!loaded) {
            return {nullptr, ExitCode::CONFIG};
        }

        if (manifest->extension_kind == "peripheral") {
            auto* peripheral = dynamic_cast<beebium::PeripheralExtension*>(loaded.get());
            if (!peripheral) {
                std::cerr << "Error: Extension '" << inst.name
                          << "' has extension_kind=peripheral but is not a "
                             "PeripheralExtension instance.\n";
                return {nullptr, ExitCode::CONFIG};
            }
            (void)loaded.release();
            extension_registry.register_extension(
                std::unique_ptr<beebium::PeripheralExtension>(peripheral));
        } else {
            std::cerr << "Error: Extension '" << inst.name
                      << "' has unsupported extension_kind '"
                      << manifest->extension_kind << "'.\n";
            return {nullptr, ExitCode::CONFIG};
        }
    }

    // Initialise extensions (dependency resolution + topological sort). This is
    // what installs a coprocessor into the Tube socket.
    std::cout << "Initialising extensions...\n";
    am->extension_context.emplace(
        beebium::HasOneMHzBus<Memory> ? &machine.state().memory.one_mhz_bus() : nullptr,
        beebium::HasUserPort<Memory> ? &machine.state().memory.user_port() : nullptr,
        beebium::HasTubeSocket<Memory> ? &machine.state().memory.tube_socket : nullptr,
        &machine.state().memory.indicators,
        beebium::HasSerialPort<Memory> ? &machine.state().memory.serial_port() : nullptr);
    extension_registry.resolve_and_init(*am->extension_context);

    // Wire the bus quiescer into every extension dispatcher so a gRPC-thread
    // handler halts the emulation thread across a device mutation. The server
    // supplies it (forwarding to Machine::with_emulation_paused); a dispatcher
    // author never sets it.
    for (auto* ext : extension_registry.extensions()) {
        for (auto* dispatcher : ext->rpc_dispatchers()) {
            dispatcher->set_bus_quiescer(
                [&machine](const std::function<void()>& fn) {
                    machine.with_emulation_paused(fn);
                });
        }
    }

    // All registrations are complete -- close the registration window and start
    // the indicators consumer thread.
    machine.state().memory.indicators.start();

    // Log initialisation order
    for (auto* ext : extension_registry.extensions()) {
        std::cout << "  " << ext->name();
        if (!ext->id().empty()) {
            std::cout << " [" << ext->id() << "]";
        }
        if (!ext->provides().empty()) {
            std::cout << " provides:";
            for (auto p : ext->provides()) std::cout << " " << p;
        }
        if (!ext->attaches_to().empty()) {
            std::cout << " on:";
            for (auto a : ext->attaches_to()) std::cout << " " << a;
        }
        std::cout << "\n";
    }

    // Find the coprocessor extension, if any, through the abstract interface.
    // There is one Tube socket, so at most one coprocessor may attach.
    for (auto* ext : extension_registry.extensions()) {
        if (auto* cop = dynamic_cast<beebium::CoprocessorExtension*>(ext)) {
            if (am->coprocessor) {
                std::cerr << "Error: more than one coprocessor attached to the "
                             "Tube; the Tube has a single socket.\n";
                return {nullptr, 1};
            }
            am->coprocessor = cop;
        }
    }

    // Wire the coprocessor debug target's execution quiescer: halting the
    // coprocessor (which runs on the host emulation thread) to mutate its debug
    // entries means pausing the host machine. The debugger SERVICE built over
    // this target is a serving concern and belongs to the caller.
    if (am->coprocessor) {
        if (auto* target = am->coprocessor->debug_target()) {
            target->set_execution_quiescer(
                [&machine](const std::function<void()>& fn) {
                    machine.with_emulation_paused(fn);
                });
        }
    }

    return {std::move(am), ExitCode::OK};
}

// ============================================================================
// Concrete subcommand implementations
// ============================================================================

// Forward declarations for subcommand dispatch
template<typename MachineType>
const std::vector<std::unique_ptr<Subcommand<MachineType>>>& get_subcommands();

template<typename MachineType>
void print_global_help(const char* program_name);

// Start subcommand - starts the emulator server (default)
template<typename MachineType>
class StartSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "start"; }
    std::string_view description() const override { return "Start the emulator server (default)"; }

    void help(const char* program_name) const override {
        print_usage<MachineType>(program_name);
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            print_usage<MachineType>(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand-specific arguments
        ServerConfig<MachineType> config;
        if (auto exit_code = parse_start_arguments<MachineType>(argc, argv, global.subcommand_argv_start, config)) {
            return *exit_code;
        }

        // Validate configuration
        if (auto error = validate_config<MachineType>(config)) {
            std::cerr << "Error: " << *error << "\n";
            return ExitCode::CONFIG;
        }

        // Apply provenance defaults (protocol specification)
        if (config.provenance_type.empty()) {
            config.provenance_type = platform::is_stdin_tty() ? "terminal" : "unknown";
        }
        if (config.provenance_uuid.empty()) {
            config.provenance_uuid = generate_uuid_v4();
        } else if (!is_valid_uuid(config.provenance_uuid)) {
            std::cerr << "Error: --provenance-uuid must be a valid UUID\n";
            return ExitCode::USAGE;
        }
        auto timestamp = std::chrono::system_clock::now();

        try {
            // Set ROM directory if specified
            if (!config.rom_dirpath.empty()) {
                RomPaths::set_rom_directory(config.rom_dirpath);
            }

            // Assemble the machine. This is the single shared assembly every
            // subcommand uses, so `start` and `capture-screenshot` cannot diverge
            // in how the machine is built (see assemble_machine).
            auto assembly = assemble_machine<MachineType>(
                config, /*with_audio=*/true, /*defer_auto_station=*/true);
            if (!assembly.assembled) {
                return assembly.exit_code;
            }
            auto& am = *assembly.assembled;
            auto& machine = am.machine;
            auto& extension_registry = am.extension_registry;
            auto& transport_registry = am.transport_registry;
            beebium::CoprocessorExtension* coprocessor_ext = am.coprocessor;

            // Signal handling needs the machine in hand so a machine paused
            // before the emulation loop starts (--wait=api) can still be told to
            // stop. Installed after assembly and torn down while the machine
            // (owned by `assembly`, declared above the services below) is alive.
            ScopedShutdownHandler<MachineType> shutdown_handler(machine);

            // Create core discovery services for frontends to enumerate
            // extensions. PeripheralExtensionService lists peripheral
            // extensions; EconetTransportService lists econet transport
            // extensions and identifies which is active. ExtensionUiService
            // exposes the server-driven UI tree of any extension that has
            // an ExtensionUi -- it reads from both registries so a single
            // SubscribeView call can target a peripheral or a transport.
            beebium::service::PeripheralExtensionServiceImpl peripheral_extension_service(
                extension_registry);
            beebium::service::EconetTransportServiceImpl econet_transport_service(
                transport_registry);
            beebium::service::ExtensionUiServiceImpl extension_ui_service(
                transport_registry, extension_registry);

            // The single channel through which clients drive extension RPC
            // services (the gRPC-free replacement for extension-hosted gRPC
            // services). See docs/discussion/extension-rpc-channel.md.
            beebium::service::ExtensionRpcServiceImpl extension_rpc_service(
                transport_registry, extension_registry);

            // Collect the discovery + channel services. Extension-provided
            // APIs (AunService, PiconetService, the serial/SCSI/RTC services,
            // ...) are NOT hosted as gRPC services by the extensions; clients
            // reach them through the single extension_rpc_service channel.
            std::vector<grpc::Service*> extension_services;
            extension_services.push_back(&peripheral_extension_service);
            extension_services.push_back(&econet_transport_service);
            extension_services.push_back(&extension_ui_service);
            extension_services.push_back(&extension_rpc_service);

            // The coprocessor debugger is the one genuine core gRPC service
            // contributed by an extension: the same DebuggerControl proto as the
            // host debugger, consumed by the typed debugger clients. The server,
            // not the extension, instantiates the debugger against the abstract
            // CpuDebugTarget and wraps it in the adapter, so no extension hosts a
            // gRPC service. The target describes its own CPU, so any family is
            // served through the one service. Both must outlive the server, hence
            // these enclosing-scope owners. (assemble_machine already found the
            // coprocessor and set its execution quiescer; here the server only
            // builds the gRPC service over it.)
            std::unique_ptr<beebium::service::DebuggerControlServiceImpl>
                coprocessor_debugger_impl;
            std::unique_ptr<beebium::CoprocessorDebuggerAdapter> coprocessor_debugger_adapter;
            if (coprocessor_ext) {
                if (auto* target = coprocessor_ext->debug_target()) {
                    coprocessor_debugger_impl =
                        std::make_unique<beebium::service::DebuggerControlServiceImpl>(*target);
                    coprocessor_debugger_adapter =
                        std::make_unique<beebium::CoprocessorDebuggerAdapter>(*coprocessor_debugger_impl);
                    extension_services.push_back(coprocessor_debugger_adapter.get());
                }
            }

            // Start gRPC server
            std::cout << "Starting gRPC server...\n";
            beebium::service::Server<MachineType> server(machine, "0.0.0.0", config.port);

            // Push the configured motherboard link state into the server
            // so that the sideways service (constructed during start())
            // reports the actual topology to clients -- e.g. Model B+
            // with --motherboard-link s13=north shows IC71 at slots 0/1.
            server.set_motherboard_links(config.motherboard_links);

            beebium::service::Provenance provenance{
                config.provenance_type,
                config.provenance_uuid,
                config.provenance_version,
                timestamp
            };

            // Apply identity defaults
            if (config.machine_uuid.empty()) {
                config.machine_uuid = generate_uuid_v4();
            }
            if (config.machine_name.empty()) {
                config.machine_name = std::string(Memory::MACHINE_DISPLAY_NAME);
            }

            // The name is a template (#153); the server renders it, and
            // re-renders it as its placeholders' values change.
            beebium::service::MachineIdentity identity{
                config.machine_uuid,
                config.machine_name,
                std::string(Memory::MACHINE_TYPE),
                std::string(Memory::MACHINE_DISPLAY_NAME),
                config.machine_name
            };

            // Print provenance details for debugging (before move)
            auto timestamp_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                provenance.timestamp.time_since_epoch()).count();
            std::cout << "Provenance: type='" << provenance.type
                      << "', uuid=" << provenance.instance_uuid
                      << ", version=" << (provenance.version.empty() ? "(none)" : "'" + provenance.version + "'")
                      << ", timestamp=" << timestamp_seconds
                      << std::endl;

            // Print identity details
            std::cout << "Identity: uuid=" << identity.uuid
                      << ", name_template='" << identity.name_template
                      << "', model_type='" << identity.model_type
                      << "', model_name='" << identity.model_name
                      << "'" << std::endl;

            // Create shutdown callback that stops the emulation loop when RPC shutdown is requested
            beebium::service::ShutdownPolicyConfig shutdown_policy_config{config.allow_shutdown};
            auto shutdown_callback = [&machine]() {
                g_running = false;
                machine.request_shutdown();
                // Also interrupt pacing clock if it's waiting (may be nullptr during startup)
                if (g_request_pacing_stop) {
                    g_request_pacing_stop();
                }
            };
            // Publish the configured speed (from --speed) before the server
            // accepts connections, so GetPacingStats/SetSpeedMultiplier work
            // immediately -- the pacing clock itself is wired only once the
            // emulation loop starts, below.
            server.set_initial_speed_multiplier(
                config.speed_multiplier_override.value_or(1.0));

            // Placeholders the loaded extensions offer for the machine's name
            // template. A plugin that breaks the registration rules loses its
            // placeholders, not the machine.
            for (auto* ext : extension_registry.extensions()) {
                register_extension_name_placeholders(server, *ext);
            }
            for (const auto& transport : transport_registry.extensions()) {
                register_extension_name_placeholders(server, *transport);
            }
            server.set_launch_preset_name(config.preset_name);
            server.set_launch_coprocessor(am.coprocessor);
            if (config.machine_ordinal) {
                server.set_launch_ordinal(*config.machine_ordinal);
            }

            server.start(std::move(provenance), std::move(identity),
                        config.advertise, shutdown_policy_config, std::move(shutdown_callback),
                        extension_services, &am.transport_registry);

            // Print actual bound port (important when port 0 was requested for dynamic allocation)
            // Flush immediately so clients parsing stdout can detect the port before we block
            std::cout << "Listening on port " << server.port() << std::endl;
            std::cout << Memory::MACHINE_DISPLAY_NAME << " ready. Press Ctrl+C to stop." << std::endl;

            // --station auto was deferred so the port above printed without
            // waiting on the (possibly multi-second) mDNS browse, which a
            // launcher's port-wait must not time out on. Resolve it now, with
            // the gRPC server already listening -- so the status stream shows
            // Econet not-yet-enabled meanwhile -- and enable Econet before the
            // machine is told to run (handle_wait_mode / the first Run RPC
            // below waits for this to finish).
            if (am.pending_auto_station.has_value()) {
                // Enter the --wait=api paused state BEFORE selecting, so a
                // client that connects the moment the port appears and calls
                // Run() finds the machine paused; Run() then waits (via
                // begin/end_startup_selection below) for the selection to
                // finish before resuming, rather than being rejected as
                // "already running" or lost (the macOS black-screen defect).
                if (config.wait_mode == WaitMode::Api) {
                    enter_api_wait_state(machine);
                }
                server.debugger_service().begin_startup_selection();
                int resolved_station = config.station_number;
                bool auto_assigned = false;
                auto exit_code = resolve_auto_station(
                    config, transport_registry, /*offline=*/false,
                    resolved_station, auto_assigned);
                if (!exit_code) {
                    exit_code = install_econet(machine, config, transport_registry,
                                               resolved_station, auto_assigned);
                }
                // Always release a waiting Run(), even on error, so the RPC
                // never hangs on a selection that failed.
                server.debugger_service().end_startup_selection();
                if (exit_code) {
                    return *exit_code;
                }
            }

            // Wire cross-processor debugger stop entirely server-side. Stop
            // detection lives in the two DebuggerControlServiceImpl instances
            // the server owns (the host's and the coprocessor's), so each one's
            // counterpart callback pauses the other processor through the
            // abstract interface.
            if (coprocessor_ext) {
                if (auto* cop = coprocessor_ext->coprocessor()) {
                    // Host breakpoint with stop_counterpart -> pause coprocessor.
                    server.debugger_service().set_counterpart_stop_callback(
                        [cop] { cop->pause(); });
                }
                // Coprocessor breakpoint with stop_counterpart -> pause host.
                // Only when a debugger was created for its family; without one
                // there is no impl to detect the breakpoint.
                if (coprocessor_debugger_impl) {
                    coprocessor_debugger_impl->set_counterpart_stop_callback(
                        [&machine] { machine.pause(); });
                }
            }

            // Handle wait mode. A shutdown signal during the wait means the
            // emulation loop is never entered: the machine was asked to stop
            // before it was ever asked to run. A deferred --station auto launch
            // already entered the Api wait state above (before selecting), so
            // don't enter it again here.
            WaitMode effective_wait_mode = config.wait_mode;
            if (am.pending_auto_station.has_value() &&
                config.wait_mode == WaitMode::Api) {
                effective_wait_mode = WaitMode::None;
            }
            if (handle_wait_mode(machine, effective_wait_mode)) {
                // Run main emulation loop (blocks until shutdown)
                run_emulation_loop(machine, server, config);
            }

            std::cout << "\nShutting down...\n";

            // Emulation has stopped (the loop ran on this thread), so this
            // cannot race the in-emulation flush. Persist any remaining writes.
            flush_disc_drives(machine);

            server.stop();
            extension_registry.shutdown();

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::SOFTWARE;
        }

        return ExitCode::OK;
    }
};

// ListFdcs subcommand - list available disc controllers
template<typename MachineType>
class ListFdcsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-fdcs"; }
    std::string_view description() const override { return "List available disc controllers"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-fdcs\n"
                  << "\n"
                  << "Lists all disc controllers that can be installed in machines\n"
                  << "with a disc controller socket (e.g., Model B).\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Resolve format based on TTY detection
        OutputFormat format = resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Available disc controllers:\n";
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << "  " << info.id << " - " << info.display_name
                              << " (" << info.fdc_chip << ")\n"
                              << "      " << info.description << "\n";
                }
                std::cout << "  none - No disc controller (leave socket empty)\n";
                break;

            case OutputFormat::Tsv:
                std::cout << "id\tdisplay_name\tfdc_chip\tdescription\n";
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << info.id << "\t" << info.display_name << "\t"
                              << info.fdc_chip << "\t" << info.description << "\n";
                }
                std::cout << "none\tNo controller\t-\tLeave socket empty (no disc)\n";
                break;

            case OutputFormat::Jsonl:
                for (const auto& info : DiscControllerRegistry::available()) {
                    std::cout << "{\"id\":\"" << info.id
                              << "\",\"display_name\":\"" << info.display_name
                              << "\",\"fdc_chip\":\"" << info.fdc_chip
                              << "\",\"description\":\"" << info.description << "\"}\n";
                }
                std::cout << "{\"id\":\"none\",\"display_name\":\"No controller\","
                          << "\"fdc_chip\":\"-\",\"description\":\"Leave socket empty (no disc)\"}\n";
                break;

            case OutputFormat::Auto:
                // Should not reach here after resolve_output_format
                break;
        }
        return ExitCode::OK;
    }
};

// ListFloppyFormats subcommand - list supported floppy disc image formats
template<typename MachineType>
class ListFloppyFormatsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-floppy-formats"; }
    std::string_view description() const override { return "List supported floppy disc image formats"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-floppy-formats\n"
                  << "\n"
                  << "Lists all floppy disc image formats that can be loaded.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        OutputFormat format = resolve_output_format(global.output_format);
        const auto& registry = default_format_registry();

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Supported floppy disc image formats:\n";
                for (const auto& handler : registry.handlers()) {
                    std::cout << "  " << handler->format_name() << "\n"
                              << "      " << handler->format_description() << "\n"
                              << "      Extensions:";
                    for (auto ext : handler->file_extensions()) {
                        std::cout << " " << ext;
                    }
                    std::cout << "\n";
                    std::cout << "      Write support: " << (handler->supports_write() ? "yes" : "no") << "\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "name\tdescription\textensions\twrite_support\n";
                for (const auto& handler : registry.handlers()) {
                    std::cout << handler->format_name() << "\t"
                              << handler->format_description() << "\t";
                    bool first = true;
                    for (auto ext : handler->file_extensions()) {
                        if (!first) std::cout << ",";
                        std::cout << ext;
                        first = false;
                    }
                    std::cout << "\t" << (handler->supports_write() ? "yes" : "no") << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& handler : registry.handlers()) {
                    std::cout << "{\"name\":\"" << handler->format_name()
                              << "\",\"description\":\"" << handler->format_description()
                              << "\",\"extensions\":[";
                    bool first = true;
                    for (auto ext : handler->file_extensions()) {
                        if (!first) std::cout << ",";
                        std::cout << "\"" << ext << "\"";
                        first = false;
                    }
                    std::cout << "],\"write_support\":" << (handler->supports_write() ? "true" : "false")
                              << "}\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

namespace extension_listing {

// Minimal JSON string escaping for description / name fields.
inline std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// Render a string list as a JSON array, e.g. ["serial-port","user-port"].
inline std::string json_string_array(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += "\"" + json_escape(items[i]) + "\"";
    }
    out += "]";
    return out;
}

// Join a string list with commas for a single TSV / pretty cell.
inline std::string join_comma(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += items[i];
    }
    return out;
}

// Parse just `--extension-dir <path>` arguments out of a subcommand argv slice.
// Other tokens are reported as "unknown argument" by the caller.
struct ExtensionDirParse {
    std::vector<std::string> extension_dirpaths;
    std::vector<std::string> remaining;  // tokens that were not --extension-dir
};

inline ExtensionDirParse parse_extension_dir_args(int argc, char* argv[], int start) {
    ExtensionDirParse result;
    for (int i = start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--extension-dir" && i + 1 < argc) {
            result.extension_dirpaths.emplace_back(argv[++i]);
            continue;
        }
        result.remaining.emplace_back(arg);
    }
    return result;
}

}  // namespace extension_listing

// ListExtensions subcommand - list all available extensions
template<typename MachineType>
class ListExtensionsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-extensions"; }
    std::string_view description() const override {
        return "List available peripheral and transport extensions";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " list-extensions [--attaches-to <point>] [--extension-dir <path>]...\n"
                  << "\n"
                  << "Lists every extension that can be selected via a --<cli-name> flag,\n"
                  << "in the same priority order as `start` would resolve them: built-in\n"
                  << "extensions, then the auto-detected <exe-dir>/extensions directory,\n"
                  << "then each --extension-dir given here. Later sources override earlier\n"
                  << "ones for matching cli-name.\n"
                  << "\n"
                  << "  --attaches-to <point>  show only extensions that attach to the given\n"
                  << "                         attachment point (e.g. serial-port); see\n"
                  << "                         list-attachment-points for the points.\n"
                  << "\n"
                  << "Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        auto parsed = extension_listing::parse_extension_dir_args(
            argc, argv, global.subcommand_argv_start);
        std::string attaches_to_filter;
        for (std::size_t i = 0; i < parsed.remaining.size(); ++i) {
            const std::string& tok = parsed.remaining[i];
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            if (tok == "--attaches-to" && i + 1 < parsed.remaining.size()) {
                attaches_to_filter = parsed.remaining[++i];
                continue;
            }
            std::cerr << "Unknown argument: " << tok << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        if (auto err = populate_extension_resolver(argv[0], parsed.extension_dirpaths, config)) {
            return *err;
        }

        OutputFormat format = resolve_output_format(global.output_format);

        // Optionally filter to extensions that attach to a given point. A point
        // appears in an extension's attaches_to list (which may name several).
        const auto& all_entries = config.extension_resolver.entries();
        std::vector<const typename std::decay_t<decltype(all_entries)>::value_type*> entries;
        for (const auto& e : all_entries) {
            if (!attaches_to_filter.empty()) {
                const auto& a = e.manifest->attaches_to;
                if (std::find(a.begin(), a.end(), attaches_to_filter) == a.end()) {
                    continue;
                }
            }
            entries.push_back(&e);
        }

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Available extensions:\n";
                for (const auto* e : entries) {
                    std::cout << "  --" << e->manifest->effective_cli_name();
                    if (e->manifest->name != e->manifest->effective_cli_name()) {
                        std::cout << " (" << e->manifest->name << ")";
                    }
                    std::cout << " [" << e->source_label << "]\n";
                    if (!e->manifest->attaches_to.empty()) {
                        std::cout << "      attaches to: "
                                  << extension_listing::join_comma(e->manifest->attaches_to)
                                  << "\n";
                    }
                    if (!e->manifest->description.empty()) {
                        std::cout << "      " << e->manifest->description << "\n";
                    }
                }
                if (entries.empty()) {
                    std::cout << "  (no extensions found)\n";
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "cli_name\tname\tkind\tsource\tattaches_to\tdescription\n";
                for (const auto* e : entries) {
                    std::cout << e->manifest->effective_cli_name() << "\t"
                              << e->manifest->name << "\t"
                              << e->manifest->extension_kind << "\t"
                              << e->source_label << "\t"
                              << extension_listing::join_comma(e->manifest->attaches_to) << "\t"
                              << e->manifest->description << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto* e : entries) {
                    std::cout << "{\"cli_name\":\""
                              << extension_listing::json_escape(e->manifest->effective_cli_name())
                              << "\",\"name\":\""
                              << extension_listing::json_escape(e->manifest->name)
                              << "\",\"kind\":\""
                              << extension_listing::json_escape(e->manifest->extension_kind)
                              << "\",\"source\":\""
                              << extension_listing::json_escape(e->source_label)
                              << "\",\"attaches_to\":"
                              << extension_listing::json_string_array(e->manifest->attaches_to)
                              << ",\"description\":\""
                              << extension_listing::json_escape(e->manifest->description)
                              << "\"}\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// DescribeExtension subcommand - parameter detail for a single extension
// One manifest parameter as describe-extension's JSON forms give it: every
// property a configuration UI needs to build a field for it.
inline nlohmann::ordered_json parameter_schema_json(const beebium::ParameterSchema& p) {
    nlohmann::ordered_json choices = nlohmann::ordered_json::array();
    for (const auto& c : p.choices) {
        choices.push_back({{"value", c.value}, {"label", c.label}});
    }
    return {
        {"key", p.key},
        {"label", p.label},
        {"description", p.description},
        {"type", p.type},
        {"required", p.required},
        {"is_list", p.is_list},
        {"position", p.position >= 0 ? nlohmann::ordered_json(p.position)
                                     : nlohmann::ordered_json(nullptr)},
        {"default", p.default_value},
        {"min", p.min ? nlohmann::ordered_json(*p.min) : nlohmann::ordered_json(nullptr)},
        {"max", p.max ? nlohmann::ordered_json(*p.max) : nlohmann::ordered_json(nullptr)},
        {"choices", choices},
        {"choices_from", p.choices_from},
        {"excludes", p.excludes},
        {"bundled", p.bundled},
        {"unique", p.unique},
        {"provider_of", p.provider_of},
    };
}

// An extension as describe-extension --json gives it.
inline nlohmann::ordered_json extension_manifest_json(const beebium::ExtensionManifest& m,
                                                      const std::string& source) {
    nlohmann::ordered_json parameters = nlohmann::ordered_json::array();
    for (const auto& p : m.parameters) parameters.push_back(parameter_schema_json(p));
    nlohmann::ordered_json roms = nlohmann::ordered_json::array();
    for (const auto& r : m.roms) {
        roms.push_back({{"key", r.key}, {"filename", r.filename}, {"size", r.size},
                        {"display_name", r.display_name}});
    }
    return {
        {"name", m.name},
        {"display_name", m.display_name},
        {"description", m.description},
        {"cli_name", std::string(m.effective_cli_name())},
        {"kind", m.extension_kind},
        {"source", source},
        {"attaches_to", m.attaches_to},
        {"provides", m.provides},
        {"parameters", parameters},
        {"roms", roms},
    };
}

template<typename MachineType>
class DescribeExtensionSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-extension"; }
    std::string_view description() const override {
        return "Show parameter detail for a specific extension";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " describe-extension <name> [--extension-dir <path>]...\n"
                  << "\n"
                  << "Shows the parameter schema for an extension. <name> may be\n"
                  << "either the CLI flag stem (e.g. tube-65c02) or the canonical\n"
                  << "extension name (e.g. acorn-65c02-coprocessor).\n"
                  << "Honours --format pretty|tsv|jsonl (jsonl: one object per\n"
                  << "parameter). --json prints the whole extension as one object:\n"
                  << "name, display_name, description, cli_name, kind, source,\n"
                  << "attaches_to, provides, parameters (key, label, description,\n"
                  << "type, required, is_list, position, default, min, max, choices,\n"
                  << "bundled, unique, provider_of) and roms.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        auto parsed = extension_listing::parse_extension_dir_args(
            argc, argv, global.subcommand_argv_start);

        std::string target_name;
        bool json_output = false;
        for (const auto& tok : parsed.remaining) {
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            if (tok == "--json") {
                json_output = true;
                continue;
            }
            if (!tok.empty() && tok.front() == '-') {
                std::cerr << "Unknown argument: " << tok << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
            if (!target_name.empty()) {
                std::cerr << "Error: describe-extension takes a single name argument\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
            target_name = tok;
        }

        if (target_name.empty()) {
            std::cerr << "Error: describe-extension requires an extension name\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        if (auto err = populate_extension_resolver(argv[0], parsed.extension_dirpaths, config)) {
            return *err;
        }

        // Look up by cli_name first, then by canonical name.
        const beebium::ExtensionManifest* manifest = nullptr;
        std::string source_label;
        for (const auto& e : config.extension_resolver.entries()) {
            if (e.manifest->effective_cli_name() == target_name
                || e.manifest->name == target_name) {
                manifest = e.manifest;
                source_label = e.source_label;
                break;
            }
        }

        if (!manifest) {
            std::cerr << "Error: no extension named '" << target_name << "'\n";
            return ExitCode::CONFIG;
        }

        if (json_output) {
            std::cout << extension_manifest_json(*manifest, source_label).dump(2) << "\n";
            return ExitCode::OK;
        }

        OutputFormat format = resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty: {
                std::cout << "--" << manifest->effective_cli_name();
                if (manifest->name != manifest->effective_cli_name()) {
                    std::cout << " (" << manifest->name << ")";
                }
                std::cout << " [" << source_label << "]\n";
                if (!manifest->description.empty()) {
                    std::cout << "  " << manifest->description << "\n";
                }
                // Synthesised invocation form: the colon-separated key=value
                // syntax the generic extension arg parser accepts. Required
                // params are shown bare; optional ones are bracketed.
                std::cout << "  Usage: --" << manifest->effective_cli_name();
                if (!manifest->parameters.empty()) {
                    std::cout << " ";
                    bool first = true;
                    for (const auto& p : manifest->parameters) {
                        std::string token = p.key + "=<" + p.type + ">";
                        if (p.required) {
                            std::cout << (first ? "" : ":") << token;
                        } else {
                            std::cout << "[" << (first ? "" : ":") << token << "]";
                        }
                        first = false;
                    }
                    std::cout << "\n         (colon-separated key=value pairs)\n";
                } else {
                    std::cout << "\n";
                }
                std::cout << "  kind: " << manifest->extension_kind << "\n";
                if (!manifest->attaches_to.empty()) {
                    std::cout << "  attaches to:";
                    for (const auto& a : manifest->attaches_to) {
                        std::cout << " " << a << " (" << attachment_point_display_name(a) << ")";
                    }
                    std::cout << "\n";
                }
                if (!manifest->provides.empty()) {
                    std::cout << "  provides:";
                    for (const auto& p : manifest->provides) std::cout << " " << p;
                    std::cout << "\n";
                }
                if (manifest->parameters.empty()) {
                    std::cout << "  (no parameters)\n";
                } else {
                    std::cout << "  Parameters:\n";
                    for (const auto& p : manifest->parameters) {
                        std::cout << "    " << p.key
                                  << " : " << p.type;
                        if (p.is_list) std::cout << " (list)";
                        if (p.required) std::cout << " (required)";
                        if (p.min || p.max) {
                            std::cout << " (" << (p.min ? std::to_string(*p.min) : "")
                                      << ".." << (p.max ? std::to_string(*p.max) : "") << ")";
                        }
                        if (!p.excludes.empty()) {
                            std::cout << " (not with";
                            for (const auto& k : p.excludes) std::cout << " " << k;
                            std::cout << ")";
                        }
                        if (!p.choices.empty()) {
                            std::cout << " {";
                            for (std::size_t c = 0; c < p.choices.size(); ++c) {
                                std::cout << (c ? "|" : "") << p.choices[c].value;
                            }
                            std::cout << "}";
                        }
                        if (!p.default_value.empty()) {
                            std::cout << " [default: " << p.default_value << "]";
                        }
                        if (!p.bundled.empty()) {
                            std::cout << " (or a bundled " << p.bundled << " image)";
                        }
                        if (p.unique) std::cout << " (unique on its attachment)";
                        if (!p.provider_of.empty()) {
                            std::cout << " (names the " << p.provider_of << " provider)";
                        }
                        std::cout << "\n";
                        if (!p.label.empty() || !p.description.empty()) {
                            std::cout << "        " << p.label
                                      << (p.label.empty() || p.description.empty() ? "" : ": ")
                                      << p.description << "\n";
                        }
                    }
                }
                if (!manifest->roms.empty()) {
                    std::cout << "  ROMs:\n";
                    for (const auto& r : manifest->roms) {
                        std::cout << "    " << r.key << " : " << r.filename
                                  << " (" << r.size << " bytes)";
                        if (!r.display_name.empty()) {
                            std::cout << " -- " << r.display_name;
                        }
                        std::cout << "\n";
                    }
                }
                break;
            }
            case OutputFormat::Tsv:
                std::cout << "key\ttype\tis_list\trequired\tdefault\tdescription\n";
                for (const auto& p : manifest->parameters) {
                    std::cout << p.key << "\t" << p.type << "\t"
                              << (p.is_list ? "true" : "false") << "\t"
                              << (p.required ? "true" : "false") << "\t"
                              << p.default_value << "\t"
                              << p.description << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& p : manifest->parameters) {
                    std::cout << parameter_schema_json(p).dump() << "\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// ListAttachmentPoints subcommand - the catalogue of points an extension can
// attach to, with human-readable names. Lets a configuration UI ask, for each
// point, "which of these <point> extensions would you like to load?".
template<typename MachineType>
class ListAttachmentPointsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-attachment-points"; }
    std::string_view description() const override {
        return "List the attachment points extensions can plug into";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-attachment-points\n"
                  << "\n"
                  << "Lists the attachment points an extension can declare via attaches_to\n"
                  << "(serial-port, user-port, 1mhz-bus, tube, scsi), each with a display\n"
                  << "name and its occupancy: 'single' (a connector that holds at most one\n"
                  << "extension) or 'multi' (a bus that several may share). Pair with\n"
                  << "`list-extensions --attaches-to <point>` to see what plugs in where.\n"
                  << "Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view tok = argv[i];
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << tok << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        OutputFormat format = resolve_output_format(global.output_format);
        const auto& points = attachment_point_catalogue();

        switch (format) {
            case OutputFormat::Pretty:
                std::cout << "Attachment points:\n";
                for (const auto& p : points) {
                    std::cout << "  " << p.id << " (" << p.display_name << ") ["
                              << occupancy_label(p) << "]\n";
                    if (!p.description.empty()) {
                        std::cout << "      " << p.description << "\n";
                    }
                }
                break;

            case OutputFormat::Tsv:
                std::cout << "id\tdisplay_name\tmin_occupancy\tmax_occupancy\tdescription\n";
                for (const auto& p : points) {
                    std::cout << p.id << "\t" << p.display_name << "\t"
                              << p.min_occupancy << "\t"
                              << (p.max_occupancy ? std::to_string(*p.max_occupancy) : "")
                              << "\t" << p.description << "\n";
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto& p : points) {
                    std::cout << "{\"id\":\"" << extension_listing::json_escape(p.id)
                              << "\",\"display_name\":\""
                              << extension_listing::json_escape(p.display_name)
                              << "\",\"min_occupancy\":" << p.min_occupancy
                              << ",\"max_occupancy\":"
                              << (p.max_occupancy ? std::to_string(*p.max_occupancy) : "null")
                              << ",\"description\":\""
                              << extension_listing::json_escape(p.description)
                              << "\"}\n";
                }
                break;

            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// ListNamePlaceholders subcommand: the placeholders a machine-name template
// can use (#153), for people writing presets and launch commands. Lists the
// core's placeholders without launching a machine, so only values fixed
// before launch are shown; a running server's ListNamePlaceholders RPC lists
// every placeholder, extensions' included, with live values.
template<typename MachineType>
class ListNamePlaceholdersSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-name-placeholders"; }
    std::string_view description() const override {
        return "List the placeholders a machine-name template can use";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-name-placeholders\n"
                  << "\n"
                  << "Lists the placeholders a machine-name template (--machine-name, a\n"
                  << "preset's machine_name) can use: each placeholder's key, written\n"
                  << "{key} in a template, its label, group and description, and its\n"
                  << "value where it is known before launch. 'Station {econet-station}'\n"
                  << "renders as 'Station 80' on station 80, and follows a renumber at\n"
                  << "the next Break. {{ and }} are literal braces; an unknown key\n"
                  << "renders as written. Extensions can add placeholders; a running\n"
                  << "server lists them all. Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view tok = argv[i];
            if (tok == "--help" || tok == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << tok << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        using Memory = typename MachineType::Memory;
        std::vector<beebium::NamePlaceholderInfo> infos =
            beebium::service::machine_name_placeholder_infos();
        for (auto& info : beebium::service::econet_name_placeholder_infos()) {
            infos.push_back(std::move(info));
        }
        for (auto& info : beebium::service::storage_name_placeholder_infos()) {
            infos.push_back(std::move(info));
        }
        for (auto& info : beebium::service::coprocessor_name_placeholder_infos()) {
            infos.push_back(std::move(info));
        }
        // The one value fixed before launch.
        auto known_value = [](const std::string& key) -> std::optional<std::string> {
            if (key == "machine-model") return std::string(Memory::MACHINE_DISPLAY_NAME);
            return std::nullopt;
        };

        OutputFormat format = resolve_output_format(global.output_format);
        switch (format) {
            case OutputFormat::Pretty: {
                std::string group;
                for (const auto& info : infos) {
                    if (info.group != group) {
                        group = info.group;
                        std::cout << group << ":\n";
                    }
                    std::cout << "  {" << info.key << "}  " << info.label;
                    if (auto value = known_value(info.key)) {
                        std::cout << " = " << *value;
                    }
                    std::cout << "\n      " << info.description << "\n";
                }
                break;
            }
            case OutputFormat::Tsv:
                std::cout << "key\tlabel\tgroup\tvalue\tdescription\n";
                for (const auto& info : infos) {
                    std::cout << info.key << "\t" << info.label << "\t" << info.group << "\t"
                              << known_value(info.key).value_or("") << "\t"
                              << info.description << "\n";
                }
                break;
            case OutputFormat::Jsonl:
                for (const auto& info : infos) {
                    auto value = known_value(info.key);
                    std::cout << "{\"key\":\"" << extension_listing::json_escape(info.key)
                              << "\",\"label\":\"" << extension_listing::json_escape(info.label)
                              << "\",\"group\":\"" << extension_listing::json_escape(info.group)
                              << "\",\"insertion\":\"{"
                              << extension_listing::json_escape(info.key)
                              << "}\",\"value\":"
                              << (value ? "\"" + extension_listing::json_escape(*value) + "\""
                                        : std::string("null"))
                              << ",\"description\":\""
                              << extension_listing::json_escape(info.description) << "\"}\n";
                }
                break;
            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// DescribePresetSchema subcommand - output preset configuration schema as JSON
template<typename MachineType>
class DescribePresetSchemaSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-preset-schema"; }
    std::string_view description() const override { return "Output preset configuration schema as JSON"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-preset-schema\n"
                  << "\n"
                  << "Outputs JSON describing what a preset for this model may contain, so a\n"
                  << "front-end can build a configuration UI before any machine is launched.\n"
                  << "\n"
                  << "Output includes:\n"
                  << "  - schema_version: Schema format version\n"
                  << "  - model: Machine identification (id, name, description)\n"
                  << "  - sections: one per preset key this model accepts, its 'type' being\n"
                  << "    that key: speed, startup, mos_image_uri, storage, sideways_bank,\n"
                  << "    econet, extensions\n"
                  << "\n"
                  << "The extensions section's catalogue describes each extension this\n"
                  << "model can take as describe-extension --json does; list-fdcs and\n"
                  << "list-floppy-formats give the controller and image catalogues.\n"
                  << "resolve-preset shows what a given preset resolves to.\n"
                  << "\n"
                  << "Output is always JSON regardless of --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help for this subcommand
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            // Unknown argument
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Build JSON output using ordered_json to preserve key insertion order
        // (standard json sorts keys alphabetically, making output harder to read)
        using ojson = nlohmann::ordered_json;

        ojson output;
        output["schema_version"] = 2;
        output["model"] = ojson{
            {"id", Memory::MACHINE_TYPE},
            {"name", Memory::MACHINE_DISPLAY_NAME},
            {"description", Memory::MACHINE_DESCRIPTION}
        };

        // Build storage section - "type" first for readability
        ojson storage;
        storage["type"] = "storage";

        // Built-in hardware - all current models have cassette
        storage["builtin"]["cassette"] = true;

        if constexpr (HasDiscControllerSocket<Memory>) {
            // Model B: socketed FDC, no built-in controller
            storage["builtin"]["fdc"] = nullptr;

            // List available FDC options for the socket - "id" first in each option
            ojson fdc_options = ojson::array();
            fdc_options.push_back(ojson{
                {"id", "none"},
                {"label", "Empty"},
                {"device", nullptr},
                {"description", "No disc controller"}
            });
            for (const auto& info : DiscControllerRegistry::available()) {
                fdc_options.push_back(ojson{
                    {"id", info.id},
                    {"label", info.display_name},
                    {"device", info.fdc_chip},
                    {"description", info.description}
                });
            }
            storage["fdc_socket"]["options"] = fdc_options;
        } else if constexpr (HasDiscDrives<Memory>) {
            // Model B+: built-in FDC, no socket
            storage["builtin"]["fdc"] = ojson{
                {"device", "WD1770"},
                {"label", "Built-in WD1770"}
            };
            // No fdc_socket key for machines with built-in FDC
        }

        // Floppy drives (if hardware has them) - "drive" first in each entry
        if constexpr (HasDiscDrives<Memory>) {
            // Build image_types from the format registry so new formats appear automatically
            ojson image_types = ojson::array();
            for (const auto& handler : default_format_registry().handlers()) {
                for (auto ext : handler->file_extensions()) {
                    // Strip the leading dot for the schema
                    std::string_view ext_no_dot = ext.substr(1);
                    image_types.push_back(std::string(ext_no_dot));
                }
            }
            storage["floppy_drives"] = ojson::array({
                ojson{{"drive", 0}, {"tracks", {40, 80}}, {"sides", 2}, {"image_types", image_types}},
                ojson{{"drive", 1}, {"tracks", {40, 80}}, {"sides", 2}, {"image_types", image_types}}
            });
        }

        ServerConfig<MachineType> resolver_cfg;
        if (auto err = populate_extension_resolver(argv[0], {}, resolver_cfg)) {
            return *err;
        }
        const auto& extension_entries = resolver_cfg.extension_resolver.entries();

        // Each section, by the preset key it describes; written out below in
        // the registry's order.
        std::map<PresetKey, ojson> section_for;

        section_for[PresetKey::Speed] = ojson{
            {"type", "speed"},
            {"default", 1.0},
            {"unlimited", "unlimited"},
            {"description", "Emulation speed as a positive multiple of real time, or "
                            "\"unlimited\" to run flat out"}};

        section_for[PresetKey::Startup] = ojson{
            {"type", "startup"},
            {"screen_mode", ojson{{"min", 0}, {"max", 7}, {"default", 7}}},
            {"auto_boot", ojson{{"default", false}}},
            {"links", ojson{{"min", 0}, {"max", 255},
                            {"excludes", ojson::array({"screen_mode", "auto_boot"})}}},
            {"description", "The startup keyboard links: screen_mode and auto_boot, or the "
                            "whole links byte"}};

        section_for[PresetKey::MosImageUri] = ojson{
            {"type", "mos_image_uri"},
            {"default", Memory::DEFAULT_MOS_ROM},
            {"description", "The MOS ROM: a ROM name (found on the ROM search path) or a path"}};

        section_for[PresetKey::Storage] = storage;

        if constexpr (HasEconetSocket<Memory>) {
            const beebium::econet::StationRange auto_range{};
            ojson transports = ojson::array();
            for (const auto& e : extension_entries) {
                if (e.manifest->extension_kind == "econet-transport") {
                    transports.push_back(e.manifest->name);
                }
            }
            section_for[PresetKey::Econet] = ojson{
                {"type", "econet"},
                {"station", ojson{{"min", 1},
                                  {"max", 254},
                                  {"auto", true},
                                  {"auto_default_range",
                                   ojson::array({auto_range.lo, auto_range.hi})}}},
                {"transports", transports}};
        }

        // Sideways ROM/RAM bank section, derived from the machine's slot
        // topology so frontends can render a slot grid (the Memory tab) and
        // show what each socket can hold. A catalogue of known ROMs and
        // categories is deferred until a ROM library exists; for now we expose
        // the topology and the machine's own default ROM placements.
        if constexpr (requires { Memory::slot_topology(typename Memory::MotherboardLinks{}); }) {
            auto topo = Memory::slot_topology(typename Memory::MotherboardLinks{});

            ojson sideways;
            sideways["type"] = "sideways_bank";
            sideways["has_aliasing"] = topo.has_aliasing;

            ojson sockets = ojson::array();
            for (const auto& sock : topo.sockets) {
                ojson capabilities = ojson::array();
                if (sock.supports_rom) capabilities.push_back("rom");
                if (sock.supports_ram) capabilities.push_back("ram");
                if (sock.supports_empty) capabilities.push_back("empty");
                if (sock.supports_write_protect) capabilities.push_back("write_protect");

                sockets.push_back(ojson{
                    {"label", sock.label},
                    {"slots", sock.slots},
                    {"capabilities", capabilities},
                    {"runtime_configurable", sock.runtime_configurable}
                });
            }
            sideways["sockets"] = sockets;

            // Default ROM placements the machine applies unless a preset or
            // --sideways overrides them, so the frontend can show what is
            // already in a slot before the user changes anything.
            ojson default_roms = ojson::array();
            if constexpr (requires { Memory::DEFAULT_LANGUAGE_ROM; }) {
                default_roms.push_back(ojson{
                    {"slot", Memory::DEFAULT_LANGUAGE_SLOT},
                    {"image", Memory::DEFAULT_LANGUAGE_ROM},
                    {"role", "language"}
                });
            }
            if constexpr (requires { Memory::DEFAULT_DFS_ROM; }) {
                default_roms.push_back(ojson{
                    {"slot", Memory::DEFAULT_DFS_SLOT},
                    {"image", Memory::DEFAULT_DFS_ROM},
                    {"role", "filing"}
                });
            }
            if constexpr (requires { Memory::DEFAULT_BOARD_ROM; }) {
                default_roms.push_back(ojson{
                    {"slot", Memory::DEFAULT_BOARD_ROM_SLOT},
                    {"image", Memory::DEFAULT_BOARD_ROM},
                    {"role", "board"}
                });
            }
            sideways["default_roms"] = default_roms;

            section_for[PresetKey::SidewaysBank] = sideways;
        }

        // The attachment points a preset's extensions may use on this model:
        // the machine's own connectors, then any point an extension provides
        // (a SCSI bus from a host adapter), each with the extensions that
        // attach to it; and the catalogue describing each of those
        // extensions, and each Econet transport, as describe-extension --json
        // does.
        {
            const auto machine_points = machine_attachment_points<Memory>();

            std::vector<std::pair<std::string, std::vector<std::string>>> points;
            for (const auto& id : machine_points) points.push_back({id, {}});
            for (const auto& e : extension_entries) {
                for (const auto& provided : e.manifest->provides) {
                    auto it = std::find_if(points.begin(), points.end(),
                                           [&](const auto& p) { return p.first == provided; });
                    if (it == points.end()) {
                        points.push_back({provided, {e.manifest->name}});
                    } else if (std::find(machine_points.begin(), machine_points.end(),
                                         provided) == machine_points.end()) {
                        it->second.push_back(e.manifest->name);
                    }
                }
            }
            ojson attachment_points = ojson::array();
            for (const auto& [id, providers] : points) {
                ojson attaching = ojson::array();
                for (const auto& e : extension_entries) {
                    const auto& at = e.manifest->attaches_to;
                    if (std::find(at.begin(), at.end(), id) != at.end()) {
                        attaching.push_back(e.manifest->name);
                    }
                }
                const auto* catalogued = find_attachment_point(id);
                ojson max_occupancy = nullptr;
                if (catalogued && catalogued->max_occupancy) {
                    max_occupancy = *catalogued->max_occupancy;
                }
                attachment_points.push_back(ojson{
                    {"id", id},
                    {"display_name", std::string(attachment_point_display_name(id))},
                    {"description", catalogued ? catalogued->description : std::string()},
                    {"min_occupancy", catalogued ? catalogued->min_occupancy : 0},
                    {"max_occupancy", max_occupancy},
                    {"provided_by", providers.empty() ? ojson(nullptr) : ojson(providers)},
                    {"extensions", attaching}});
            }
            ojson catalogue = ojson::array();
            for (const auto& e : extension_entries) {
                const auto& at = e.manifest->attaches_to;
                const bool attachable =
                    std::any_of(at.begin(), at.end(), [&](const std::string& point) {
                        return std::any_of(points.begin(), points.end(),
                                           [&](const auto& p) { return p.first == point; });
                    });
                bool transport = false;
                if constexpr (HasEconetSocket<Memory>) {
                    transport = e.manifest->extension_kind == "econet-transport";
                }
                if (attachable || transport) {
                    catalogue.push_back(extension_manifest_json(*e.manifest, e.source_label));
                }
            }
            // The images a parameter declared bundled may name by bare name,
            // by kind (bundled_images): the hard-disc masters the server ships,
            // and each plugin's own ROMs. Floppy discs have none.
            ojson by_kind{{"hard-disc", ojson::array()}, {"rom", ojson::array()}};
            for (const auto& image : bundled_images(resolver_cfg.extension_resolver)) {
                by_kind[image.kind].push_back(ojson{
                    {"image", image.image},
                    {"display_name", image.display_name},
                    {"size_bytes", image.size_bytes},
                    {"extension", image.extension.empty() ? ojson(nullptr)
                                                          : ojson(image.extension)}});
            }
            section_for[PresetKey::Extensions] = ojson{{"type", "extensions"},
                                                       {"attachment_points", attachment_points},
                                                       {"catalogue", catalogue},
                                                       {"bundled_images", by_kind}};
        }

        ojson sections = ojson::array();
        for (const auto& info : kPresetKeys) {
            if (!describes(info.key)) continue;
            if (auto it = section_for.find(info.key); it != section_for.end()) {
                sections.push_back(std::move(it->second));
            }
        }
        output["sections"] = sections;

        std::cout << output.dump(2) << "\n";
        return ExitCode::OK;
    }

private:
    // Whether a preset key has a section here: the machine settings do, the
    // preset's own descriptive keys do not.
    static bool describes(PresetKey key) {
        switch (key) {
            case PresetKey::Speed:
            case PresetKey::Startup:
            case PresetKey::MosImageUri:
            case PresetKey::Storage:
            case PresetKey::SidewaysBank:
            case PresetKey::Econet:
            case PresetKey::Extensions:
                return true;
            case PresetKey::Model:
            case PresetKey::Name:
            case PresetKey::ReleaseDate:
            case PresetKey::Description:
            case PresetKey::MachineName:
            case PresetKey::ThumbnailCaptureDelaySeconds:
            case PresetKey::AutoBoot:
                return false;
        }
        return false;
    }
};

// ListPresets subcommand - list available presets for this model
template<typename MachineType>
class ListPresetsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-presets"; }
    std::string_view description() const override { return "List available presets for this model"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " [--format pretty|tsv|jsonl] list-presets [--json]\n"
                  << "\n"
                  << "Lists presets compatible with this machine model. The output format\n"
                  << "follows the global --format option (pretty on a terminal, tsv when\n"
                  << "piped); rows carry the preset id, name and source (system or user).\n"
                  << "\n"
                  << "Options:\n"
                  << "  --json    Same as --format jsonl\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        bool json_output = false;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--json") {
                json_output = true;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        // Get model ID for this executable
        std::string_view model_id = Memory::MACHINE_TYPE;

        // Collect presets from system and user directories
        struct PresetEntry {
            std::string id;
            std::string name;
            std::string source;  // "system" or "user"
        };
        std::vector<PresetEntry> system_presets;
        std::vector<PresetEntry> user_presets;

        // List system presets
        if (auto system_dir = PresetPaths::get_system_presets_dirpath()) {
            for (const auto& preset_id : PresetPaths::list_presets_in_directory(*system_dir)) {
                auto filepath = *system_dir / (preset_id + std::string(PresetPaths::PRESET_EXTENSION));
                auto result = load_preset(filepath);
                if (result) {
                    // Filter by model
                    if (result.config->model && *result.config->model != model_id) {
                        continue;  // Skip presets for other models
                    }
                    system_presets.push_back({preset_id, result.config->name, "system"});
                }
            }
        }

        // List user presets
        auto user_dir = PresetPaths::get_user_presets_dirpath();
        if (std::filesystem::is_directory(user_dir)) {
            for (const auto& preset_id : PresetPaths::list_presets_in_directory(user_dir)) {
                auto filepath = user_dir / (preset_id + std::string(PresetPaths::PRESET_EXTENSION));
                auto result = load_preset(filepath);
                if (result) {
                    // Filter by model
                    if (result.config->model && *result.config->model != model_id) {
                        continue;  // Skip presets for other models
                    }
                    user_presets.push_back({preset_id, result.config->name, "user"});
                }
            }
        }

        OutputFormat format = json_output ? OutputFormat::Jsonl
                                          : resolve_output_format(global.output_format);

        switch (format) {
            case OutputFormat::Pretty: {
                // The id column is as wide as the longest id plus a two-space gap.
                size_t id_width = 0;
                for (const auto* presets : {&system_presets, &user_presets}) {
                    for (const auto& p : *presets) {
                        id_width = std::max(id_width, p.id.size());
                    }
                }
                id_width += 2;
                auto print_section = [&](std::string_view heading,
                                         const std::vector<PresetEntry>& presets) {
                    std::cout << heading << "\n";
                    if (presets.empty()) {
                        std::cout << "  (none)\n";
                        return;
                    }
                    for (const auto& p : presets) {
                        std::cout << "  " << p.id << std::string(id_width - p.id.size(), ' ')
                                  << p.name << "\n";
                    }
                };
                print_section("Built-in presets:", system_presets);
                std::cout << "\n";
                print_section("User presets:", user_presets);
                break;
            }

            case OutputFormat::Tsv:
                std::cout << "id\tname\tsource\n";
                for (const auto* presets : {&system_presets, &user_presets}) {
                    for (const auto& p : *presets) {
                        std::cout << p.id << "\t" << p.name << "\t" << p.source << "\n";
                    }
                }
                break;

            case OutputFormat::Jsonl:
                for (const auto* presets : {&system_presets, &user_presets}) {
                    for (const auto& p : *presets) {
                        nlohmann::json row{{"id", p.id}, {"name", p.name}, {"source", p.source}};
                        std::cout << row.dump() << "\n";
                    }
                }
                break;

            case OutputFormat::Auto:
                // Should not reach here after resolve_output_format
                break;
        }

        return ExitCode::OK;
    }
};

// ShowPreset subcommand - output preset file contents
template<typename MachineType>
class ShowPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "show-preset"; }
    std::string_view description() const override { return "Output preset file contents"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " show-preset <id>\n"
                  << "\n"
                  << "Outputs the JSON contents of the specified preset file.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto filepath = PresetPaths::find_preset_filepath(preset_id);
        if (!filepath) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        // Read and output file contents
        std::ifstream file(*filepath);
        if (!file) {
            std::cerr << "Error: cannot read preset file: " << filepath->string() << "\n";
            return ExitCode::NOINPUT;
        }

        std::cout << file.rdbuf();
        return ExitCode::OK;
    }
};

// ReportPresetsDirpath subcommand - report user presets directory
template<typename MachineType>
class ReportPresetsDirpathSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "report-presets-dirpath"; }
    std::string_view description() const override { return "Report user presets directory path"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " report-presets-dirpath\n"
                  << "\n"
                  << "Outputs the directory path where user presets are stored.\n"
                  << "This path can be overridden with the BEEBIUM_USER_PRESETS_DIRPATH\n"
                  << "environment variable.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Check for subcommand-specific --help
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        std::cout << PresetPaths::get_user_presets_dirpath().string() << "\n";
        return ExitCode::OK;
    }
};

// ---- AUN map file subcommands ----
//
// Edit the per-user aun-map.json through the same library the AUN transport
// uses at runtime. Model-independent: pure file work, no machine is built.

struct AunMapArgs {
    std::vector<std::string> positional;
    std::filesystem::path filepath;
    std::string label;
    bool help = false;
    std::string unknown_flag;  // first unrecognised --flag, if any
};

inline AunMapArgs parse_aun_map_args(int argc, char* argv[],
                                     const GlobalConfig& global) {
    AunMapArgs out;
    bool have_mapfile = false;
    for (int i = global.subcommand_argv_start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            out.help = true;
        } else if (arg == "--map-file" && i + 1 < argc) {
            out.filepath = argv[++i];
            have_mapfile = true;
        } else if (arg.rfind("--map-file=", 0) == 0) {
            out.filepath = std::string(arg.substr(11));
            have_mapfile = true;
        } else if (arg == "--label" && i + 1 < argc) {
            out.label = argv[++i];
        } else if (arg.rfind("--label=", 0) == 0) {
            out.label = std::string(arg.substr(8));
        } else if (!arg.empty() && arg.front() == '-') {
            if (out.unknown_flag.empty()) out.unknown_flag = std::string(arg);
        } else {
            out.positional.push_back(std::string(arg));
        }
    }
    if (!have_mapfile) {
        if (auto env = beebium::platform::get_env("BEEBIUM_AUN_MAP_FILEPATH");
            env && !env->empty()) {
            out.filepath = std::filesystem::path(*env);
        } else {
            out.filepath =
                beebium::platform::user_state_base_dirpath() / "aun-map.json";
        }
    }
    return out;
}

// Parse "net.stn" (e.g. "0.254"): net 0..255, stn 1..254.
inline bool parse_aun_net_stn(const std::string& text, int& net, int& stn) {
    auto dot = text.find('.');
    if (dot == std::string::npos) return false;
    try {
        std::size_t net_len = 0, stn_len = 0;
        net = std::stoi(text.substr(0, dot), &net_len);
        stn = std::stoi(text.substr(dot + 1), &stn_len);
        if (net_len != dot || stn_len != text.size() - dot - 1) return false;
    } catch (...) {
        return false;
    }
    return net >= 0 && net <= 255 && stn >= 1 && stn <= 254;
}

// Load the document for an edit, printing a position-bearing error and setting
// `exit_code` if the file is present but malformed.
inline std::optional<beebium::AunMapDocument> open_aun_map_for_edit(
        const std::filesystem::path& filepath, int& exit_code) {
    auto load = beebium::AunMapDocument::load(filepath);
    if (!load.document.has_value()) {
        std::cerr << load.error << "\n";
        exit_code = ExitCode::DATAERR;
        return std::nullopt;
    }
    return std::move(load.document);
}

template<typename MachineType>
class ReportAunMapFilepathSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "report-aun-map-filepath"; }
    std::string_view description() const override {
        return "Report the AUN map file path";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " report-aun-map-filepath [--map-file <path>]\n\n"
                  << "Outputs the per-user aun-map.json path, after the "
                     "--map-file option and the\nBEEBIUM_AUN_MAP_FILEPATH "
                     "environment variable override.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        std::cout << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class CreateAunMapSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-aun-map"; }
    std::string_view description() const override {
        return "Create a template AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " create-aun-map [--map-file <path>]\n\n"
                  << "Writes a template aun-map.json with one example peer and "
                     "one example subnet,\nwhose labels explain the "
                     "PiEconetBridge and RISC OS cases. Refuses to overwrite an\n"
                     "existing file.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        std::error_code ec;
        if (std::filesystem::exists(args.filepath, ec)) {
            std::cerr << "Refusing to overwrite existing file: "
                      << args.filepath.string() << "\n";
            return ExitCode::CONFIG;
        }
        auto doc = beebium::AunMapDocument::empty();
        doc.add_or_replace_peer(
            0, 254, "192.168.1.10", 32768,
            "PiEconetBridge: file server 1.254 exposed as 0.254");
        doc.add_or_replace_subnet(
            128, "192.168.1.0/24",
            "RISC OS convention: net 128 is this /24, station = last octet, "
            "port 32768");
        if (std::string err = doc.save(args.filepath); !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::IOERR;
        }
        std::cout << "Created " << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class ShowAunMapSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "show-aun-map"; }
    std::string_view description() const override {
        return "Show the AUN map file's entries";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " [--format pretty|tsv|jsonl] show-aun-map [--map-file <path>]\n\n"
                  << "Shows the parsed peers and subnets with labels and host "
                     "resolution.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        auto loaded = beebium::load_aun_map(args.filepath);
        if (!loaded.map.has_value()) {
            std::cerr << loaded.error << "\n";
            return ExitCode::DATAERR;
        }
        OutputFormat format = resolve_output_format(global.output_format);
        const auto& map = *loaded.map;
        if (format == OutputFormat::Jsonl) {
            for (const auto& p : map.peers) {
                auto ip = beebium::resolve_map_host(p.host);
                nlohmann::json row{{"kind", "peer"}, {"net", p.net},
                                   {"station", p.stn}, {"host", p.host},
                                   {"port", p.port}, {"label", p.label},
                                   {"resolved", ip.has_value()},
                                   {"resolved_ip", ip.value_or("")}};
                std::cout << row.dump() << "\n";
            }
            for (const auto& s : map.subnets) {
                nlohmann::json row{{"kind", "subnet"}, {"net", s.net},
                                   {"subnet", s.subnet_text}, {"label", s.label}};
                std::cout << row.dump() << "\n";
            }
            return ExitCode::OK;
        }
        if (format == OutputFormat::Tsv) {
            std::cout << "kind\tnet\tstn\thost_or_subnet\tport\tlabel\tresolved\n";
            for (const auto& p : map.peers) {
                auto ip = beebium::resolve_map_host(p.host);
                std::cout << "peer\t" << int(p.net) << "\t" << int(p.stn) << "\t"
                          << p.host << "\t" << p.port << "\t" << p.label << "\t"
                          << (ip ? *ip : std::string("unresolved")) << "\n";
            }
            for (const auto& s : map.subnets) {
                std::cout << "subnet\t" << int(s.net) << "\t\t" << s.subnet_text
                          << "\t\t" << s.label << "\t\n";
            }
            return ExitCode::OK;
        }
        // Pretty.
        std::cout << "Map file: " << args.filepath.string() << "\n";
        std::cout << "Peers:\n";
        if (map.peers.empty()) std::cout << "  (none)\n";
        for (const auto& p : map.peers) {
            auto ip = beebium::resolve_map_host(p.host);
            std::cout << "  " << int(p.net) << "." << int(p.stn) << "  " << p.host
                      << ":" << p.port
                      << (ip ? ("  [" + *ip + "]") : std::string("  [unresolved]"));
            if (!p.label.empty()) std::cout << "  " << p.label;
            std::cout << "\n";
        }
        std::cout << "Subnets:\n";
        if (map.subnets.empty()) std::cout << "  (none)\n";
        for (const auto& s : map.subnets) {
            std::cout << "  net " << int(s.net) << " = " << s.subnet_text;
            if (!s.label.empty()) std::cout << "  " << s.label;
            std::cout << "\n";
        }
        auto doc = beebium::AunMapDocument::load(args.filepath);
        if (doc.document.has_value()) {
            auto unknown = doc.document->unknown_top_level_keys();
            if (!unknown.empty()) {
                std::cout << "Unknown keys preserved:";
                for (const auto& key : unknown) std::cout << " " << key;
                std::cout << "\n";
            }
        }
        return ExitCode::OK;
    }
};

template<typename MachineType>
class AddAunPeerSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "add-aun-peer"; }
    std::string_view description() const override {
        return "Add or replace a peer in the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " add-aun-peer <net.stn> <host> <port> [--label <text>] "
                     "[--map-file <path>]\n\n"
                  << "Adds or replaces the peers[] entry for net.stn.\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 3) {
            std::cerr << "Expected <net.stn> <host> <port>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0, stn = 0;
        if (!parse_aun_net_stn(args.positional[0], net, stn)) {
            std::cerr << "Invalid net.stn '" << args.positional[0]
                      << "' (net 0-255, station 1-254)\n";
            return ExitCode::USAGE;
        }
        int port = 0;
        try {
            std::size_t len = 0;
            port = std::stoi(args.positional[2], &len);
            if (len != args.positional[2].size() || port < 1 || port > 65535) {
                throw std::invalid_argument("");
            }
        } catch (...) {
            std::cerr << "Invalid port '" << args.positional[2]
                      << "' (1-65535)\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        if (std::string err = doc->add_or_replace_peer(
                static_cast<std::uint8_t>(net), static_cast<std::uint8_t>(stn),
                args.positional[1], static_cast<std::uint16_t>(port), args.label);
            !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::USAGE;
        }
        if (std::string err = doc->save(args.filepath); !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::IOERR;
        }
        std::cout << "Wrote " << net << "." << stn << " to "
                  << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class RemoveAunPeerSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "remove-aun-peer"; }
    std::string_view description() const override {
        return "Remove a peer from the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " remove-aun-peer <net.stn> [--map-file <path>]\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 1) {
            std::cerr << "Expected <net.stn>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0, stn = 0;
        if (!parse_aun_net_stn(args.positional[0], net, stn)) {
            std::cerr << "Invalid net.stn '" << args.positional[0] << "'\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        bool removed = doc->remove_peer(static_cast<std::uint8_t>(net),
                                        static_cast<std::uint8_t>(stn));
        if (removed) {
            if (std::string err = doc->save(args.filepath); !err.empty()) {
                std::cerr << err << "\n";
                return ExitCode::IOERR;
            }
            std::cout << "Removed " << net << "." << stn << "\n";
        } else {
            std::cout << "No entry for " << net << "." << stn << "\n";
        }
        return ExitCode::OK;
    }
};

template<typename MachineType>
class AddAunSubnetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "add-aun-subnet"; }
    std::string_view description() const override {
        return "Add or replace a subnet rule in the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " add-aun-subnet <net> <a.b.c.0/24> [--label <text>] "
                     "[--map-file <path>]\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 2) {
            std::cerr << "Expected <net> <a.b.c.0/24>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0;
        try {
            std::size_t len = 0;
            net = std::stoi(args.positional[0], &len);
            if (len != args.positional[0].size() || net < 0 || net > 255) {
                throw std::invalid_argument("");
            }
        } catch (...) {
            std::cerr << "Invalid net '" << args.positional[0] << "' (0-255)\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        if (std::string err = doc->add_or_replace_subnet(
                static_cast<std::uint8_t>(net), args.positional[1], args.label);
            !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::USAGE;
        }
        if (std::string err = doc->save(args.filepath); !err.empty()) {
            std::cerr << err << "\n";
            return ExitCode::IOERR;
        }
        std::cout << "Wrote subnet net " << net << " to "
                  << args.filepath.string() << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class RemoveAunSubnetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "remove-aun-subnet"; }
    std::string_view description() const override {
        return "Remove a subnet rule from the AUN map file";
    }
    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " remove-aun-subnet <net> [--map-file <path>]\n";
    }
    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        auto args = parse_aun_map_args(argc, argv, global);
        if (global.help_requested || args.help) { help(argv[0]); return ExitCode::OK; }
        if (!args.unknown_flag.empty()) {
            std::cerr << "Unknown argument: " << args.unknown_flag << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (args.positional.size() != 1) {
            std::cerr << "Expected <net>\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        int net = 0;
        try {
            std::size_t len = 0;
            net = std::stoi(args.positional[0], &len);
            if (len != args.positional[0].size() || net < 0 || net > 255) {
                throw std::invalid_argument("");
            }
        } catch (...) {
            std::cerr << "Invalid net '" << args.positional[0] << "'\n";
            return ExitCode::USAGE;
        }
        int exit_code = ExitCode::OK;
        auto doc = open_aun_map_for_edit(args.filepath, exit_code);
        if (!doc) return exit_code;
        bool removed = doc->remove_subnet(static_cast<std::uint8_t>(net));
        if (removed) {
            if (std::string err = doc->save(args.filepath); !err.empty()) {
                std::cerr << err << "\n";
                return ExitCode::IOERR;
            }
            std::cout << "Removed subnet net " << net << "\n";
        } else {
            std::cout << "No subnet for net " << net << "\n";
        }
        return ExitCode::OK;
    }
};

// CreatePreset subcommand - create a new preset
template<typename MachineType>
class CreatePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "create-preset"; }
    std::string_view description() const override { return "Create a new preset"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " create-preset --name <name> [options]\n"
                  << "\n"
                  << "Creates a new preset for this machine model, recording the machine the\n"
                  << "options describe. The options are parsed exactly as 'start' parses them,\n"
                  << "and only what is given is recorded: a default stays a default.\n"
                  << "\n"
                  << "Preset options:\n"
                  << "  --name <name>         Display name for the preset (required)\n"
                  << "  --description <text>  Short one-line description shown in the picker\n"
                  << "  --release-date <date> Release date (YYYY, YYYY-MM, or YYYY-MM-DD)\n"
                  << "  --from <id|filepath>  Source preset: the baseline the machine options\n"
                  << "                        override, as 'start --preset' does\n"
                  << "  --output <path>       Write to this path instead of the user directory\n"
                  << "\n"
                  << "Machine options, as for 'start' (see 'start --help'):\n"
                  << "  --machine-name, --speed, --screen-mode, --auto-boot, --links, --mos,\n"
                  << "  --language-rom (recorded as the model's language slot), --fdc,\n"
                  << "  --floppy (a relative path is taken from the working directory),\n"
                  << "  --sideways, --station, and --<extension> [k=v:...] for any extension\n"
                  << "  (an Econet transport such as --aun is recorded as the station's).\n"
                  << "Options that configure the launch rather than the machine (--port,\n"
                  << "--wait, --advertise, ...) and those with no preset form\n"
                  << "(--write-protect, --hide, --motherboard-link, a board RTC) are refused.\n"
                  << "\n"
                  << "The preset ID is derived by slugifying the name.\n"
                  << "Outputs the created preset ID (or path if --output used).\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Separate the preset's own options from the machine options, which
        // go to parse_start_arguments exactly as `start` would receive them
        // (with --from as its --preset).
        std::string preset_name;
        std::string preset_description;
        std::string release_date;
        std::string output_filepath;
        std::vector<std::string> machine_args{argv[0]};
        const std::set<std::string> launch_only{
            "--port", "--wait", "--advertise", "--allow-shutdown", "--machine-uuid",
            "--machine-ordinal", "--provenance-type", "--provenance-uuid",
            "--provenance-version", "--rom-dir"};
        const std::set<std::string> no_preset_form{
            "--write-protect", "--hide", "--motherboard-link", board_rtc_flag<Memory>()};
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            const std::string flag = arg.substr(0, arg.find('='));
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--name" && i + 1 < argc) {
                preset_name = argv[++i];
            } else if (arg == "--description" && i + 1 < argc) {
                preset_description = argv[++i];
            } else if (arg == "--release-date" && i + 1 < argc) {
                release_date = argv[++i];
            } else if (arg == "--output" && i + 1 < argc) {
                output_filepath = argv[++i];
            } else if (arg == "--from" && i + 1 < argc) {
                machine_args.push_back("--preset");
                machine_args.push_back(argv[++i]);
            } else if (launch_only.count(flag) != 0) {
                std::cerr << "Error: " << flag << " configures the launch, not the machine; "
                             "a preset cannot record it\n";
                return ExitCode::USAGE;
            } else if (!flag.empty() && no_preset_form.count(flag) != 0) {
                std::cerr << "Error: " << flag << " has no preset form yet\n";
                return ExitCode::USAGE;
            } else {
                machine_args.push_back(std::move(arg));
            }
        }
        if (preset_name.empty()) {
            std::cerr << "Error: --name is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        ServerConfig<MachineType> config;
        {
            std::vector<char*> machine_argv;
            for (auto& a : machine_args) machine_argv.push_back(a.data());
            machine_argv.push_back(nullptr);
            if (auto exit_code = parse_start_arguments<MachineType>(
                    static_cast<int>(machine_args.size()), machine_argv.data(), 1, config)) {
                return *exit_code;
            }
        }
        if (auto err = validate_config(config)) {
            std::cerr << "Error: " << *err << "\n";
            return ExitCode::USAGE;
        }

        // The source preset's descriptive fields are the baseline for the
        // copy's (its machine settings arrived through parse_start_arguments).
        std::optional<PresetConfig> source;
        if (config.preset_filepath) {
            auto loaded = load_preset(*config.preset_filepath);
            if (loaded) source = std::move(*loaded.config);
        }
        if (preset_description.empty() && source && source->description) {
            preset_description = *source->description;
        }
        if (release_date.empty() && source && source->release_date) {
            release_date = *source->release_date;
        }

        PresetDocumentHeader header{preset_name, preset_description, release_date,
                                    source ? source->thumbnail_capture_delay_seconds
                                           : std::nullopt};
        std::string write_error;
        auto document = preset_document(config, header, DocumentKeys::Given, write_error);
        if (!document) {
            std::cerr << "Error: " << write_error << "\n";
            return ExitCode::USAGE;
        }
        const auto& preset = *document;

        // Determine output path
        std::filesystem::path target_filepath;
        std::string preset_id;

        if (!output_filepath.empty()) {
            target_filepath = output_filepath;
            // For --output, just output the path
            preset_id = output_filepath;
        } else {
            // Create in user presets directory
            if (!PresetPaths::ensure_user_presets_dirpath_exists()) {
                std::cerr << "Error: cannot create user presets directory\n";
                return ExitCode::IOERR;
            }

            // Generate unique ID
            preset_id = PresetPaths::generate_unique_preset_id(PresetPaths::slugify(preset_name));
            target_filepath = PresetPaths::get_user_presets_dirpath() /
                              (preset_id + std::string(PresetPaths::PRESET_EXTENSION));
        }

        // Write preset file
        std::ofstream output_file(target_filepath);
        if (!output_file) {
            std::cerr << "Error: cannot write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        output_file << preset.dump(2) << "\n";
        output_file.close();

        if (!output_file) {
            std::cerr << "Error: failed to write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        // Output the created preset ID (or path)
        std::cout << preset_id << "\n";
        return ExitCode::OK;
    }
};

// ResolvePreset subcommand - load a preset exactly as `start` would and print
// the machine it describes, with every key filled in
template<typename MachineType>
class ResolvePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "resolve-preset"; }
    std::string_view description() const override {
        return "Print a preset as start would load it, with every key filled in";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " resolve-preset <id|filepath> [--json]\n"
                  << "\n"
                  << "Loads a preset the way 'start --preset' does -- the same loader, the same\n"
                  << "extension normalisation and validation -- without building a machine, and\n"
                  << "prints the machine it describes as a preset document in which every key\n"
                  << "is present: defaults filled in, relative paths resolved, the model's\n"
                  << "default ROMs as explicit sideways slots, and every extension parameter\n"
                  << "given (each extension also lists its attaches_to points).\n"
                  << "\n"
                  << "Without --json, the document goes to stdout and any diagnostics to stderr.\n"
                  << "With --json, stdout is one object:\n"
                  << "  {\"preset\": <document, or null if none could be loaded>,\n"
                  << "   \"diagnostics\": [{\"severity\": \"error\"|\"warning\",\n"
                  << "                    \"path\": <JSON pointer into the preset>,\n"
                  << "                    \"message\": <text>}, ...]}\n"
                  << "\n"
                  << "Exits 0 when there are no errors, 66 when the preset cannot be found or\n"
                  << "read, and 78 when it does not describe a valid machine for this model.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::string preset_arg;
        bool json_output = false;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--json") {
                json_output = true;
            } else if (!arg.empty() && arg[0] != '-' && preset_arg.empty()) {
                preset_arg = std::string(arg);
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }
        if (preset_arg.empty()) {
            std::cerr << "Error: a preset id or file path is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        std::vector<Diagnostic> diagnostics;
        auto error = [&](std::string path, std::string message) {
            diagnostics.push_back({std::move(path), std::move(message)});
        };
        auto warning = [&](std::string path, std::string message) {
            diagnostics.push_back(
                {std::move(path), std::move(message), Diagnostic::Severity::Warning});
        };

        std::optional<nlohmann::ordered_json> document;
        int exit_code = resolve(argv[0], preset_arg, document, error, warning);

        if (json_output) {
            nlohmann::ordered_json out;
            out["preset"] = document ? *document : nlohmann::ordered_json(nullptr);
            out["diagnostics"] = nlohmann::ordered_json::array();
            for (const auto& d : diagnostics) {
                out["diagnostics"].push_back({{"severity", std::string(d.severity_name())},
                                              {"path", d.path},
                                              {"message", d.message}});
            }
            std::cout << out.dump(2) << "\n";
        } else {
            if (document) {
                std::cout << document->dump(2) << "\n";
            }
            for (const auto& d : diagnostics) {
                std::cerr << (d.is_error() ? "Error: " : "Warning: ");
                if (!d.path.empty()) std::cerr << d.path << ": ";
                std::cerr << d.message << "\n";
            }
        }
        return exit_code;
    }

private:
    template<typename Error, typename Warning>
    static int resolve(const char* argv0, const std::string& preset_arg,
                       std::optional<nlohmann::ordered_json>& document,
                       Error error, Warning warning) {
        using Memory = typename MachineType::Memory;

        // Find and load the preset as `start --preset` does.
        const auto located = locate_preset(preset_arg);
        if (!located) {
            error("", preset_not_found_message(preset_arg));
            return ExitCode::NOINPUT;
        }
        const std::filesystem::path preset_filepath = *located;
        auto loaded = load_preset(preset_filepath);
        for (const auto& w : loaded.warnings()) {
            warning(w.path, w.message);
        }
        if (!loaded) {
            error(loaded.error()->path, loaded.error()->message);
            return loaded.unreadable ? ExitCode::NOINPUT : ExitCode::CONFIG;
        }
        const PresetConfig& preset = *loaded.config;
        if (auto err = preset_model_error<Memory>(preset)) {
            error("/model", *err);
            return ExitCode::CONFIG;
        }

        // Build the configuration exactly as parse_start_arguments does with
        // no other arguments.
        ServerConfig<MachineType> config;
        config.preset_filepath = preset_filepath;
        apply_preset(config, preset);
        if (auto err = populate_extension_resolver(argv0, {}, config)) {
            error("", "cannot scan the extension directories");
            return *err;
        }
        bool failed = false;
        for (const auto& e : fold_in_preset(config)) {
            error(e.path, e.message);
            failed = true;
        }
        if (auto err = validate_startup_links(config)) {
            error("/startup", *err);
            failed = true;
        }
        if (auto err = validate_sideways_slots(config)) {
            // Locate it at the preset's slot entry for the request at fault.
            std::string path = "/sideways_bank";
            if (err->index) {
                const auto slot = config.sideways_configs[*err->index].slot;
                for (std::size_t i = 0; i < preset.sideways.size(); ++i) {
                    if (preset.sideways[i].slot == slot) {
                        path = "/sideways_bank/slots/" + std::to_string(i);
                    }
                }
            }
            error(path, err->message);
            failed = true;
        }
        if constexpr (HasDiscControllerSocket<Memory>) {
            if (!config.fdc_type.empty() && config.fdc_type != "none" &&
                !DiscControllerRegistry::find(config.fdc_type)) {
                error("/storage/fdc_socket/id",
                      "unknown disc controller '" + config.fdc_type + "' (see list-fdcs)");
                failed = true;
            }
        } else if (!config.fdc_type.empty()) {
            warning("/storage/fdc_socket/id",
                    "ignored: this model has a built-in disc controller");
        }
        apply_rom_defaults(config);
        if (check_bundled_images_exist(config, error, warning)) failed = true;
        check_roms_exist(config, preset, warning);
        check_floppies_exist(config, warning);

        std::string write_error;
        document = preset_document(
            config,
            PresetDocumentHeader{preset.name, preset.description, preset.release_date,
                                 preset.thumbnail_capture_delay_seconds},
            DocumentKeys::Every, write_error);
        if (!document) {
            error("/econet", write_error);
            return ExitCode::CONFIG;
        }
        return failed ? ExitCode::CONFIG : ExitCode::OK;
    }

    // A ROM the machine would load but that cannot be found is reported, not
    // fatal: the ROM directory may differ where the preset is launched.
    template<typename Warning>
    static void check_roms_exist(const ServerConfig<MachineType>& config,
                                 const PresetConfig& preset, Warning warning) {
        auto check = [&](const std::string& path, const std::string& rom) {
            try {
                (void)RomPaths::find_rom(rom);
            } catch (const std::exception& e) {
                warning(path, e.what());
            }
        };
        check(preset.mos_image_uri ? "/mos_image_uri" : "", config.mos_filepath);
        for (const auto& [slot, image] : config.rom_slots) {
            if (image == EMPTY_SLOT_MARKER) continue;
            std::string path;
            for (std::size_t i = 0; i < preset.sideways.size(); ++i) {
                if (preset.sideways[i].slot == slot) {
                    path = "/sideways_bank/slots/" + std::to_string(i) + "/image_uri";
                }
            }
            if (image == RAM_SLOT_MARKER) {
                for (const auto& c : config.sideways_configs) {
                    if (c.slot == slot && !c.image_filepath.empty()) {
                        check(path, c.image_filepath);
                    }
                }
                continue;
            }
            check(path, image);
        }
    }

    // A bare name in a parameter declared bundled must name a bundled image;
    // the launch fails otherwise. A plugin's ROMs are certain, so an unknown
    // one is an error; the hard-disc masters are the server's disc directory,
    // which the launch may see differently, so an unknown one is a warning.
    template<typename Error, typename Warning>
    static bool check_bundled_images_exist(const ServerConfig<MachineType>& config,
                                           Error error, Warning warning) {
        bool failed = false;
        const auto images = bundled_images(config.extension_resolver);
        for (const auto& inst : config.extension_instances) {
            const auto* m = config.extension_resolver.find_by_name(inst.name);
            if (!m) continue;
            for (const auto& p : m->parameters) {
                if (p.bundled.empty()) continue;
                auto it = inst.config.find(p.key);
                if (it == inst.config.end() || !is_bare_name(it->second) ||
                    find_bundled_image(images, *m, p, it->second)) {
                    continue;
                }
                const auto path = inst.from_preset ? inst.preset_pointer + "/config/" + p.key
                                                   : std::string();
                if (p.bundled == "rom") {
                    error(path, "'" + it->second + "' is not a ROM " + inst.name + " ships");
                    failed = true;
                } else {
                    warning(path, "'" + it->second + "' is not a bundled " + p.bundled +
                                      " image");
                }
            }
        }
        return failed;
    }

    template<typename Warning>
    static void check_floppies_exist(const ServerConfig<MachineType>& config, Warning warning) {
        for (std::size_t drive = 0; drive < config.floppy_filepaths.size(); ++drive) {
            const auto& uri = config.floppy_filepaths[drive];
            if (uri.empty()) continue;
            auto url = beebium::DiscUrl::parse(uri);
            std::error_code ec;
            if (url && url->is_local() && !std::filesystem::exists(url->to_filepath(), ec)) {
                warning("/storage/floppy_drives/" + std::to_string(drive) + "/image_uri",
                        "disc image not found: " + url->to_filepath().string());
            }
        }
    }

};

// ListSerialPorts subcommand - the host's serial ports, for a configuration
// UI offering values to a parameter declared choices_from "serial-ports"
template<typename MachineType>
class ListSerialPortsSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "list-serial-ports"; }
    std::string_view description() const override {
        return "List the host's serial ports";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " list-serial-ports\n"
                  << "\n"
                  << "Lists the host's serial ports, with the USB identity the OS reports\n"
                  << "for each (best-effort; a port may be missing or unopenable). These are\n"
                  << "the values a configuration UI offers for an extension parameter whose\n"
                  << "manifest declares choices_from \"serial-ports\" (see describe-extension).\n"
                  << "Honours --format pretty|tsv|jsonl.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            }
            std::cerr << "Unknown argument: " << arg << "\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        const auto ports = beebium::serial::enumerate_serial_ports();
        auto hex = [](const std::optional<std::uint16_t>& id) -> std::string {
            if (!id) return {};
            char text[8];
            std::snprintf(text, sizeof text, "%04x", static_cast<unsigned>(*id));
            return text;
        };
        switch (resolve_output_format(global.output_format)) {
            case OutputFormat::Pretty:
                if (ports.empty()) {
                    std::cout << "No serial ports found.\n";
                }
                for (const auto& p : ports) {
                    std::cout << p.path;
                    if (p.usb_vendor_id) {
                        std::cout << "  [" << hex(p.usb_vendor_id) << ":"
                                  << hex(p.usb_product_id) << "]";
                    }
                    if (!p.manufacturer.empty() || !p.product.empty()) {
                        std::cout << "  " << p.manufacturer
                                  << (p.manufacturer.empty() || p.product.empty() ? "" : " ")
                                  << p.product;
                    }
                    std::cout << "\n";
                }
                break;
            case OutputFormat::Tsv:
                std::cout << "path\tusb_vendor_id\tusb_product_id\tmanufacturer\tproduct\t"
                             "serial_number\n";
                for (const auto& p : ports) {
                    std::cout << p.path << "\t" << hex(p.usb_vendor_id) << "\t"
                              << hex(p.usb_product_id) << "\t" << p.manufacturer << "\t"
                              << p.product << "\t" << p.serial_number << "\n";
                }
                break;
            case OutputFormat::Jsonl:
                for (const auto& p : ports) {
                    auto id = [](const std::optional<std::uint16_t>& v) {
                        return v ? nlohmann::ordered_json(*v) : nlohmann::ordered_json(nullptr);
                    };
                    std::cout << nlohmann::ordered_json{
                                     {"path", p.path},
                                     {"usb_vendor_id", id(p.usb_vendor_id)},
                                     {"usb_product_id", id(p.usb_product_id)},
                                     {"manufacturer", p.manufacturer},
                                     {"product", p.product},
                                     {"serial_number", p.serial_number}}
                                     .dump()
                              << "\n";
                }
                break;
            case OutputFormat::Auto:
                break;
        }
        return ExitCode::OK;
    }
};

// DeletePreset subcommand - delete a user preset
template<typename MachineType>
class DeletePresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "delete-preset"; }
    std::string_view description() const override { return "Delete a user preset"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " delete-preset <id>\n"
                  << "\n"
                  << "Deletes a user preset. System presets cannot be deleted.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto filepath = PresetPaths::find_preset_filepath(preset_id);
        if (!filepath) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        // Check if it's a system preset
        if (PresetPaths::is_system_preset(*filepath)) {
            std::cerr << "Error: cannot delete system preset: " << preset_id << "\n";
            return ExitCode::USAGE;
        }

        // Delete the file
        std::error_code ec;
        if (!std::filesystem::remove(*filepath, ec)) {
            std::cerr << "Error: cannot delete preset file: " << filepath->string();
            if (ec) {
                std::cerr << " (" << ec.message() << ")";
            }
            std::cerr << "\n";
            return ExitCode::IOERR;
        }

        return ExitCode::OK;
    }
};

// ImportPreset subcommand - import a preset file into user directory
template<typename MachineType>
class ImportPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "import-preset"; }
    std::string_view description() const override { return "Import a preset file into user directory"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " import-preset <filepath>\n"
                  << "\n"
                  << "Imports a preset file into the user presets directory.\n"
                  << "The preset's model field must match this executable's model.\n"
                  << "Outputs the imported preset ID.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        using Memory = typename MachineType::Memory;

        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::filesystem::path source_filepath;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] != '-') {
                if (source_filepath.empty()) {
                    source_filepath = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (source_filepath.empty()) {
            std::cerr << "Error: filepath required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Verify source file exists
        if (!std::filesystem::exists(source_filepath)) {
            std::cerr << "Error: file not found: " << source_filepath.string() << "\n";
            return ExitCode::NOINPUT;
        }

        // Read and parse the preset file
        std::ifstream input_file(source_filepath);
        if (!input_file) {
            std::cerr << "Error: cannot read file: " << source_filepath.string() << "\n";
            return ExitCode::NOINPUT;
        }

        nlohmann::json preset;
        try {
            input_file >> preset;
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error: invalid JSON: " << e.what() << "\n";
            return ExitCode::DATAERR;
        }

        // Verify model field matches this executable
        if (!preset.contains("model") || !preset["model"].is_string()) {
            std::cerr << "Error: preset missing 'model' field\n";
            return ExitCode::DATAERR;
        }

        std::string preset_model = preset["model"].get<std::string>();
        if (preset_model != Memory::MACHINE_TYPE) {
            std::cerr << "Error: preset model '" << preset_model << "' does not match this executable's model '"
                      << Memory::MACHINE_TYPE << "'\n";
            return ExitCode::DATAERR;
        }

        // Determine ID from filename
        std::string filename = source_filepath.stem().string();
        // Remove .preset suffix if present
        if (filename.size() > 7 && filename.substr(filename.size() - 7) == ".preset") {
            filename = filename.substr(0, filename.size() - 7);
        }
        std::string preset_id = PresetPaths::slugify(filename);
        if (preset_id.empty()) {
            // Fall back to using name field if present
            if (preset.contains("name") && preset["name"].is_string()) {
                preset_id = PresetPaths::slugify(preset["name"].get<std::string>());
            }
        }
        if (preset_id.empty()) {
            preset_id = "imported-preset";
        }

        // Ensure user presets directory exists
        if (!PresetPaths::ensure_user_presets_dirpath_exists()) {
            std::cerr << "Error: cannot create user presets directory\n";
            return ExitCode::IOERR;
        }

        // Generate unique ID to avoid conflicts
        preset_id = PresetPaths::generate_unique_preset_id(preset_id);

        // Write to user presets directory
        std::filesystem::path target_filepath = PresetPaths::get_user_presets_dirpath() /
                                                (preset_id + std::string(PresetPaths::PRESET_EXTENSION));

        std::ofstream output_file(target_filepath);
        if (!output_file) {
            std::cerr << "Error: cannot write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        output_file << preset.dump(2) << "\n";
        output_file.close();

        if (!output_file) {
            std::cerr << "Error: failed to write preset file: " << target_filepath.string() << "\n";
            return ExitCode::IOERR;
        }

        std::cout << preset_id << "\n";
        return ExitCode::OK;
    }
};

// ExportPreset subcommand - export a preset to a file
template<typename MachineType>
class ExportPresetSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "export-preset"; }
    std::string_view description() const override { return "Export a preset to a file"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " export-preset <id> --output <filepath>\n"
                  << "\n"
                  << "Exports a preset to the specified file path.\n"
                  << "\n"
                  << "Options:\n"
                  << "  --output <path>   Output file path (required)\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        // Handle --help
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Parse subcommand options
        std::string preset_id;
        std::filesystem::path output_filepath;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--output" || arg == "-o") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: --output requires a path\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
                output_filepath = argv[++i];
            } else if (!arg.empty() && arg[0] != '-') {
                if (preset_id.empty()) {
                    preset_id = std::string(arg);
                } else {
                    std::cerr << "Too many arguments\n";
                    help(argv[0]);
                    return ExitCode::USAGE;
                }
            } else {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (preset_id.empty()) {
            std::cerr << "Error: preset ID required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        if (output_filepath.empty()) {
            std::cerr << "Error: --output is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Find preset file
        auto source_filepath = PresetPaths::find_preset_filepath(preset_id);
        if (!source_filepath) {
            std::cerr << "Error: preset not found: " << preset_id << "\n";
            return ExitCode::NOINPUT;
        }

        // Copy file to output path
        std::error_code ec;
        std::filesystem::copy_file(*source_filepath, output_filepath,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "Error: cannot write to output file: " << output_filepath.string()
                      << " (" << ec.message() << ")\n";
            return ExitCode::IOERR;
        }

        return ExitCode::OK;
    }
};

// CaptureScreenshot subcommand - headless emulation to PNG
template<typename MachineType>
class CaptureScreenshotSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "capture-screenshot"; }
    std::string_view description() const override {
        return "Capture a screenshot from headless emulation";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name
                  << " capture-screenshot --output <filepath> [options]\n"
                  << "\n"
                  << "Runs the emulator headlessly and captures a framebuffer screenshot as PNG.\n"
                  << "Accepts all 'start' options (--preset, --floppy, --fdc, etc.).\n"
                  << "\n"
                  << "Required:\n"
                  << "  --output <filepath>      Output PNG filepath\n"
                  << "\n"
                  << "Optional:\n"
                  << "  --duration <seconds>     Emulation duration before capture (default: from\n"
                  << "                           preset thumbnail_capture_delay_seconds, or 2.0)\n"
                  << "  --border <pixels>        Black border width around the image (default: 20)\n"
                  << "  --crop <mode>            none (default) or auto. auto crops to the\n"
                  << "                           screen content (e.g. a boot banner top-left in a\n"
                  << "                           mostly black frame) and enlarges it to the same\n"
                  << "                           image size, keeping the frame's aspect ratio and\n"
                  << "                           resampling with a filter (not pixel-replicated).\n"
                  << "\n"
                  << "All 'start' subcommand options are also accepted.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        // Pre-scan argv for capture-specific args (--output, --duration, --border),
        // then pass the rest to parse_start_arguments unchanged.
        std::string output_filepath;
        std::optional<double> cli_duration;
        int border_pixels = 20;
        bool crop_auto = false;  // --crop none (default) | auto

        // Accept the mode of a --crop flag in either form (`--crop auto` or
        // `--crop=auto`); returns false and reports a USAGE error for an unknown
        // value, naming the accepted ones.
        auto parse_crop_mode = [](std::string_view mode, bool& out) -> bool {
            if (mode == "none") { out = false; return true; }
            if (mode == "auto") { out = true; return true; }
            std::cerr << "Error: --crop must be 'none' or 'auto'\n";
            return false;
        };

        std::vector<char*> filtered_argv;
        for (int i = 0; i < argc; ++i) {
            if (i >= global.subcommand_argv_start) {
                std::string_view arg = argv[i];
                if (arg == "--output" && i + 1 < argc) {
                    output_filepath = argv[i + 1];
                    ++i;
                    continue;
                } else if (arg == "--duration" && i + 1 < argc) {
                    try {
                        cli_duration = std::stod(argv[i + 1]);
                    } catch (...) {
                        std::cerr << "Error: --duration must be a number\n";
                        return ExitCode::USAGE;
                    }
                    if (*cli_duration < 0) {
                        std::cerr << "Error: --duration must be non-negative\n";
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg == "--border" && i + 1 < argc) {
                    try {
                        border_pixels = parse_int(argv[i + 1], "--border");
                    } catch (const std::exception& e) {
                        std::cerr << "Error: " << e.what() << "\n";
                        return ExitCode::USAGE;
                    }
                    if (border_pixels < 0) {
                        std::cerr << "Error: --border must be non-negative\n";
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg == "--crop" && i + 1 < argc) {
                    if (!parse_crop_mode(argv[i + 1], crop_auto)) {
                        return ExitCode::USAGE;
                    }
                    ++i;
                    continue;
                } else if (arg.rfind("--crop=", 0) == 0) {
                    if (!parse_crop_mode(arg.substr(7), crop_auto)) {
                        return ExitCode::USAGE;
                    }
                    continue;
                } else if (arg == "--help" || arg == "-h") {
                    help(argv[0]);
                    return ExitCode::OK;
                }
            }
            filtered_argv.push_back(argv[i]);
        }
        int filtered_argc = static_cast<int>(filtered_argv.size());

        if (output_filepath.empty()) {
            std::cerr << "Error: --output is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // Parse machine configuration from remaining args
        ServerConfig<MachineType> config;
        if (auto exit_code = parse_start_arguments<MachineType>(
                filtered_argc, filtered_argv.data(),
                global.subcommand_argv_start, config)) {
            return *exit_code;
        }

        if (auto error = validate_config<MachineType>(config)) {
            std::cerr << "Error: " << *error << "\n";
            return ExitCode::CONFIG;
        }

        // Determine capture duration.
        // Priority: CLI --duration > preset thumbnail_capture_delay_seconds > 2.0
        double duration_seconds = 2.0;
        if (cli_duration) {
            duration_seconds = *cli_duration;
        } else if (config.preset_filepath) {
            auto result = load_preset(*config.preset_filepath);
            if (result && result.config->thumbnail_capture_delay_seconds) {
                duration_seconds = *result.config->thumbnail_capture_delay_seconds;
            }
        }

        try {
            if (!config.rom_dirpath.empty()) {
                RomPaths::set_rom_directory(config.rom_dirpath);
            }

            // Assemble the machine through the one shared assembly, exactly as
            // `start` does. This installs any --tube-* coprocessor and every
            // other extension the config carries -- the old screenshot path
            // built its own extension-less machine, so a Tube preset rendered
            // the host-only banner. Screenshots need video but not audio.
            auto assembly = assemble_machine<MachineType>(config, /*with_audio=*/false);
            if (!assembly.assembled) {
                return assembly.exit_code;
            }
            auto& am = *assembly.assembled;
            auto& machine = am.machine;

            // Create local framebuffer and renderer (no gRPC server needed)
            FrameBuffer frame_buffer;
            FrameRenderer renderer(&frame_buffer);
            renderer.set_field_cycles(&machine.video_binding().renderer.field_cycles());
            auto& video_output = *machine.state().memory.video_output;

            // Helper to drain the video output queue completely.
            // Each machine.run() step produces ~20,000 PixelBatches (one per 2 CPU
            // cycles), so we must drain the entire queue after each step rather than
            // relying on the default max_units of 1000.
            auto drain_video = [&]() {
                while (renderer.process(video_output, 100000) > 0) {}
            };

            constexpr uint64_t cycles_per_step = 40000;

            // Advance the machine one chunk through the SAME stepper the serving
            // loop uses -- disc-drive ticks, Econet speed gate and run() -- so
            // the screenshot capture cannot diverge from a running server. No
            // pacing (a screenshot renders as fast as it can), never parked, so
            // the on-parked hook is a no-op and the budget is a fixed chunk.
            auto step_chunk = [&] {
                step_emulation(machine, /*speed_multiplier=*/1.0,
                               /*on_parked=*/[] {},
                               [&]() -> uint64_t { return cycles_per_step; });
            };

            if (duration_seconds == 0.0) {
                // Duration 0: capture first complete frame
                uint64_t initial_version = frame_buffer.version();
                while (frame_buffer.version() == initial_version) {
                    step_chunk();
                    drain_video();
                }
            } else {
                uint64_t total_cycles = static_cast<uint64_t>(
                    duration_seconds * timing::CYCLES_PER_SECOND);
                uint64_t cycles_run = 0;
                while (cycles_run < total_cycles) {
                    step_chunk();
                    drain_video();
                    cycles_run += cycles_per_step;
                }
            }

            // The capture is complete; shut the extensions down cleanly (e.g.
            // remove a coprocessor from the Tube socket) while the machine is
            // still alive, as `start` does after its serving loop.
            am.extension_registry.shutdown();

            if (frame_buffer.version() == 0) {
                std::cerr << "Error: no frames produced during emulation\n";
                return ExitCode::SOFTWARE;
            }

            // Extract frame data: the metadata and pixels of one frame
            FrameMetadata metadata;
            std::vector<uint32_t> frame_copy(frame_buffer.capacity_pixels());
            frame_buffer.read_frame(metadata, frame_copy);
            uint32_t frame_width = metadata.width;
            uint32_t frame_height = metadata.height;
            size_t stride_pixels = frame_buffer.stride_pixels();

            // Scale from logical frame to display dimensions (nearest-neighbour)
            // with black border around the image
            uint32_t display_width = metadata.display_width;
            uint32_t display_height = metadata.display_height;
            uint32_t border = static_cast<uint32_t>(border_pixels);
            uint32_t output_width = display_width + 2 * border;
            uint32_t output_height = display_height + 2 * border;

            // Zero-initialised = black with alpha 0, then we set alpha for all pixels
            std::vector<uint8_t> rgba(output_width * output_height * 4, 0);

            // Set alpha to 0xFF for all pixels (border stays black RGB)
            for (size_t i = 3; i < rgba.size(); i += 4) {
                rgba[i] = 0xFF;
            }

            // Write one display-sized content pixel (BGRA32) into the bordered
            // region.
            auto put_pixel = [&](uint32_t x, uint32_t y, uint32_t bgra) {
                size_t dst_idx = ((y + border) * output_width + (x + border)) * 4;
                rgba[dst_idx + 0] = static_cast<uint8_t>((bgra >> 16) & 0xFF);  // R
                rgba[dst_idx + 1] = static_cast<uint8_t>((bgra >> 8) & 0xFF);   // G
                rgba[dst_idx + 2] = static_cast<uint8_t>(bgra & 0xFF);           // B
                rgba[dst_idx + 3] = static_cast<uint8_t>((bgra >> 24) & 0xFF);   // A
            };

            if (crop_auto) {
                // Find the content and resample that logical sub-rectangle up to
                // the SAME display size and border a full thumbnail has, with a
                // filtered (Lanczos-3) per-axis scale -- so the banner is enlarged
                // legibly, not pixel-replicated, and a thumbnail set stays uniform
                // in aspect and pixel size. See ScreenshotCrop.hpp / ImageResample.hpp.
                CropRect src_rect = compute_auto_crop(frame_copy.data(), frame_width,
                                                      frame_height, stride_pixels);
                std::vector<uint32_t> content = resample_bgra_lanczos3(
                    frame_copy.data(), stride_pixels, src_rect.x, src_rect.y,
                    src_rect.width, src_rect.height, display_width, display_height);
                for (uint32_t y = 0; y < display_height; ++y) {
                    for (uint32_t x = 0; x < display_width; ++x) {
                        put_pixel(x, y, content[static_cast<size_t>(y) * display_width + x]);
                    }
                }
            } else {
                // --crop none: the whole logical frame scaled nearest-neighbour to
                // the display -- byte-for-byte the original behaviour.
                for (uint32_t y = 0; y < display_height; ++y) {
                    uint32_t src_y = y * frame_height / display_height;
                    if (src_y >= frame_height) src_y = frame_height - 1;
                    for (uint32_t x = 0; x < display_width; ++x) {
                        uint32_t src_x = x * frame_width / display_width;
                        if (src_x >= frame_width) src_x = frame_width - 1;
                        put_pixel(x, y, frame_copy[src_y * stride_pixels + src_x]);
                    }
                }
            }

            int write_result = stbi_write_png(
                output_filepath.c_str(),
                static_cast<int>(output_width),
                static_cast<int>(output_height),
                4,
                rgba.data(),
                static_cast<int>(output_width * 4)
            );

            if (!write_result) {
                std::cerr << "Error: failed to write PNG: " << output_filepath << "\n";
                return ExitCode::IOERR;
            }

            std::cout << output_filepath << "\n";
            return ExitCode::OK;

        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::SOFTWARE;
        }
    }
};

// Help subcommand - shows help for other subcommands
// DescribeRom subcommand - parse a sideways ROM header and print it as JSON.
// Model-independent (ROM header parsing is generic). Used by frontends to show
// proper ROM titles/versions instead of filenames.
template<typename MachineType>
class DescribeRomSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-rom"; }
    std::string_view description() const override {
        return "Parse a sideways ROM image header and print it as JSON";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-rom <rom> [--rom-dir <dir>]\n"
                  << "\n"
                  << "Parses the standard sideways ROM header of <rom> (a ROM library name or a\n"
                  << "path to a ROM image) and prints it as JSON: whether it is a recognised\n"
                  << "sideways ROM, its title, version and copyright, the type-byte flags, and\n"
                  << "whether it is a language or a service ROM.\n"
                  << "\n"
                  << "  --rom-dir <dir>   Directory to resolve a bare ROM name against\n"
                  << "\n"
                  << "Output is always JSON regardless of the --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::string rom_ref;
        std::string rom_dir;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (arg == "--rom-dir" && i + 1 < argc) {
                rom_dir = argv[++i];
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            } else if (rom_ref.empty()) {
                rom_ref = arg;
            } else {
                std::cerr << "Unexpected argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (rom_ref.empty()) {
            std::cerr << "Error: a ROM name or path is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }
        if (!rom_dir.empty()) {
            RomPaths::set_rom_directory(rom_dir);
        }

        std::vector<uint8_t> data;
        try {
            data = load_file(RomPaths::find_rom(rom_ref));
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            return ExitCode::NOINPUT;
        }

        auto header = beebium::parse_sideways_rom_header(data);

        using ojson = nlohmann::ordered_json;
        ojson out;
        out["recognised"] = header.recognised;
        out["title"] = header.title;
        out["version"] = header.version;
        out["copyright"] = header.copyright;
        out["is_language"] = header.is_language();
        out["is_service_only"] = header.is_service_only();
        out["has_language_entry"] = header.has_language_entry;
        out["has_service_entry"] = header.has_service_entry;
        out["has_relocation_address"] = header.has_relocation_address;
        out["supports_electron_firmkeys"] = header.supports_electron_firmkeys;
        out["cpu_type"] = header.cpu_type;
        out["binary_version"] = header.binary_version;
        out["contains_romfs"] = header.contains_romfs;
        out["romfs_data_offset"] = header.romfs_data_offset;
        // "kinds" is a convenience list for UIs: ROMs can be any combination
        // of language / service / romfs (e.g. Acornsoft cartridges are all three).
        ojson kinds = ojson::array();
        if (header.has_language_entry) kinds.push_back("language");
        if (header.has_service_entry) kinds.push_back("service");
        if (header.contains_romfs) kinds.push_back("romfs");
        out["kinds"] = kinds;
        std::cout << out.dump(2) << "\n";
        return ExitCode::OK;
    }
};

// Validate and categorise a disc image, the way the running server does when
// a disc is inserted. This is the same rule -- the shared DiscFormatRegistry
// via load_disc_from_url_or_filepath -- reached through the CLI, so a front
// end configuring a machine before it is launched judges an image by exactly
// the criteria the machine itself will apply. There is no second, weaker rule
// anywhere.
template<typename MachineType>
class DescribeDiscImageSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "describe-disc-image"; }
    std::string_view description() const override {
        return "Validate a floppy disc image and print its format as JSON";
    }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " describe-disc-image <path>\n"
                  << "\n"
                  << "Loads the disc image at <path> using the same format detection the\n"
                  << "server applies when a disc is inserted, and prints the result as JSON:\n"
                  << "whether it is a recognised disc image, its format, how many sides it\n"
                  << "has, and whether it is write-protected. When it is not recognised, the\n"
                  << "reason is given.\n"
                  << "\n"
                  << "Exit status is 0 whenever the file could be read, whether or not it is\n"
                  << "a disc image (read the \"recognised\" field); non-zero only if the file\n"
                  << "itself cannot be read.\n"
                  << "\n"
                  << "Output is always JSON regardless of the --format option.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override {
        if (global.help_requested) {
            help(argv[0]);
            return ExitCode::OK;
        }

        std::string path;
        for (int i = global.subcommand_argv_start; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--help" || arg == "-h") {
                help(argv[0]);
                return ExitCode::OK;
            } else if (!arg.empty() && arg[0] == '-') {
                std::cerr << "Unknown argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            } else if (path.empty()) {
                path = arg;
            } else {
                std::cerr << "Unexpected argument: " << arg << "\n";
                help(argv[0]);
                return ExitCode::USAGE;
            }
        }

        if (path.empty()) {
            std::cerr << "Error: a disc image path is required\n";
            help(argv[0]);
            return ExitCode::USAGE;
        }

        // A missing or unreadable file is an operational failure, not a
        // verdict on an image: report it on stderr with a non-zero status,
        // rather than as recognised=false.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || ec) {
            std::cerr << "Error: cannot read disc image: " << path << "\n";
            return ExitCode::NOINPUT;
        }

        // The one rule, shared with InsertDisc.
        auto result = load_disc_from_url_or_filepath(path);

        using ojson = nlohmann::ordered_json;
        ojson out;
        out["recognised"] = result.success();
        if (result.success()) {
            const Disc* disc = result.disc.get();
            out["format"] = std::string(disc->format_name());
            out["sides"] = disc->is_double_sided() ? 2 : 1;
            out["write_protected"] = disc->is_write_protected();
        } else {
            out["format"] = "";
            out["reason"] = result.error;
            // Machine-readable classification of the failure, so a front end can
            // choose a fitting label without parsing the reason text.
            out["kind"] = std::string(disc_load_error_kind_name(result.kind));
        }
        std::cout << out.dump(2) << "\n";
        return ExitCode::OK;
    }
};

template<typename MachineType>
class HelpSubcommand : public Subcommand<MachineType> {
public:
    std::string_view name() const override { return "help"; }
    std::string_view description() const override { return "Show help for a subcommand"; }

    void help(const char* program_name) const override {
        std::cerr << "Usage: " << program_name << " help [subcommand]\n"
                  << "\n"
                  << "Shows help for the specified subcommand, or global help if none specified.\n";
    }

    int invoke(int argc, char* argv[], const GlobalConfig& global) const override;  // Defined after get_subcommands
};

// ============================================================================
// Subcommand registry and dispatch
// ============================================================================

// Get the singleton registry of all subcommands
template<typename MachineType>
const std::vector<std::unique_ptr<Subcommand<MachineType>>>& get_subcommands() {
    static auto cmds = []() {
        std::vector<std::unique_ptr<Subcommand<MachineType>>> v;
        v.push_back(std::make_unique<StartSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListFdcsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListFloppyFormatsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListExtensionsSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeExtensionSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListAttachmentPointsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListSerialPortsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListNamePlaceholdersSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribePresetSchemaSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeRomSubcommand<MachineType>>());
        v.push_back(std::make_unique<DescribeDiscImageSubcommand<MachineType>>());
        v.push_back(std::make_unique<ListPresetsSubcommand<MachineType>>());
        v.push_back(std::make_unique<ShowPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ResolvePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ReportPresetsDirpathSubcommand<MachineType>>());
        v.push_back(std::make_unique<CreatePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<DeletePresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ImportPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ExportPresetSubcommand<MachineType>>());
        v.push_back(std::make_unique<ReportAunMapFilepathSubcommand<MachineType>>());
        v.push_back(std::make_unique<CreateAunMapSubcommand<MachineType>>());
        v.push_back(std::make_unique<ShowAunMapSubcommand<MachineType>>());
        v.push_back(std::make_unique<AddAunPeerSubcommand<MachineType>>());
        v.push_back(std::make_unique<RemoveAunPeerSubcommand<MachineType>>());
        v.push_back(std::make_unique<AddAunSubnetSubcommand<MachineType>>());
        v.push_back(std::make_unique<RemoveAunSubnetSubcommand<MachineType>>());
        v.push_back(std::make_unique<CaptureScreenshotSubcommand<MachineType>>());
        v.push_back(std::make_unique<HelpSubcommand<MachineType>>());
        return v;
    }();
    return cmds;
}

// HelpSubcommand::invoke implementation (needs get_subcommands)
template<typename MachineType>
int HelpSubcommand<MachineType>::invoke(int argc, char* argv[], const GlobalConfig& global) const {
    // Check for target subcommand name in arguments
    std::string target_name;
    for (int i = global.subcommand_argv_start; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            // help --help shows help for the help command
            help(argv[0]);
            return ExitCode::OK;
        }
        if (!arg.empty() && arg[0] != '-') {
            target_name = std::string(arg);
            break;
        }
    }

    if (target_name.empty()) {
        // No target: show global help
        print_global_help<MachineType>(argv[0]);
        return ExitCode::OK;
    }

    // Find and show help for the target subcommand
    for (const auto& cmd : get_subcommands<MachineType>()) {
        if (cmd->name() == target_name) {
            cmd->help(argv[0]);
            return ExitCode::OK;
        }
    }

    std::cerr << "Unknown subcommand: " << target_name << "\n";
    print_global_help<MachineType>(argv[0]);
    return ExitCode::USAGE;
}

// Print global help (lists all subcommands)
template<typename MachineType>
void print_global_help(const char* program_name) {
    using Memory = typename MachineType::Memory;

    std::cerr << "Usage: " << program_name << " [global-options] <subcommand> [subcommand-options]\n"
              << "\n"
              << "Beebium " << BEEBIUM_VERSION << " - " << Memory::MACHINE_DISPLAY_NAME << " emulator server\n"
              << "\n"
              << "Global options:\n"
              << "  --help, -h          Show this help message\n"
              << "  --format <format>   Output format for data commands:\n"
              << "                      pretty - human-friendly (default if TTY)\n"
              << "                      tsv    - tab-separated values (default if not TTY)\n"
              << "                      jsonl  - JSON Lines (one JSON object per line)\n"
              << "\n"
              << "Subcommands:\n";

    for (const auto& cmd : get_subcommands<MachineType>()) {
        std::cerr << "  " << cmd->name();
        // Pad to align descriptions
        for (size_t i = cmd->name().length(); i < 22; ++i) {
            std::cerr << ' ';
        }
        std::cerr << cmd->description() << "\n";
    }

    std::cerr << "\n"
              << "Use '" << program_name << " <subcommand> --help' for subcommand options.\n"
              << "Use '" << program_name << " help <subcommand>' for subcommand help.\n"
              << "\n"
              << "If no subcommand is specified, 'start' is assumed.\n";
}

// Dispatch to the appropriate subcommand
template<typename MachineType>
int dispatch_subcommand(int argc, char* argv[], const GlobalConfig& global) {
    // Determine subcommand name (default to "start")
    std::string subcommand_name = global.subcommand_name;
    if (subcommand_name.empty()) {
        // No subcommand specified
        if (global.help_requested) {
            // Global --help with no subcommand
            print_global_help<MachineType>(argv[0]);
            return ExitCode::OK;
        }
        subcommand_name = "start";  // Default subcommand
    }

    // Find and invoke the subcommand
    for (const auto& cmd : get_subcommands<MachineType>()) {
        if (cmd->name() == subcommand_name) {
            return cmd->invoke(argc, argv, global);
        }
    }

    // Unknown subcommand
    std::cerr << "Unknown subcommand: " << subcommand_name << "\n";
    print_global_help<MachineType>(argv[0]);
    return ExitCode::USAGE;
}

// ============================================================================
// Main entry point
// ============================================================================

template<typename MachineType>
int server_main(int argc, char* argv[]) {
    // Parse global arguments
    GlobalConfig global;
    if (auto exit_code = parse_global_arguments(argc, argv, global)) {
        return *exit_code;
    }

    // Dispatch to appropriate subcommand
    return dispatch_subcommand<MachineType>(argc, argv, global);
}

// ============================================================================
// Legacy function for backwards compatibility with tests
// ============================================================================

} // namespace beebium::server

#endif // BEEBIUM_SERVER_SERVER_MAIN_HPP
