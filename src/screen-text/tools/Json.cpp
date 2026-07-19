// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#include "Json.hpp"

#include <cstdio>

namespace screentext::tools {

namespace {

void append_rect(std::string& out, const Rect& rect)
{
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer),
                  "{\"x\":%zu,\"y\":%zu,\"width\":%zu,\"height\":%zu}", rect.x,
                  rect.y, rect.width, rect.height);
    out += buffer;
}

void append_number(std::string& out, std::size_t value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%zu", value);
    out += buffer;
}

void append_cell(std::string& out, const Cell& cell)
{
    out += "{\"bounds\":";
    append_rect(out, cell.bounds);

    out += ",\"matched\":";
    out += cell.matched() ? "true" : "false";

    // Codepoint and colours are reported only when there is a match. An
    // unmatched cell says so rather than reporting zeroes that might be read
    // as a character drawn in black on black.
    if (cell.matched()) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer),
                      ",\"codepoint\":%u,\"foreground\":%u,\"background\":%u",
                      static_cast<unsigned>(cell.codepoint),
                      static_cast<unsigned>(cell.foreground),
                      static_cast<unsigned>(cell.background));
        out += buffer;
        out += ",\"glyph_set\":";
        out += json_string(cell.glyph_set);

        // Reported only when there is a choice, so that its presence in the
        // output means something rather than being noise on every cell.
        if (cell.ambiguous()) {
            out += ",\"alternatives\":[";
            for (std::size_t index = 0; index < cell.alternatives.size();
                 ++index) {
                if (index > 0) {
                    out += ",";
                }
                char buffer[32];
                std::snprintf(buffer, sizeof(buffer), "%u",
                              static_cast<unsigned>(cell.alternatives[index]));
                out += buffer;
            }
            out += "]";
        }
    }

    out += ",\"offset\":";
    out += cell.offset ? "true" : "false";
    out += "}";
}

} // namespace

std::string json_string(const std::string& value)
{
    std::string out = "\"";
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (character < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04X", character);
                out += buffer;
            } else {
                // Bytes at or above 0x80 are passed through: the text is
                // already UTF-8, which is what JSON wants.
                out += static_cast<char>(character);
            }
            break;
        }
    }
    out += "\"";
    return out;
}

std::string to_json(const Result& result)
{
    std::string out = "{\n  \"text\": ";
    out += json_string(result.text());

    out += ",\n  \"total_cells\": ";
    append_number(out, result.total_cells);
    out += ",\n  \"unmatched_cells\": ";
    append_number(out, result.unmatched_cells);
    out += ",\n  \"ambiguous_cells\": ";
    append_number(out, result.ambiguous_cells);

    out += ",\n  \"runs\": [";
    for (std::size_t index = 0; index < result.runs.size(); ++index) {
        const Run& run = result.runs[index];
        out += index > 0 ? ",\n    {" : "\n    {";

        out += "\"text\": ";
        out += json_string(run.text);
        out += ", \"bounds\": ";
        append_rect(out, run.bounds);
        out += ", \"unmatched_cells\": ";
        append_number(out, run.unmatched_cells());
        out += ", \"ambiguous_cells\": ";
        append_number(out, run.ambiguous_cells());

        out += ", \"cells\": [";
        for (std::size_t cell_index = 0; cell_index < run.cells.size();
             ++cell_index) {
            if (cell_index > 0) {
                out += ", ";
            }
            append_cell(out, run.cells[cell_index]);
        }
        out += "]}";
    }
    out += result.runs.empty() ? "]" : "\n  ]";
    out += "\n}\n";
    return out;
}

} // namespace screentext::tools
