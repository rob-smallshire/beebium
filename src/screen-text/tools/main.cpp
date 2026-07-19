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

// screentext: read text out of images, given sets of glyphs.
//
// The same capability as the library, without linking, and for offline
// pipelines over directories of screenshots.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ImageIo.hpp"
#include "Json.hpp"
#include "screentext/ScreenText.hpp"

namespace {

using namespace screentext;

const char* const USAGE =
    "Usage: screentext read <image> [options]\n"
    "\n"
    "Read text from an image by matching character cells against glyphs.\n"
    "\n"
    "Options:\n"
    "  --selection X,Y,W,H   region to read, in image pixels (default: all)\n"
    "  --glyphs FILE         a glyph set to add; repeatable, later sets win\n"
    "  --builtin NAME        use this built-in set instead of the default\n"
    "  --no-builtin          use only the glyph sets given with --glyphs\n"
    "  --cell WxH            character cell size in pixels (default: 8x8)\n"
    "  --pitch WxH           cell-to-cell step (default: the cell size).\n"
    "                        MODE 3 and MODE 6 need --cell 8x8 --pitch 8x10:\n"
    "                        an 8-scanline glyph on a 10-scanline row, the\n"
    "                        gap blanked to black whatever the palette says\n"
    "  --origin X,Y          where the character grid starts (default: 0,0)\n"

    "  --search MODE         aligned (default) or offset\n"
    "  --no-inverted         do not match inverse video\n"
    "  --format FORMAT       text (default) or json\n"
    "  --list-builtin        list the built-in glyph sets and exit\n"
    "  -h, --help            show this help and exit\n"
    "\n"
    "Glyph set files hold one glyph per line:\n"
    "  <codepoint> <byte> <byte> ...\n"
    "with the codepoint in hex (U+00A3, 0xA3) or decimal, and one byte per\n"
    "row of the glyph. Blank lines and lines starting with # are ignored.\n"
    "\n"
    "Exit status is 0 whenever the image was read, whatever was found.\n"
    "Finding no text is not a failure.\n";

// A parsed command line. Kept separate from acting on it so that the parsing
// is testable and the failure messages are all in one place.
struct Arguments {
    std::string image_filepath;
    std::vector<std::string> glyph_filepaths;
    std::string builtin_name = "acorn-mos-1.20";
    bool use_builtin = true;
    std::size_t cell_width = 8;
    std::size_t cell_height = 8;
    std::size_t column_pitch = 0;
    std::size_t row_pitch = 0;
    std::size_t origin_x = 0;
    std::size_t origin_y = 0;
    Search search = Search::AlignedOnly;
    bool match_inverted = true;
    bool json = false;
};

bool parse_size_list(const std::string& value,
                     char separator,
                     std::vector<unsigned long long>& out)
{
    out.clear();
    std::stringstream stream(value);
    std::string field;
    while (std::getline(stream, field, separator)) {
        if (field.empty()) {
            return false;
        }
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(field.c_str(), &end, 10);
        if (end == nullptr || *end != '\0') {
            return false;
        }
        out.push_back(parsed);
    }
    return !out.empty();
}

bool parse_codepoint(const std::string& token, char32_t& codepoint)
{
    std::string text = token;
    int base = 10;
    if (text.size() > 2 && (text.compare(0, 2, "U+") == 0
                            || text.compare(0, 2, "u+") == 0)) {
        text = text.substr(2);
        base = 16;
    } else if (text.size() > 2 && text.compare(0, 2, "0x") == 0) {
        text = text.substr(2);
        base = 16;
    } else if (!text.empty() && text[0] == '&') {
        text = text.substr(1);
        base = 16;
    }

    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, base);
    if (text.empty() || end == nullptr || *end != '\0') {
        return false;
    }
    codepoint = static_cast<char32_t>(parsed);
    return true;
}

