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

// The CLI, exercised through its own interface: run as a process, with its
// output parsed. Nothing here calls the library directly.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "Canvas.hpp"
#include "screentext/ScreenText.hpp"

using namespace screentext;
using screentext::testing::Canvas;

namespace {

struct Output {
    std::string stdout_text;
    int status = 0;
};

// Run the CLI and capture its standard output. Standard error is left to the
// terminal, so a failure is visible when a test does not expect one.
Output run(const std::string& arguments)
{
    const std::string command
        = std::string("\"") + SCREENTEXT_CLI_PATH + "\" " + arguments;

    Output output;
#ifdef _WIN32
    std::FILE* pipe = _popen(command.c_str(), "r");
#else
    std::FILE* pipe = popen(command.c_str(), "r");
#endif
    REQUIRE(pipe != nullptr);

    std::array<char, 4096> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)
           != nullptr) {
        output.stdout_text += buffer.data();
    }

#ifdef _WIN32
    output.status = _pclose(pipe);
#else
    const int closed = pclose(pipe);
    output.status = WIFEXITED(closed) ? WEXITSTATUS(closed) : -1;
#endif
    return output;
}

std::string temporary_filepath(const std::string& name)
{
    return std::string(SCREENTEXT_TEST_SCRATCH_DIR) + "/" + name;
}

// Screenshots captured from a real machine and committed, so the library is
// exercised against genuine output as well as its own idea of it.
std::string fixture_filepath(const std::string& name)
{
    return std::string(SCREENTEXT_TEST_FIXTURES_DIR) + "/" + name;
}

// Write an image as binary PGM, which stb decodes, so a test can build its
// input with the same Canvas the library tests use.
void write_pgm(const std::string& filepath, const Image& image)
{
    std::ofstream stream(filepath, std::ios::binary);
    REQUIRE(stream.good());
    stream << "P5\n" << image.width << " " << image.height << "\n255\n";
    for (const std::uint8_t pixel : image.pixels) {
        // Scale so that the glyph stands out from the background: any value
        // other than the background reads as foreground either way.
        const char value = static_cast<char>(pixel == 0 ? 0 : 255);
        stream.write(&value, 1);
    }
    stream.close();
}

const GlyphSet& acorn()
{
    return builtin_glyph_set("acorn-mos-1.20");
}

std::string write_listing_image(const std::string& name,
                                const std::vector<std::string>& lines)
{
    std::size_t widest = 0;
    for (const std::string& line : lines) {
        widest = line.size() > widest ? line.size() : widest;
    }

    Canvas canvas(widest * 8, lines.size() * 8);
    for (std::size_t index = 0; index < lines.size(); ++index) {
        canvas.stamp_text(0, index * 8, lines[index], acorn());
    }

    const std::string filepath = temporary_filepath(name);
    write_pgm(filepath, canvas.image());
    return filepath;
}

} // namespace

TEST_CASE("The CLI reads text from an image")
{
    const std::string filepath
        = write_listing_image("hello.pgm", {"HELLO WORLD"});

    const Output output = run("read \"" + filepath + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text == "HELLO WORLD\n");
}

TEST_CASE("Text format emits nothing but the text")
{
    // So that the CLI composes in a shell pipeline: no banner, no counts, no
    // diagnostics on standard output.
    const std::string filepath
        = write_listing_image("plain.pgm", {"10 PRINT", "20 GOTO 10"});

    const Output output = run("read \"" + filepath + "\" --format text");

    CHECK(output.status == 0);
    CHECK(output.stdout_text == "10 PRINT\n20 GOTO 10\n");
}

TEST_CASE("An image with no text produces no output and still succeeds")
{
    // Finding no text is not a failure.
    Canvas canvas(64, 32);
    const std::string filepath = temporary_filepath("blank.pgm");
    write_pgm(filepath, canvas.image());

    const Output output = run("read \"" + filepath + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.empty());
}

TEST_CASE("JSON format reports the structure, including what was unmatched")
{
    Canvas canvas(8 * 3, 8);
    canvas.stamp_text(0, 0, "A", acorn());
    canvas.set_pixel(8, 0, 1); // unreadable
    canvas.stamp_text(16, 0, "B", acorn());

    const std::string filepath = temporary_filepath("mixed.pgm");
    write_pgm(filepath, canvas.image());

    const Output output = run("read \"" + filepath + "\" --format json");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("\"text\": \"A B\"") != std::string::npos);
    CHECK(output.stdout_text.find("\"unmatched_cells\": 1") != std::string::npos);
    CHECK(output.stdout_text.find("\"matched\":false") != std::string::npos);
    CHECK(output.stdout_text.find("\"total_cells\": 3") != std::string::npos);
}

TEST_CASE("A selection restricts what is read")
{
    const std::string filepath
        = write_listing_image("selection.pgm", {"ABCDEFGH"});

    const Output output
        = run("read \"" + filepath + "\" --selection 16,0,24,8");

    CHECK(output.status == 0);
    CHECK(output.stdout_text == "CDE\n");
}

TEST_CASE("Inverse matching can be turned off from the command line")
{
    Canvas canvas(8 * 2, 8);
    canvas.stamp_inverted(0, 0, Canvas::glyph_for(acorn(), U'H'));
    const std::string filepath = temporary_filepath("inverse.pgm");
    write_pgm(filepath, canvas.image());

    const Output matched = run("read \"" + filepath + "\"");
    CHECK(matched.stdout_text.find('H') != std::string::npos);

    const Output not_matched = run("read \"" + filepath + "\" --no-inverted");
    CHECK(not_matched.status == 0);
    CHECK(not_matched.stdout_text.find('H') == std::string::npos);
}

