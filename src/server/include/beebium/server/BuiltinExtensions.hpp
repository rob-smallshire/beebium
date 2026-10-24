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

#ifndef BEEBIUM_SERVER_BUILTIN_EXTENSIONS_HPP
#define BEEBIUM_SERVER_BUILTIN_EXTENSIONS_HPP

// Built-in extensions: extensions compiled directly into the server
// rather than loaded from an on-disk plugin.
//
// Anything that *could* be a plugin should be. Entries here describe
// extensions that the server has link-time knowledge of -- typically
// because ServerMain reaches into their type via dynamic_cast or
// because they PUBLIC-link libraries (like beebium_service) that
// cannot safely coexist with plugin copies. The acorn-65c02-coprocessor
// used to be here for exactly that reason; it is now a plugin, reached
// through the abstract CoprocessorExtension interface, and no longer
// appears in this table.

#include "AunEconetTransportExtension.hpp"
#include "FilePrinterExtension.hpp"
#include "JobFilePrinterExtension.hpp"
#include "HostSerialExtension.hpp"
#include "beebium/extension/Extension.hpp"
#include "beebium/extension/ExtensionManifest.hpp"

#include <functional>
#include <memory>
#include <string_view>
#include <stdexcept>
#include <vector>

namespace beebium::builtin_extensions {

struct Entry {
    ExtensionManifest manifest;
    std::function<std::unique_ptr<Extension>()> factory;
};

namespace detail {

inline std::vector<Entry> make_entries() {
    std::vector<Entry> result;

    // AUN UDP econet transport.
    //
    // Built-in rather than a plugin primarily for CLI-test ergonomics:
    // many parse_start_arguments tests use a synthetic argv
    // (argv[0] = "beebium") which cannot auto-discover plugins via
    // <exe-dir>/extensions/ lookup. Making AUN a plugin would need
    // those tests to pre-populate an extension directory, which is a
    // non-trivial refactor. A future test-side cleanup could unblock
    // conversion -- the runtime auto-discovery is in place and would
    // work identically for an AUN plugin.
    {
        ExtensionManifest m;
        m.name = "aun";
        m.display_name = "AUN";
        m.description =
            "Connects the Econet port to other machines over the network using AUN "
            "(Acorn Universal Networking).";
        m.cli_name = "aun";
        m.extension_kind = "econet-transport";
        m.parameters.push_back(
            {"port", "string",
             "The UDP port this machine listens on for other AUN stations. Real AUN "
             "machines use 32768; several Beebium instances on one computer each need "
             "a different port. 0 lets the computer choose one, and 'none' keeps this "
             "machine off the network.",
             -1, false, false, "32768"});
        m.parameters.push_back(
            {"net", "integer",
             "The Econet net this machine is on. Leave it at 0 unless it joins an AUN "
             "network whose nets are numbered.",
             -1, false, false, "0"});
        // map and subnet come from the extension, with the fields and item
        // formats it reads their entries by (map_parameter, subnet_parameter).
        // In the Network sidebar's order: a configuration UI lists them as
        // declared.
        m.parameters.push_back(AunEconetTransportExtension::map_parameter());
        m.parameters.push_back(AunEconetTransportExtension::subnet_parameter());
        m.parameters.push_back(
            {"map-file", "filepath",
             "The file of AUN stations shared by every Beebium instance on this computer, "
             "which the Network sidebar edits. 'none' uses no map file.",
             -1, false, false, ""});
        m.parameters.push_back(
            {"discovery", "string",
             "Whether this machine finds other Beebium machines on the local network, "
             "and lets them find it, by mDNS.",
             -1, false, false, "on"});
        auto label = [&](std::string_view key, std::string text) {
            for (auto& p : m.parameters) {
                if (p.key == key) p.label = std::move(text);
            }
        };
        label("port", "UDP port");
        label("net", "Net number");
        for (auto& p : m.parameters) {
            if (p.key == "net") {
                p.min = 0;
                p.max = 255;
            }
        }
        label("map-file", "Map file");
        for (auto& p : m.parameters) {
            if (p.key == "map-file") p.default_from = "aun-map-file";
        }
        label("discovery", "Discovery");
        for (auto& p : m.parameters) {
            if (p.key == "discovery") {
                p.choices = {{"on", "Announce and browse"},
                             {"announce", "Announce only"},
                             {"browse", "Browse only"},
                             {"off", "Off"}};
            }
        }
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new AunEconetTransportExtension()); }});
    }

    // Host serial bridge.
    //
    // Built-in for the same CLI-test ergonomics reason as AUN: --host-serial
    // must be recognised without a plugin directory. Attaches a HostSerial-
    // Endpoint to the serial port; when it claims the port, SerialService sees
    // is_occupied() and yields (reports status only).
    {
        ExtensionManifest m;
        m.name = "host-serial";
        m.display_name = "Host Serial";
        m.description =
            "Connects the serial port to a serial device or pseudo-terminal on this computer.";
        m.cli_name = "host-serial";
        m.extension_kind = "peripheral";
        m.attaches_to = {"serial-port"};
        m.parameters.push_back(
            {"mode", "string",
             "'pty' (create a pseudo-terminal) or 'device' (open an existing "
             "serial/pty device path)",
             -1, false, false, "pty"});
        m.parameters.push_back(
            {"path", "string",
             "pty: optional stable symlink to the pty slave; "
             "device: the serial device path to open",
             -1, false, false, ""});
        m.parameters.push_back(
            {"baud", "integer", "device line speed (ignored for pty)",
             -1, false, false, "19200"});
        m.parameters.push_back(
            {"tx_buffer", "integer",
             "transmit buffer size in bytes; the BBC's /CTS asserts at/above it",
             -1, false, false, "4096"});
        for (auto& p : m.parameters) {
            if (p.key == "mode") {
                p.label = "Mode";
                p.choices = {{"pty", "Pseudo-terminal"}, {"device", "Serial device"}};
            } else if (p.key == "path") {
                p.label = "Path";
                p.choices_from = "serial-ports";
            } else if (p.key == "baud") {
                p.label = "Baud rate";
            } else if (p.key == "tx_buffer") {
                p.label = "Transmit buffer (bytes)";
            }
        }
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new HostSerialExtension()); }});
    }

    // File printer.
    //
    // Built-in so that --file-printer works without a plugin directory, so
    // that presets can name it unconditionally, and because it is the test
    // oracle for everything else on the printer port. Attaches a
    // CentronicsReceiver to the printer port and spools what it receives
    // into a file. See docs/printer-port.md.
    {
        ExtensionManifest m;
        m.name = "file-printer";
        m.display_name = "File Printer";
        m.description =
            "Spools everything the BBC sends to its printer port into a file on the host.";
        m.cli_name = "file-printer";
        m.extension_kind = "peripheral";
        m.attaches_to = {"printer-port"};
        m.parameters.push_back(
            {"path", "filepath", "file to write; created or truncated at attach",
             -1, true, false, ""});
        m.parameters.push_back(
            {"line_ending", "string",
             "'raw' keeps the bytes as sent; 'lf' and 'crlf' translate the "
             "Beeb's CR line ends",
             -1, false, false, "raw"});
        m.parameters.push_back(
            {"buffer", "integer", "receiver queue depth in bytes",
             -1, false, false, "256"});
        m.parameters.push_back(
            {"online", "boolean",
             "start online; false models a printer that is switched off",
             -1, false, false, "true"});
        for (auto& p : m.parameters) {
            if (p.key == "path") {
                p.label = "File";
            } else if (p.key == "line_ending") {
                p.label = "Line ending";
                p.choices = {{"raw", "As sent"},
                             {"lf", "LF"},
                             {"crlf", "CR LF"}};
            } else if (p.key == "buffer") {
                p.label = "Buffer (bytes)";
                p.min = 1;
            } else if (p.key == "online") {
                p.label = "Online";
            }
        }
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new FilePrinterExtension()); }});
    }

    // Job file printer.
    //
    // Built-in for the same reasons as file-printer: --job-file-printer works
    // without a plugin directory and presets can name it. Writes one file per
    // print job, a job ending after `idle` seconds of emulated quiet, which
    // is the replay record a printer emulator wants. See
    // docs/printer-port.md.
    {
        ExtensionManifest m;
        m.name = "job-file-printer";
        m.display_name = "Job File Printer";
        m.description =
            "Writes what the BBC prints as one file per print job, a job ending after a "
            "quiet period.";
        m.cli_name = "job-file-printer";
        m.extension_kind = "peripheral";
        m.attaches_to = {"printer-port"};
        m.parameters.push_back(
            {"directory", "dirpath", "directory to write job files into; created if missing",
             -1, true, false, ""});
        m.parameters.push_back(
            {"idle", "integer",
             "seconds of quiet, in emulated time, that end a job",
             -1, false, false, "5"});
        m.parameters.push_back(
            {"line_ending", "string",
             "'raw' keeps the bytes as sent; 'lf' and 'crlf' translate the "
             "Beeb's CR line ends",
             -1, false, false, "raw"});
        m.parameters.push_back(
            {"buffer", "integer", "receiver queue depth in bytes",
             -1, false, false, "256"});
        m.parameters.push_back(
            {"online", "boolean",
             "start online; false models a printer that is switched off",
             -1, false, false, "true"});
        for (auto& p : m.parameters) {
            if (p.key == "directory") {
                p.label = "Directory";
            } else if (p.key == "idle") {
                p.label = "Job ends after (seconds)";
                p.min = 1;
            } else if (p.key == "line_ending") {
                p.label = "Line ending";
                p.choices = {{"raw", "As sent"},
                             {"lf", "LF"},
                             {"crlf", "CR LF"}};
            } else if (p.key == "buffer") {
                p.label = "Buffer (bytes)";
                p.min = 1;
            } else if (p.key == "online") {
                p.label = "Online";
            }
        }
        result.push_back({std::move(m),
                          [] { return std::unique_ptr<Extension>(
                              new JobFilePrinterExtension()); }});
    }

    // Every manifest passes the checks a plugin's does when it is read; a
    // malformed built-in is a programming error.
    for (auto& e : result) {
        for (const auto& p : e.manifest.parameters) {
            if (auto err = check_item_format(p)) {
                throw std::logic_error("built-in extension '" + e.manifest.name + "': " + *err);
            }
        }
        if (auto err = apply_parameter_groups(e.manifest)) {
            throw std::logic_error("built-in extension '" + e.manifest.name + "': " + *err);
        }
    }

    return result;
}

}  // namespace detail

// Returns the table of built-in extensions. The list is constructed on
// first call and cached for the program's lifetime.
inline const std::vector<Entry>& entries() {
    static const std::vector<Entry> table = detail::make_entries();
    return table;
}

// Look up an entry by canonical extension name (manifest.name).
// Returns nullptr if no built-in by that name exists.
inline const Entry* find(std::string_view name) {
    for (const auto& e : entries()) {
        if (e.manifest.name == name) return &e;
    }
    return nullptr;
}

}  // namespace beebium::builtin_extensions

#endif  // BEEBIUM_SERVER_BUILTIN_EXTENSIONS_HPP