// Load a glyph set from a text file. Deliberately a plain format: a caller
// generating one from a font, or from RAM, should not need a library to do it.
bool load_glyph_set(const std::string& filepath,
                    GlyphSet& set,
                    std::string& error)
{
    std::ifstream stream(filepath);
    if (!stream) {
        error = "cannot open glyph file " + filepath;
        return false;
    }

    set.name = filepath;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;

        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }

        std::istringstream fields(line);
        std::string token;
        if (!(fields >> token)) {
            continue; // blank
        }

        char32_t codepoint = 0;
        if (!parse_codepoint(token, codepoint)) {
            error = filepath + ":" + std::to_string(line_number)
                + ": cannot parse codepoint '" + token + "'";
            return false;
        }

        std::vector<std::uint8_t> rows;
        while (fields >> token) {
            char32_t byte = 0;
            if (!parse_codepoint(token, byte) || byte > 0xFF) {
                error = filepath + ":" + std::to_string(line_number)
                    + ": cannot parse glyph row '" + token + "'";
                return false;
            }
            rows.push_back(static_cast<std::uint8_t>(byte));
        }

        if (rows.empty()) {
            error = filepath + ":" + std::to_string(line_number)
                + ": glyph has no rows";
            return false;
        }

        set.glyphs.push_back(Glyph(codepoint, Bitmap::from_rows(rows)));
    }

    return true;
}

int fail(const std::string& message)
{
    std::cerr << "screentext: " << message << "\n";
    return 2;
}

int parse_arguments(const std::vector<std::string>& argv,
                    Arguments& arguments,
                    std::optional<Rect>& selection,
                    bool& handled)
{
    handled = false;

    // A value-taking option, checked in one place so that a missing value is
    // reported rather than silently swallowing the next option.
    const auto value_for = [&argv](std::size_t& index, const char* name,
                                   std::string& out) {
        if (index + 1 >= argv.size()) {
            std::cerr << "screentext: " << name << " needs a value\n";
            return false;
        }
        out = argv[++index];
        return true;
    };

    for (std::size_t index = 1; index < argv.size(); ++index) {
        const std::string& argument = argv[index];
        std::string value;

        if (argument == "-h" || argument == "--help") {
            std::cout << USAGE;
            handled = true;
            return 0;
        }
        if (argument == "--list-builtin") {
            for (const std::string& name : builtin_glyph_set_names()) {
                std::cout << name << "\n";
            }
            handled = true;
            return 0;
        }
        if (argument == "--no-inverted") {
            arguments.match_inverted = false;
        } else if (argument == "--no-builtin") {
            arguments.use_builtin = false;
        } else if (argument == "--glyphs") {
            if (!value_for(index, "--glyphs", value)) {
                return 2;
            }
            arguments.glyph_filepaths.push_back(value);
        } else if (argument == "--builtin") {
            if (!value_for(index, "--builtin", value)) {
                return 2;
            }
            arguments.builtin_name = value;
        } else if (argument == "--format") {
            if (!value_for(index, "--format", value)) {
                return 2;
            }
            if (value == "json") {
                arguments.json = true;
            } else if (value == "text") {
                arguments.json = false;
            } else {
                return fail("unknown format '" + value + "'; use text or json");
            }
        } else if (argument == "--search") {
            if (!value_for(index, "--search", value)) {
                return 2;
            }
            if (value == "aligned") {
                arguments.search = Search::AlignedOnly;
            } else if (value == "offset") {
                arguments.search = Search::IncludeOffset;
            } else {
                return fail("unknown search '" + value
                            + "'; use aligned or offset");
            }
        } else if (argument == "--selection") {
            if (!value_for(index, "--selection", value)) {
                return 2;
            }
            std::vector<unsigned long long> parts;
            if (!parse_size_list(value, ',', parts) || parts.size() != 4) {
                return fail("--selection needs X,Y,W,H");
            }
            selection = Rect{static_cast<std::size_t>(parts[0]),
                             static_cast<std::size_t>(parts[1]),
                             static_cast<std::size_t>(parts[2]),
                             static_cast<std::size_t>(parts[3])};
        } else if (argument == "--cell") {
            if (!value_for(index, "--cell", value)) {
                return 2;
            }
            std::vector<unsigned long long> parts;
            if (!parse_size_list(value, 'x', parts) || parts.size() != 2
                || parts[0] == 0 || parts[1] == 0) {
                return fail("--cell needs WxH, both non-zero");
            }
            arguments.cell_width = static_cast<std::size_t>(parts[0]);
            arguments.cell_height = static_cast<std::size_t>(parts[1]);
        } else if (argument == "--pitch") {
            if (!value_for(index, "--pitch", value)) {
                return 2;
            }
            std::vector<unsigned long long> parts;
            if (!parse_size_list(value, 'x', parts) || parts.size() != 2
                || parts[0] == 0 || parts[1] == 0) {
                return fail("--pitch needs WxH, both non-zero");
            }
            arguments.column_pitch = static_cast<std::size_t>(parts[0]);
            arguments.row_pitch = static_cast<std::size_t>(parts[1]);
        } else if (argument == "--origin") {
            if (!value_for(index, "--origin", value)) {
                return 2;
            }
            std::vector<unsigned long long> parts;
            if (!parse_size_list(value, ',', parts) || parts.size() != 2) {
                return fail("--origin needs X,Y");
            }
            arguments.origin_x = static_cast<std::size_t>(parts[0]);
            arguments.origin_y = static_cast<std::size_t>(parts[1]);
        } else if (!argument.empty() && argument[0] == '-') {
            return fail("unknown option '" + argument + "'");
        } else if (arguments.image_filepath.empty()) {
            arguments.image_filepath = argument;
        } else {
            return fail("unexpected argument '" + argument + "'");
        }
    }

    return 0;
}

