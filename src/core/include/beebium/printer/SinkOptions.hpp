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

#ifndef BEEBIUM_PRINTER_SINK_OPTIONS_HPP
#define BEEBIUM_PRINTER_SINK_OPTIONS_HPP

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace beebium::printer {

// What the sinks that write the printer's bytes to host files share: the
// translation of the Beeb's line ends and the parsing of the options every
// such sink takes (line_ending, buffer).

// How the Beeb's CR line ends are written to the file. The MOS sends CR for a
// newline and by default (*FX6,10) suppresses LF to the printer.
enum class LineEnding {
    Raw,   // the bytes as sent
    Lf,    // a lone CR becomes LF; a CR LF pair becomes LF
    CrLf,  // a lone CR becomes CR LF; a CR LF pair is left alone
};

// Translates a byte stream's line ends. Stateful across calls, since the LF
// of a CR LF pair arrives after its CR; one translator per output stream.
class LineEndingTranslator {
public:
    explicit LineEndingTranslator(LineEnding line_ending) : line_ending_(line_ending) {}

    LineEnding line_ending() const { return line_ending_; }

    // Pass `byte` through, calling put(uint8_t) for each byte to write.
    template <typename Put>
    void translate(uint8_t byte, Put&& put) {
        constexpr uint8_t CR = 0x0D;
        constexpr uint8_t LF = 0x0A;
        if (line_ending_ == LineEnding::Raw) {
            put(byte);
            return;
        }
        // The LF of a CR LF pair has already been written as part of the
        // translated CR, so it is swallowed.
        if (byte == LF && last_was_cr_) {
            last_was_cr_ = false;
            return;
        }
        last_was_cr_ = (byte == CR);
        if (byte == CR) {
            if (line_ending_ == LineEnding::Lf) {
                put(LF);
            } else {
                put(CR);
                put(LF);
            }
            return;
        }
        put(byte);
    }

    // Forget a pending CR, for a new output stream.
    void reset() { last_was_cr_ = false; }

private:
    LineEnding line_ending_;
    bool last_was_cr_ = false;
};

// The line_ending option, or raw when it is not given. Throws naming
// `extension_name` when the value is not one of raw, lf or crlf.
inline LineEnding parse_line_ending_option(std::string_view extension_name,
                                           std::optional<std::string_view> value) {
    const std::string text(value.value_or("raw"));
    if (text == "raw") return LineEnding::Raw;
    if (text == "lf") return LineEnding::Lf;
    if (text == "crlf") return LineEnding::CrLf;
    throw std::runtime_error(std::string(extension_name) + ": unknown line_ending '" + text +
                             "' (expected 'raw', 'lf' or 'crlf')");
}

// The buffer option (receiver queue depth in bytes), or `fallback` when it is
// not given. Throws naming `extension_name` unless it is a positive integer.
inline std::size_t parse_buffer_option(std::string_view extension_name,
                                       std::optional<std::string_view> value,
                                       std::size_t fallback = 256) {
    if (!value || value->empty()) {
        return fallback;
    }
    std::size_t parsed = 0;
    auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (ec != std::errc{} || ptr != value->data() + value->size() || parsed == 0) {
        throw std::runtime_error(std::string(extension_name) +
                                 ": buffer must be a positive integer, got '" +
                                 std::string(*value) + "'");
    }
    return parsed;
}

}  // namespace beebium::printer

#endif  // BEEBIUM_PRINTER_SINK_OPTIONS_HPP
