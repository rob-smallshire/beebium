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
#include "ScreentextCliPath.hpp"
#include "screentext/ScreenText.hpp"

using namespace screentext;
using screentext::testing::Canvas;

namespace {

struct Output {
    std::string stdout_text;
    int status = 0;
};

// Discard standard error, for the cases that expect the CLI to complain.
// cmd.exe spells its null device differently from a POSIX shell.
const char* discard_stderr()
{
#ifdef _WIN32
    return " 2>NUL";
#else
    return " 2>/dev/null";
#endif
}

// Run the CLI and capture its standard output. Standard error is left to the
// terminal unless a test discards it, so a failure is visible when a test does
// not expect one.
Output run(const std::string& arguments)
{
    std::string command
        = std::string("\"") + SCREENTEXT_CLI_PATH + "\" " + arguments;

#ifdef _WIN32
    // cmd.exe strips the outermost pair of quotes from the command it is
    // given, which would unquote the executable path and break on the spaces
    // in it. An extra enclosing pair survives that.
    command = "\"" + command + "\"";
#endif

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

TEST_CASE("Text drawn dark on light is read like any other")
{
    Canvas canvas(8 * 2, 8);
    canvas.stamp_inverted(0, 0, Canvas::glyph_for(acorn(), U'H'));
    const std::string filepath = temporary_filepath("inverse.pgm");
    write_pgm(filepath, canvas.image());

    const Output output = run("read \"" + filepath + "\"");
    CHECK(output.status == 0);
    CHECK(output.stdout_text.find('H') != std::string::npos);
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
    const Output output = run(std::string("read /no/such/image.png") + discard_stderr());
    CHECK(output.status != 0);
}

TEST_CASE("An unreadable glyph file is an error")
{
    const std::string filepath = write_listing_image("needs-glyphs.pgm", {"A"});
    const Output output
        = run("read \"" + filepath + "\" --glyphs /no/such/glyphs"
              + discard_stderr());
    CHECK(output.status != 0);
}

TEST_CASE("An unknown option is refused rather than ignored")
{
    const std::string filepath = write_listing_image("options.pgm", {"A"});
    const Output output
        = run("read \"" + filepath + "\" --wat" + discard_stderr());
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

TEST_CASE("JSON reports the colours a cell was drawn from")
{
    // The screenshot has a line printed dark-on-light below a command echoed
    // light-on-dark, so both arrangements appear, and each cell says which
    // colour its glyph was drawn in rather than claiming an orientation.
    const Output output = run("read \"" + fixture_filepath("mode4-inverse.png")
                              + "\" --format json");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("\"foreground\":") != std::string::npos);
    CHECK(output.stdout_text.find("\"background\":") != std::string::npos);
    CHECK(output.stdout_text.find("\"inverted\"") == std::string::npos);
}

TEST_CASE("A selection over a real screenshot clips to the cells it covers")
{
    // Three cells wide, one row down: "10 " of the first listing line.
    const Output output = run("read \"" + fixture_filepath("mode4-listing.png")
                              + "\" --selection 24,8,24,8");

    CHECK(output.status == 0);
    CHECK(output.stdout_text == "10\n");
}

// The corpus: every bitmap mode, in two variants.
//
// Imported from the emulator's own golden masters (see
// fixtures/import_golden_corpus.py). The testcard fills every character cell
// with a cycling pattern of characters 32 to 126, so a whole screen is
// checked rather than a line of it; the blue variant is the same screen after
// VDU 19,0,4,0,0,0.

namespace {

struct ModeGeometry {
    int mode;
    std::size_t columns;
    std::size_t rows;
    std::size_t row_pitch; // 10 where the display leaves a gap between rows
};

// MODE 3 and MODE 6 are 25-row text modes: an 8-scanline glyph on a
// 10-scanline pitch. Every mode is 8 pixels per column in logical pixel
// space, which is what makes one glyph set serve all of them.
const ModeGeometry MODES[] = {
    {0, 80, 32, 8},  {1, 40, 32, 8},  {2, 20, 32, 8}, {3, 80, 25, 10},
    {4, 40, 32, 8},  {5, 20, 32, 8},  {6, 40, 25, 10},
};

std::string corpus_filepath(int mode, const std::string& variant)
{
    return fixture_filepath("corpus/mode" + std::to_string(mode) + "-"
                            + variant + ".png");
}

std::string geometry_arguments(const ModeGeometry& geometry)
{
    return " --cell 8x8 --pitch 8x" + std::to_string(geometry.row_pitch);
}

void append_utf8(std::string& text, char32_t codepoint)
{
    if (codepoint < 0x80U) {
        text.push_back(static_cast<char>(codepoint));
    } else {
        text.push_back(static_cast<char>(0xC0U | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
    }
}

// Character 96 of the Acorn set is a pound sign, and the testcard's cycle
// runs right through it.
char32_t codepoint_of(std::size_t character)
{
    return character == 96 ? 0x00A3U : static_cast<char32_t>(character);
}

// Reproduce what the testcard puts on screen, independently of the library:
// a header row of digits, then rows that each begin with a digit and continue
// the cycle of characters 32 to 126. The last row is one character short, so
// that filling the screen does not scroll it.
std::string expected_testcard(std::size_t columns, std::size_t rows)
{
    const std::size_t max_col = columns - 1;
    const std::size_t max_row = rows - 1;

    std::vector<std::string> lines;

    std::string header;
    for (std::size_t index = 0; index <= max_col; ++index) {
        header.push_back(static_cast<char>('0' + index % 10));
    }
    lines.push_back(header);

    for (std::size_t y = 1; y <= max_row; ++y) {
        std::string line;
        line.push_back(static_cast<char>('0' + y % 10));
        const std::size_t count = (y == max_row) ? max_col - 1 : max_col;
        for (std::size_t x = 1; x <= count; ++x) {
            const std::size_t offset = ((y - 1) * max_col + (x - 1)) % 95;
            append_utf8(line, codepoint_of(32 + offset));
        }
        if (y == max_row) {
            // The cell the testcard leaves unfilled holds the cursor, which
            // the golden masters render steady. It is an underline on screen
            // and so it is an underline in the text: the library reads what
            // is there and has no idea what a cursor is.
            line.push_back('_');
        }
        lines.push_back(line);
    }

    // Runs are trimmed of leading and trailing blanks, so the expectation is
    // trimmed the same way before comparing.
    std::string text;
    for (const std::string& line : lines) {
        const std::size_t first = line.find_first_not_of(' ');
        if (first == std::string::npos) {
            continue;
        }
        const std::size_t last = line.find_last_not_of(' ');
        text += line.substr(first, last - first + 1);
        text.push_back('\n');
    }
    return text;
}

std::size_t json_number(const std::string& json, const std::string& key)
{
    const std::size_t at = json.find("\"" + key + "\": ");
    REQUIRE(at != std::string::npos);
    return static_cast<std::size_t>(
        std::strtoul(json.c_str() + at + key.size() + 4, nullptr, 10));
}

} // namespace

TEST_CASE("Every character cell of every mode's testcard is read")
{
    // The strongest statement the corpus can make: across all seven bitmap
    // modes, every cell of a full screen matches a glyph, and the text is
    // exactly what the testcard put there.
    for (const ModeGeometry& geometry : MODES) {
        const std::string label = "MODE " + std::to_string(geometry.mode);
        INFO(label);

        const Output output
            = run("read \"" + corpus_filepath(geometry.mode, "testcard")
                  + "\"" + geometry_arguments(geometry));

        CHECK(output.status == 0);
        CHECK(output.stdout_text == expected_testcard(geometry.columns,
                                                      geometry.rows));
    }
}

TEST_CASE("No cell of any mode's testcard is left unread")
{
    for (const ModeGeometry& geometry : MODES) {
        INFO("MODE " + std::to_string(geometry.mode));

        const Output output
            = run("read \"" + corpus_filepath(geometry.mode, "testcard")
                  + "\"" + geometry_arguments(geometry) + " --format json");

        CHECK(output.status == 0);
        CHECK(json_number(output.stdout_text, "total_cells")
              == geometry.columns * geometry.rows);
        CHECK(json_number(output.stdout_text, "unmatched_cells") == 0);
    }
}

TEST_CASE("A recoloured background is read in every mode")
{
    // VDU 19,0,4,0,0,0 turns the character background blue. Nothing here
    // says so: the two values in a cell are the glyph's colour and its
    // background, and which is which is worked out from the image.
    for (const ModeGeometry& geometry : MODES) {
        INFO("MODE " + std::to_string(geometry.mode));

        const Output output
            = run("read \"" + corpus_filepath(geometry.mode, "blue") + "\""
                  + geometry_arguments(geometry) + " --format json");

        CHECK(output.status == 0);
        CHECK(json_number(output.stdout_text, "total_cells")
              == geometry.columns * geometry.rows);
        CHECK(json_number(output.stdout_text, "unmatched_cells") == 0);
        CHECK(output.stdout_text.find("BBC Computer 32K") != std::string::npos);
        CHECK(output.stdout_text.find("VDU 19,0,4,0,0,0") != std::string::npos);
    }
}

TEST_CASE("In MODE 3 and MODE 6 the gap between rows must not be sampled")
{
    // The blanked scanlines between character rows stay black however the
    // palette is programmed. Treating them as part of the cell costs nothing
    // while the background is also black, and costs everything once it is
    // not: everything on the screen stops matching at once.
    for (const int mode : {3, 6}) {
        INFO("MODE " + std::to_string(mode));
        const std::string image = corpus_filepath(mode, "blue");

        const Output correct
            = run("read \"" + image + "\" --cell 8x8 --pitch 8x10"
                  + " --format json");
        CHECK(json_number(correct.stdout_text, "unmatched_cells") == 0);

        // The same screen read as though the glyph were ten scanlines tall.
        // Such a cell holds three values -- the glyph, its background, and
        // the blanked gap -- so it cannot be one glyph in one colour, and
        // every cell says so.
        const Output naive
            = run("read \"" + image + "\" --cell 8x10 --format json");
        const std::size_t total = json_number(naive.stdout_text, "total_cells");
        CHECK(total > 0);
        CHECK(json_number(naive.stdout_text, "unmatched_cells") == total);
    }
}

TEST_CASE("A real screen with a different colour pair in every cell is read")
{
    // MODE 2 with its own foreground and background per cell, unequal,
    // captured from a running machine. Nothing on the command line says
    // anything about colour: the two values in a cell are the glyph's and its
    // background's, and which is which is worked out from the image.
    //
    // This is the case a single declared background could not express at all.
    // Read against one, a cell of white on red reduces to all-ones -- both
    // colours counting as glyph -- and quietly matches an inverse space.
    const std::size_t columns = 20;
    const std::size_t rows = 32;
    const std::size_t printed = 639; // the last cell is left blank

    std::string expected;
    for (std::size_t row = 0; row < rows; ++row) {
        std::string line;
        for (std::size_t column = 0; column < columns; ++column) {
            const std::size_t index = row * columns + column;
            if (index < printed) {
                append_utf8(line, codepoint_of(32 + index % 95));
            } else {
                line.push_back(' ');
            }
        }
        const std::size_t first = line.find_first_not_of(' ');
        if (first == std::string::npos) {
            continue;
        }
        const std::size_t last = line.find_last_not_of(' ');
        expected += line.substr(first, last - first + 1);
        expected.push_back('\n');
    }

    const Output output
        = run("read \"" + corpus_filepath(2, "multicolour") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text == expected);
}

TEST_CASE("Every cell of a real multicoloured screen is read")
{
    const Output output
        = run("read \"" + corpus_filepath(2, "multicolour") + "\" --format json");

    CHECK(output.status == 0);
    CHECK(json_number(output.stdout_text, "total_cells") == 20 * 32);
    CHECK(json_number(output.stdout_text, "unmatched_cells") == 0);
}

// Screens from a real game.
//
// Waffle, by Chris Bradburne. Its instruction screens flow text around
// graphics in the standard Acorn font on the character grid, which makes them
// the first test of any of this against software written by somebody else --
// everything before was rendered by the emulator's own tests or by this
// library's, and so shares its assumptions.

TEST_CASE("Text is read from a real game's screens")
{
    struct Expectation {
        const char* screen;
        const char* line;
    };

    const Expectation expectations[] = {
        {"waffle-title", "By Chris Bradburne"},
        {"waffle-instructions-2", "This letter is in the correct place:"},
        {"waffle-instructions-4", "The Funky Dingo Situation"},
        {"waffle-board", "Swaps remaining:15"},
    };

    for (const Expectation& expectation : expectations) {
        INFO(expectation.screen);
        const Output output = run(
            "read \"" + fixture_filepath(std::string("screens/")
                                         + expectation.screen + ".png")
            + "\"");
        CHECK(output.status == 0);
        CHECK(output.stdout_text.find(expectation.line) != std::string::npos);
    }
}

TEST_CASE("A real game's graphics are reported unread, never guessed at")
{
    // The board is mostly graphics: coloured tiles carrying letters far larger
    // than any 8x8 glyph, and a title in a font that is not the ROM's. All of
    // it comes back unmatched. What matters is the other half of that -- that
    // nothing spurious is invented from it, which the next case checks.
    const Output output = run("read \"" + fixture_filepath("screens/waffle-board.png")
                              + "\" --format json");

    CHECK(output.status == 0);
    CHECK(json_number(output.stdout_text, "total_cells") == 1280);
    CHECK(json_number(output.stdout_text, "unmatched_cells") > 100);
}

TEST_CASE("Nothing is invented from a real game's graphics")
{
    // Every character read from these four screens, gathered up. If the
    // matcher ever started guessing, something that is not in this alphabet
    // would appear -- the graphics offer far more opportunity to invent a
    // character than the text does to lose one.
    const std::string permitted =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "0123456789 .,:;!?'\"()-/+=<>#*@_$%&[]{}|\\^~`";

    for (const char* const screen :
         {"waffle-title", "waffle-instructions-2", "waffle-instructions-4",
          "waffle-board"}) {
        INFO(screen);
        const Output output
            = run("read \"" + fixture_filepath(std::string("screens/") + screen + ".png")
                  + "\"");
        for (const char character : output.stdout_text) {
            if (character == '\n') {
                continue;
            }
            INFO("character " << static_cast<int>(character));
            CHECK(permitted.find(character) != std::string::npos);
        }
    }
}

TEST_CASE("Text on a dense background is read where it is the ROM font")
{
    // Loopy Loop, by A.S.Shakoor. Its panel text is the ROM font on the grid
    // over black; the rest of the screen is text over a dithered background
    // dense enough that most cells hold three or more colours.
    const Output output
        = run("read \"" + fixture_filepath("screens/loopy-loop.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("points needed") != std::string::npos);
    CHECK(output.stdout_text.find("level") != std::string::npos);
    CHECK(output.stdout_text.find("500") != std::string::npos);
}

TEST_CASE("A redefined character is declined rather than guessed at")
{
    // The game prints "to reach next" with a diamond in place of the 'a',
    // redefined through VDU 23. It is not in the ROM set and is not any other
    // character either, so the cell is unmatched and contributes a space --
    // the word comes back as "re ch" rather than as a plausible wrong word.
    //
    // Reading it properly needs the game's own glyph supplied, which is what
    // supplied glyph sets are for.
    const Output output
        = run("read \"" + fixture_filepath("screens/loopy-loop.png") + "\"");

    CHECK(output.stdout_text.find("to re ch next") != std::string::npos);
    CHECK(output.stdout_text.find("to reach next") == std::string::npos);
}

TEST_CASE("Text placed off the character grid is not read by the aligned path")
{
    // Fruits, a fruit machine. Its labels are placed at arbitrary pixel
    // positions -- GAMBLE at y=173, BANK at 186, DOUBLE at 203, QUITS at 216,
    // spacings of 13, 17 and 13 -- while COLLECT above them happens to land on
    // the grid at y=160.
    //
    // The aligned reader finds what is on the grid and nothing else, which is
    // correct: the off-grid labels are the offset pass's to find, and a test
    // below asserts it does. The two passes partition the screen.
    const Output output
        = run("read \"" + fixture_filepath("screens/fruits-machine.png") + "\"");

    CHECK(output.status == 0);

    // On the grid, and found.
    CHECK(output.stdout_text.find("COLLECT") != std::string::npos);
    CHECK(output.stdout_text.find("PRESS 'S' TO START") != std::string::npos);
    CHECK(output.stdout_text.find("RE-SPIN") != std::string::npos);

    // Off the grid, and not found. Not guessed at either.
    CHECK(output.stdout_text.find("GAMBLE") == std::string::npos);
    CHECK(output.stdout_text.find("DOUBLE") == std::string::npos);
    CHECK(output.stdout_text.find("QUITS") == std::string::npos);
    CHECK(output.stdout_text.find("METER") == std::string::npos);
}

TEST_CASE("A pound sign on a real screen survives the round trip")
{
    // Character 96 of the Acorn set, read off a game that shows money.
    const Output output
        = run("read \"" + fixture_filepath("screens/fruits-machine.png") + "\"");

    CHECK(output.stdout_text.find("CASH  \xC2\xA3 002.50") != std::string::npos);
    CHECK(output.stdout_text.find("STAKE \xC2\xA3 000.00") != std::string::npos);
}

TEST_CASE("A win table of text among graphics is read")
{
    const Output output
        = run("read \"" + fixture_filepath("screens/fruits-wins.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("FRUITS! - WINS") != std::string::npos);
    CHECK(output.stdout_text.find("900 P") != std::string::npos);
}

TEST_CASE("Drop-shadowed text is beyond the aligned path, for two reasons")
{
    // Rondo draws its title twice, a pixel apart in two colours, so it
    // carries its own shadow: the 'R' window holds yellow forming the ROM
    // glyph exactly, red forming the crescent of shadow the text did not
    // cover, and black behind. Three colours, and at y=14 x=61, off the grid.
    //
    // Either reason alone would put it out of reach here: the aligned reader
    // takes cells of two colours, on the grid. The high score table beneath
    // it is ordinary text and reads.
    const Output output
        = run("read \"" + fixture_filepath("screens/rondo-title.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("HIGH SCORES") != std::string::npos);
    CHECK(output.stdout_text.find("MicroUser 2000") != std::string::npos);
    CHECK(output.stdout_text.find("<SPACE>") != std::string::npos);

    // The shadowed title, which offset search finds and this does not.
    CHECK(output.stdout_text.find("RONDO") == std::string::npos);
}

TEST_CASE("A MODE 6 screen from a real game reads with the row pitch")
{
    // Krazy Ape's loading screen: white on blue, with the two blanked
    // scanlines between character rows that MODE 6 leaves. Read with an
    // 8-scanline cell on a 10-scanline pitch, every one of its 1000 cells
    // matches. Read as though the cell were ten scanlines tall, none does.
    const std::string image = fixture_filepath("screens/krazy-loading.png");

    const Output correct = run("read \"" + image + "\" --pitch 8x10 --format json");
    CHECK(correct.status == 0);
    CHECK(json_number(correct.stdout_text, "total_cells") == 1000);
    CHECK(json_number(correct.stdout_text, "unmatched_cells") == 0);

    const Output text = run("read \"" + image + "\" --pitch 8x10");
    CHECK(text.stdout_text.find("LOADING CODE") != std::string::npos);

    const Output naive = run("read \"" + image + "\" --cell 8x10 --format json");
    const std::size_t total = json_number(naive.stdout_text, "total_cells");
    CHECK(total > 0);
    CHECK(json_number(naive.stdout_text, "unmatched_cells") == total);
}

TEST_CASE("Flashing colours are read like any other colours")
{
    // Krazy Ape's key-control screen prints its last two lines in a flashing
    // colour, which alternates between two physical colours about once a
    // second. A capture catches one phase, and it does not matter which:
    // nothing here knows what a palette is, let alone that one is flashing.
    const Output output
        = run("read \"" + fixture_filepath("screens/krazy-controls.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("Key Controls") != std::string::npos);
    CHECK(output.stdout_text.find("SPACE ... jump") != std::string::npos);
    CHECK(output.stdout_text.find("Press SPACE to LOAD") != std::string::npos);
    CHECK(output.stdout_text.find("MACHINE CODE") != std::string::npos);

    // The drop-shadowed title above them is not read, being three colours.
    CHECK(output.stdout_text.find("KRAZY APE II") == std::string::npos);
}

TEST_CASE("In-game drop-shadowed labels are beyond the aligned path")
{
    // LIVES= and Score and BONUS are drawn shadowed and off the grid; the
    // numbers beside them are ordinary text on the grid and read.
    const Output output
        = run("read \"" + fixture_filepath("screens/krazy-game.png") + "\"");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("000000") != std::string::npos);

    CHECK(output.stdout_text.find("LIVES") == std::string::npos);
    CHECK(output.stdout_text.find("Score") == std::string::npos);
    CHECK(output.stdout_text.find("BONUS") == std::string::npos);
}

// The acceptance target for the off-grid search: the three fixtures whose
// off-grid labels the aligned pass cannot see, now found under --search offset.
// These are the inversion of the aligned-pass expectations above.

TEST_CASE("The offset search reads the off-grid labels of a fruit machine")
{
    const Output output = run("read \"" + fixture_filepath("screens/fruits-machine.png")
                              + "\" --search offset");

    CHECK(output.status == 0);
    for (const char* const label :
         {"GAMBLE", "BANK", "DOUBLE", "QUITS", "MELON", "METER"}) {
        INFO(label);
        CHECK(output.stdout_text.find(label) != std::string::npos);
    }

    // The partition holds: grid text is the aligned pass's, not this one's.
    CHECK(output.stdout_text.find("COLLECT") == std::string::npos);
    CHECK(output.stdout_text.find("RE-SPIN") == std::string::npos);
}

TEST_CASE("The offset search reads a drop-shadowed title")
{
    const Output output = run("read \"" + fixture_filepath("screens/rondo-title.png")
                              + "\" --search offset");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("RONDO") != std::string::npos);

    // The high score table is on the grid, so it belongs to the aligned pass.
    CHECK(output.stdout_text.find("MicroUser") == std::string::npos);
}

TEST_CASE("The offset search reads in-game drop-shadowed labels")
{
    const Output output = run("read \"" + fixture_filepath("screens/krazy-game.png")
                              + "\" --search offset");

    CHECK(output.status == 0);
    CHECK(output.stdout_text.find("LIVES") != std::string::npos);
    CHECK(output.stdout_text.find("Score") != std::string::npos);
    CHECK(output.stdout_text.find("BONUS") != std::string::npos);
}

TEST_CASE("Every off-grid cell is flagged offset in the JSON")
{
    const Output output = run("read \"" + fixture_filepath("screens/rondo-title.png")
                              + "\" --search offset --format json");

    CHECK(output.status == 0);
    // Under the offset pass, matched cells carry offset:true; the aligned
    // false never appears.
    CHECK(output.stdout_text.find("\"offset\":true") != std::string::npos);
    CHECK(output.stdout_text.find("\"offset\":false") == std::string::npos);
}

TEST_CASE("The offset search invents nothing on a screen with no off-grid text")
{
    // Waffle's text is entirely on the grid, so the off-grid pass must find
    // nothing at all -- the negative guarantee, on a real screen.
    for (const char* const screen :
         {"waffle-title", "waffle-instructions-2", "waffle-instructions-4",
          "waffle-board"}) {
        INFO(screen);
        const Output output
            = run("read \"" + fixture_filepath(std::string("screens/") + screen
                                               + ".png")
                  + "\" --search offset");
        CHECK(output.status == 0);

        // Nothing but whitespace on standard output.
        for (const char character : output.stdout_text) {
            CHECK((character == '\n' || character == ' '));
        }
    }
}

TEST_CASE("The offset search reads real text off a hostile background")
{
    // Loopy Loop draws over a dense dithered pattern. Much of its text is a
    // thickened variant of the ROM font and does not match, but the glyphs
    // that are the ROM font are found -- and everything reported is a real
    // character, none invented from the background.
    const Output output = run("read \"" + fixture_filepath("screens/loopy-loop.png")
                              + "\" --search offset");

    CHECK(output.status == 0);

    // Fragments of "A.S.SHAKOOR '92" and the "* ... UP / ? ... DOWN" legend.
    CHECK(output.stdout_text.find("'92") != std::string::npos);

    // Whatever is reported is drawn from the readable alphabet, not conjured
    // from the dither: no control characters, no stray bytes.
    const std::string permitted =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "0123456789 .,:;!?'\"()-/+=<>#*@_$%&[]{}|\\^~`\n";
    for (const char character : output.stdout_text) {
        INFO("character " << static_cast<int>(character));
        CHECK(permitted.find(character) != std::string::npos);
    }
}

TEST_CASE("A game's own font is declined by the ROM set, read when supplied")
{
    // Thrust draws its high-score table in a bespoke 5-pixel font it blits
    // from its own glyph table -- not the ROM font, and not a VDU 23
    // redefinition the MOS would give a character code. So the built-in Acorn
    // set matches almost none of it, and the text copies as spaces: unread,
    // never guessed. This is the "copies as spaces" a custom-font game shows.
    const std::string image = fixture_filepath("screens/thrust-hiscore.png");

    const Output builtin = run("read \"" + image + "\" --format json");
    CHECK(builtin.status == 0);
    // Almost the whole table is unmatched against the ROM font.
    CHECK(json_number(builtin.stdout_text, "unmatched_cells") > 400);
    const Output builtin_text = run("read \"" + image + "\"");
    CHECK(builtin_text.stdout_text.find("THRUSTERS") == std::string::npos);
    CHECK(builtin_text.stdout_text.find("SPACELORD") == std::string::npos);

    // Supply the game's own font -- transcribed into fonts/thrust.glyphs --
    // and the same pixels read as text.
    const std::string font = fixture_filepath("fonts/thrust.glyphs");
    const Output supplied
        = run("read \"" + image + "\" --no-builtin --glyphs \"" + font + "\"");
    CHECK(supplied.status == 0);
    CHECK(supplied.stdout_text.find("TOP EIGHT THRUSTERS") != std::string::npos);
    CHECK(supplied.stdout_text.find("SPACELORD") != std::string::npos);
    CHECK(supplied.stdout_text.find("COMMODORE") != std::string::npos);
    CHECK(supplied.stdout_text.find("PRESS SPACE BAR TO START")
          != std::string::npos);
}