TEST_CASE("A supplied glyph set is loaded and takes precedence")
{
    Canvas canvas(8, 8);
    canvas.stamp_text(0, 0, "A", acorn());
    const std::string image_filepath = temporary_filepath("override.pgm");
    write_pgm(image_filepath, canvas.image());

    const std::string glyph_filepath = temporary_filepath("override.glyphs");
    {
        std::ofstream stream(glyph_filepath);
        REQUIRE(stream.good());
        stream << "# the bitmap of A, meaning Z\n";
        stream << "U+005A 0x3C 0x66 0x66 0x7E 0x66 0x66 0x66 0x00\n";
    }

    const Output plain = run("read \"" + image_filepath + "\"");
    CHECK(plain.stdout_text == "A\n");

    const Output overridden = run("read \"" + image_filepath + "\" --glyphs \""
                                  + glyph_filepath + "\"");
    CHECK(overridden.status == 0);
    CHECK(overridden.stdout_text == "Z\n");
}

TEST_CASE("The built-in glyph sets can be listed")
{
    const Output output = run("read --list-builtin");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("acorn-mos-1.20") != std::string::npos);
}

TEST_CASE("A missing image is an error, not an empty result")
{
    const Output output = run("read /no/such/image.png 2>/dev/null");
    CHECK(output.status != 0);
}

TEST_CASE("An unreadable glyph file is an error")
{
    const std::string filepath = write_listing_image("needs-glyphs.pgm", {"A"});
    const Output output
        = run("read \"" + filepath + "\" --glyphs /no/such/glyphs 2>/dev/null");
    CHECK(output.status != 0);
}

TEST_CASE("An unknown option is refused rather than ignored")
{
    const std::string filepath = write_listing_image("options.pgm", {"A"});
    const Output output
        = run("read \"" + filepath + "\" --wat 2>/dev/null");
    CHECK(output.status != 0);
}

TEST_CASE("Help is available and succeeds")
{
    const Output output = run("--help");
    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("Usage: screentext") != std::string::npos);
}

TEST_CASE("Cell geometry and grid origin can be set from the command line")
{
    // Nothing about 8x8 is baked in: the CLI exposes the same inputs the
    // library takes.
    Canvas canvas(8 * 3, 8);
    canvas.stamp_text(4, 0, "AB", acorn()); // off the grid by four pixels

    const std::string filepath = temporary_filepath("origin.pgm");
    write_pgm(filepath, canvas.image());

    const Output aligned = run("read \"" + filepath + "\"");
    CHECK(aligned.stdout_text.find("AB") == std::string::npos);

    const Output shifted
        = run("read \"" + filepath + "\" --origin 4,0 --cell 8x8");
    CHECK(shifted.status == 0);
    CHECK(shifted.stdout_text == "AB\n");
}

// Real screenshots, captured from a running Model B. These are the cases the
// synthetic fixtures cannot vouch for: that the geometry a real machine
// produces is what the library expects, and that a whole screen of genuine
// output matches byte for byte rather than nearly.

TEST_CASE("A real MODE 4 BASIC listing is read from a screenshot")
{
    const Output output
        = run("read \"" + fixture_filepath("mode4-listing.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text ==
          ">LIST\n"
          "10 REM SCREEN TEXT\n"
          "20 FOR I%=1 TO 3\n"
          "30 PRINT \"LINE \";I%\n"
          "40 NEXT I%\n"
          "50 END\n"
          ">_\n");
}

TEST_CASE("Every cell of a real MODE 4 screen matches, none unread")
{
    // The claim the whole design rests on: a character cell on a BBC screen
    // matches its font glyph exactly. A single unmatched cell here would mean
    // that is not true, or that the geometry is wrong.
    const Output output = run("read \"" + fixture_filepath("mode4-listing.png")
                              + "\" --format json");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("\"total_cells\": 1280") != std::string::npos);
    CHECK(output.stdout_text.find("\"unmatched_cells\": 0") != std::string::npos);
    CHECK(output.stdout_text.find("\"matched\":false") == std::string::npos);
}

TEST_CASE("Real inverse video is read, and its spaces are spaces")
{
    // Captured with COLOUR 129:COLOUR 0, which is how the BBC produces
    // inverse text. The space inside "INVERSE TEXT" is a solid cell, and it
    // must come back as a space rather than as character 127.
    const Output output
        = run("read \"" + fixture_filepath("mode4-inverse.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("\nINVERSE TEXT\n") != std::string::npos);
}

TEST_CASE("Turning inverse matching off leaves real inverse text unread")
{
    const Output output = run("read \"" + fixture_filepath("mode4-inverse.png")
                              + "\" --no-inverted");

    CHECK(output.status == 0);
    // The line the program printed in inverse video is gone. The echo of the
    // command that printed it survives, because that was typed in normal
    // video, so the whole screenshot is not expected to be empty.
    CHECK(output.stdout_text.find("\nINVERSE TEXT\n") == std::string::npos);
    CHECK(output.stdout_text.find("PRINT \"INVERSE TEXT") != std::string::npos);
}

TEST_CASE("A selection over a real screenshot clips to the cells it covers")
{
    // Three cells wide, one row down: "10 " of the first listing line.
    const Output output = run("read \"" + fixture_filepath("mode4-listing.png")
                              + "\" --selection 24,8,24,8");

    CHECK(output.status == 0);
    CHECK(output.stdout_text == "10\n");
}