int run_read(const std::vector<std::string>& argv)
{
    Arguments arguments;
    std::optional<Rect> selection;
    bool handled = false;

    if (const int status = parse_arguments(argv, arguments, selection, handled);
        status != 0 || handled) {
        return status;
    }

    if (arguments.image_filepath.empty()) {
        return fail("no image given\n\n" + std::string(USAGE));
    }

    std::vector<GlyphSet> glyph_sets;
    if (arguments.use_builtin) {
        const GlyphSet* builtin = find_builtin_glyph_set(arguments.builtin_name);
        if (builtin == nullptr) {
            return fail("no built-in glyph set named '" + arguments.builtin_name
                        + "'; try --list-builtin");
        }
        glyph_sets.push_back(*builtin);
    }

    // Supplied sets come last, so they take precedence over the built-in one.
    for (const std::string& filepath : arguments.glyph_filepaths) {
        GlyphSet set;
        std::string error;
        if (!load_glyph_set(filepath, set, error)) {
            return fail(error);
        }
        glyph_sets.push_back(std::move(set));
    }

    Image image;
    std::string error;
    if (!tools::load_image(arguments.image_filepath, image, error)) {
        return fail(error);
    }

    Band band;
    band.top = 0;
    band.bottom = image.height;
    band.cell_width = arguments.cell_width;
    band.cell_height = arguments.cell_height;
    band.column_pitch = arguments.column_pitch;
    band.row_pitch = arguments.row_pitch;
    band.origin_x = arguments.origin_x;
    band.origin_y = arguments.origin_y;

    Options options;
    options.search = arguments.search;
    options.match_inverted = arguments.match_inverted;
    options.selection = selection;

    const Result result = read(image, {band}, glyph_sets, options);

    if (arguments.json) {
        std::cout << tools::to_json(result);
    } else {
        // Nothing but the text, so this composes in a shell pipeline.
        const std::string text = result.text();
        if (!text.empty()) {
            std::cout << text << "\n";
        }
    }

    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv, argv + argc);

    if (args.size() < 2) {
        std::cerr << USAGE;
        return 2;
    }

    const std::string& command = args[1];
    if (command == "-h" || command == "--help") {
        std::cout << USAGE;
        return 0;
    }
    if (command == "read") {
        return run_read(std::vector<std::string>(args.begin() + 1, args.end()));
    }

    std::cerr << "screentext: unknown command '" << command << "'\n\n" << USAGE;
    return 2;
}
