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

// What every subcommand shares: exit codes, the global options read before the
// subcommand name, the Subcommand interface, and small helpers (shutdown
// handling, UUIDs, argument completion, output format).

#pragma once

#include "beebium/server/CliArgParsers.hpp"
#include "beebium/server/Platform.hpp"
#include "beebium/PlatformUtils.hpp"

#ifdef _WIN32
#include <io.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
#include <stdexcept>
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
    constexpr int CANTCREAT = 73;   // Cannot create output (EX_CANTCREAT)
    constexpr int IOERR = 74;       // I/O error (EX_IOERR)
    constexpr int TEMPFAIL = 75;    // In use; try again later (EX_TEMPFAIL)
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

// Forward declarations for subcommand dispatch
template<typename MachineType>
const std::vector<std::unique_ptr<Subcommand<MachineType>>>& get_subcommands();

template<typename MachineType>
void print_global_help(const char* program_name);

} // namespace beebium::server
