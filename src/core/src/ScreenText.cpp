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

#include <beebium/ScreenText.hpp>

#include <beebium/TeletextText.hpp>

#include <screentext/Read.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace beebium::screen {

namespace {

// A BBC character cell is eight logical pixels wide in every bitmap mode --
// MODE 0 is 640 pixels over 80 columns, MODE 2 is 160 over 20 -- because the
// framebuffer holds logical pixels and the client stretches them.
constexpr uint32_t BITMAP_CELL_WIDTH = 8;

// A glyph is eight scanlines tall whatever the row pitch. MODE 3 and MODE 6
// leave the two spare scanlines of their ten-scanline pitch blank.
constexpr uint32_t BITMAP_CELL_HEIGHT = 8;

// The SAA5050 draws a character sixteen framebuffer pixels wide: two pixel
// batches, each expanding six bits of the font row to eight pixels (Saa5050
// emit_pixels). A whole 40-column teletext row is therefore 640 pixels, the
// same width the renderer reports -- so a run's bounds line up with the pixels
// it names. (The chip's internal font row is doubled 6->12 for anti-aliasing,
// which is where an earlier value of twelve came from, but that is re-expanded
// to 8 per half before it reaches the framebuffer, so the on-screen pitch is
// sixteen, not twelve.)
constexpr uint32_t TELETEXT_CELL_WIDTH = 16;

void strip_trailing_spaces(std::string& line) {
    while (!line.empty() && line.back() == ' ') {
        line.pop_back();
    }
}

void append_utf8(std::string& out, char32_t codepoint) {
    if (codepoint < 0x80) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

// Which grid cells of a band a pixel rectangle covers.
struct CellRange {
    uint32_t first_row = 0;
    uint32_t last_row = 0;      // One past
    uint32_t first_column = 0;
    uint32_t last_column = 0;   // One past

    [[nodiscard]] bool empty() const {
        return last_row <= first_row || last_column <= first_column;
    }
};

// Cells whose top-left corner falls inside the rectangle, clipped to the grid.
//
// A partly covered cell is taken whole: a drag that clips the right-hand half
// of a character selected that character, and returning half a glyph is not a
// thing text can express.
CellRange cells_covered(const Band& band,
                        const PixelRect& region,
                        uint32_t columns,
                        uint32_t rows) {
    const PixelRect band_rect{band.origin_x, band.top,
                              columns * band.column_pitch,
                              band.bottom - band.top};
    const PixelRect covered = band_rect.intersected(region);
    if (covered.empty() || band.column_pitch == 0 || band.row_pitch == 0) {
        return {};
    }

    CellRange range;
    range.first_column = (covered.x - band.origin_x) / band.column_pitch;
    range.last_column =
        (covered.right() - band.origin_x + band.column_pitch - 1) / band.column_pitch;
    range.first_row = (covered.y - band.origin_y) / band.row_pitch;
    range.last_row =
        (covered.bottom() - band.origin_y + band.row_pitch - 1) / band.row_pitch;

    range.last_column = std::min(range.last_column, columns);
    range.last_row = std::min(range.last_row, rows);
    range.first_column = std::min(range.first_column, range.last_column);
    range.first_row = std::min(range.first_row, range.last_row);
    return range;
}

// Read a band the SAA5050 was driving.
//
// The characters are taken from the grid the chip filled, not recognised in
// pixels: they are known exactly, with their attributes, before any pixels
// exist, and sending them through image recognition would be converting
// information into a picture in order to guess it back. Nothing here can be
// uncertain, so both uncertainty counts stay zero.
//
// What a cell copies as matches teletext_text(): graphics, control codes,
// concealed cells and the bottom half of a double-height row all occupy their
// column as a space, so what is copied lines up with what is displayed.
BandReading read_teletext_band(const Band& band,
                               const PixelRect& region,
                               const TeletextGrid::Snapshot& teletext) {
    BandReading reading;
    if (!teletext.active) {
        // The grid holds whatever was last shown in MODE 7. A band the
        // SAA5050 was not driving must not be read from it.
        return reading;
    }

    reading.supported = true;

    const CellRange range = cells_covered(
        band, region,
        static_cast<uint32_t>(TeletextGrid::COLUMNS),
        static_cast<uint32_t>(TeletextGrid::ROWS));
    if (range.empty()) {
        return reading;
    }

    for (uint32_t row = range.first_row; row < range.last_row; ++row) {
        std::string line;
        for (uint32_t column = range.first_column; column < range.last_column; ++column) {
            const TeletextCell& cell = teletext.cell(row, column);

            const bool readable =
                !cell.is_control_code
                && !cell.concealed
                && !cell.double_height_bottom
                && cell.charset == TeletextCellCharset::Alpha;

            const char32_t codepoint =
                readable ? teletext_alpha_codepoint(cell.character) : 0;

            if (codepoint == 0) {
                line.push_back(' ');
            } else {
                append_utf8(line, codepoint);
            }
        }

        // Measured before trailing spaces are stripped: running right up to
        // the edge is what distinguishes a wrapped line from one that ended.
        const bool reached_the_edge = !line.empty() && line.back() != ' ';
        const uint32_t cells = range.last_column - range.first_column;
        strip_trailing_spaces(line);

        // A run per row of the region, blank rows included. The blank rows are
        // part of what a selection captured, and carrying them is what lets
        // the layouts reproduce the shape of the screen.
        TextRun run;
        run.text = std::move(line);
        run.bounds = {band.origin_x + range.first_column * band.column_pitch,
                      band.origin_y + row * band.row_pitch,
                      cells * band.column_pitch,
                      band.row_pitch};
        run.cell_width = band.cell_width;
        run.cell_height = band.cell_height;
        run.reached_right_edge = reached_the_edge;
        reading.runs.push_back(std::move(run));
    }

    return reading;
}

// The VDU driver's font workspace, read to resolve the soft (VDU 23) font.
//
// These addresses are specific to the MOS 1.20 (Model B) and MOS 2.00 (Model
// B+) family Beebium ships, and are NOT assumed to hold for any other OS: a
// Master or a foreign MOS may lay its workspace out differently. So the soft
// font is read only once the running MOS has been recognised by its ROM font
// (see known_bbc_mos); an unrecognised MOS falls back to the base font alone
// rather than reading these addresses on faith. See ScreenText.hpp.
constexpr uint16_t VDU_FONT_FLAGS = 0x0367;
constexpr uint16_t VDU_FONT_ZONE_PAGES = 0x0368; // Seven bytes, zones 1..7

// The MOS ROM font sits at $C000, eight bytes per character from character 32,
// so character c is at $C000 + (c - 32) * 8. This is where the base set was
// generated from, and comparing it back is how the running MOS is recognised.
constexpr uint16_t ROM_FONT_BASE = 0xC000;
constexpr uint16_t ROM_FONT_FIRST_CHARACTER = 0x20;

// The vduFontFlags bit that marks a zone as holding RAM (soft) definitions,
// indexed by zone number 1..7. The zone of code c is c >> 5. From the MOS
// fontMaskTable.
constexpr std::array<uint8_t, 8> ZONE_SOFT_BIT = {
    0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01};

// The text a redefined code carries. Identity across the printable range, bar
// the single place the Acorn font departs from ASCII: code &60 is a pound sign.
char32_t soft_codepoint(uint8_t code) {
    return code == 0x60 ? char32_t{0x00A3} : char32_t{code};
}

// Whether the running MOS is one whose ROM font is the base set, and so whose
// font workspace we know how to read.
//
// There is no version byte to read for this. OSBYTE 0 returns a single coarse
// "OS type" -- 1 for the whole Model B/B+ line, indistinguishable from a Master
// -- and it is a call, not a byte; the exact "OS 1.20" text exists only as an
// error string whose ROM address moves between versions. So rather than a
// proxy, we verify the thing we actually depend on: the ROM font itself. If
// every glyph at $C000 matches the built-in Acorn set byte for byte, the base
// font is correct for this machine, and -- since the only ROMs carrying that
// exact font are MOS 1.20 and 2.00 -- its font workspace is the one read above.
//
// The residual assumption is narrow: a ROM with the 1.20 font but a different
// page-3 layout would be misread, but such a ROM is the 1.20/2.00 MOS.
//
// DEFERRED -- other MOS families. A Master (MOS 3.x), MOS 5.x, or a second
// processor explodes the font into a different place by default and lays its
// VDU workspace out differently, and its ROM font differs, so this check fails
// and the machine gets the base font alone. Supporting them means recognising
// the MOS and carrying a per-family (font, workspace-layout) map, plus reading
// the ROM font live for the base set. Until then, failing here is safe: the
// built-in font stands and redefinitions are declined, never misread.
bool known_bbc_mos(const std::function<uint8_t(uint16_t)>& peek_byte) {
    const screentext::GlyphSet& acorn =
        screentext::builtin_glyph_set("acorn-mos-1.20");

    // The whole font, not a sample: every built-in glyph must match the ROM at
    // its address, so "recognised" means the base set is verified in full and
    // there is no room for an unrelated ROM to pass by coincidence.
    for (const screentext::Glyph& glyph : acorn.glyphs) {
        // The built-in codepoint maps back to a BBC code: identity everywhere
        // bar the pound sign, which the Acorn font places at code &60.
        const uint8_t code = glyph.codepoint == 0x00A3
            ? uint8_t{0x60}
            : static_cast<uint8_t>(glyph.codepoint);

        const uint16_t address = static_cast<uint16_t>(
            ROM_FONT_BASE + (code - ROM_FONT_FIRST_CHARACTER) * 8);
        const std::vector<uint8_t>& expected = glyph.bitmap.bytes();
        if (expected.size() < 8) {
            return false;
        }
        for (int i = 0; i < 8; ++i) {
            if (peek_byte(static_cast<uint16_t>(address + i)) != expected[i]) {
                return false;
            }
        }
    }
    return true;
}

// The rightmost cell right-edge the aligned walk reaches within the region, in
// image-local pixels, or zero when no whole cell fits. Used to tell a run that
// ran to the edge -- and so wrapped -- from one that stopped short.
uint32_t last_cell_right(const screentext::Band& band, std::size_t region_right) {
    const std::size_t pitch = band.effective_column_pitch();
    uint32_t last = 0;
    for (std::size_t x = band.origin_x; x + band.cell_width <= region_right;
         x += pitch) {
        last = static_cast<uint32_t>(x + band.cell_width);
    }
    return last;
}

// Reduce the band's rendered pixels to the library's one-byte-per-pixel image,
// where equal bytes mean the same colour. Each distinct framebuffer value is
// given a small id; the library needs only equality, not the palette, so any
// consistent assignment serves. A bitmap mode shows at most sixteen colours in
// a band, so the ids never approach the byte's range.
screentext::Image band_image(const FrameImage& frame, const Band& band) {
    screentext::Image image;
    image.width = frame.width;
    image.height = band.bottom - band.top;
    image.pixels.resize(image.width * image.height);

    std::vector<uint32_t> palette;
    palette.reserve(16);
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            const uint32_t value = frame.pixel(x, band.top + y);
            std::size_t id = 0;
            for (; id < palette.size(); ++id) {
                if (palette[id] == value) {
                    break;
                }
            }
            if (id == palette.size()) {
                // More distinct colours than a byte can hold cannot arise in a
                // bitmap mode; were it ever to, the surplus collapses onto the
                // last id rather than wrapping, so a cell is at worst unread.
                if (palette.size() <= 0xFF) {
                    palette.push_back(value);
                } else {
                    id = 0xFF;
                }
            }
            image.pixels[y * image.width + x] = static_cast<uint8_t>(id);
        }
    }
    return image;
}

// Add a run to the reading, translating the library's image-local geometry back
// to frame pixels and carrying the cell geometry a snapped selection needs.
void append_run(BandReading& reading, const screentext::Run& run,
                const Band& band, uint32_t last_right) {
    if (run.cells.empty()) {
        return;
    }
    const bool off_grid = run.cells.front().offset;

    TextRun out;
    out.text = run.text;
    out.bounds = {static_cast<uint32_t>(run.bounds.x),
                  static_cast<uint32_t>(run.bounds.y) + band.top,
                  static_cast<uint32_t>(run.bounds.width),
                  static_cast<uint32_t>(run.bounds.height)};

    if (off_grid) {
        // Text at the graphics cursor is not on any grid, so it carries no cell
        // geometry to snap to and never counts as having reached an edge.
        out.cell_width = 0;
        out.cell_height = 0;
        out.reached_right_edge = false;
    } else {
        out.cell_width = band.cell_width;
        out.cell_height = band.cell_height;
        out.reached_right_edge =
            last_right != 0 && run.bounds.right() == last_right;
    }
    reading.runs.push_back(std::move(out));
}

// Read a band by recognising glyphs in its pixels.
//
// The band the SAA5050 was not driving is bitmap: its characters are font
// glyphs drawn unmodified, so a cell either matches a known glyph exactly or is
// left unread -- never guessed. The geometry carries straight over from the
// core band, in image-local coordinates; the glyph set is the one assembled
// from the running machine. Aligned runs one grid pass; Anywhere runs the grid
// pass and the off-grid pass and concatenates them, the library having built
// the two to be disjoint.
BandReading read_bitmap_band(const Band& band,
                             const PixelRect& region,
                             Search search,
                             const BandSources& sources) {
    BandReading reading;
    // The band was read: a bitmap display is supported whether or not any text
    // was found on it, which is what tells "graphics, no text" from "could not
    // read". An absent image or glyph set is a wiring fault, not a blank
    // screen, so it stays unsupported rather than claiming an empty reading.
    if (sources.image.empty() || sources.glyph_sets == nullptr) {
        return reading;
    }
    reading.supported = true;

    const PixelRect band_rect{0, band.top, sources.image.width,
                              band.bottom - band.top};
    const PixelRect covered = band_rect.intersected(region);
    if (covered.empty() || band.column_pitch == 0 || band.row_pitch == 0) {
        return reading;
    }

    const screentext::Image image = band_image(sources.image, band);

    screentext::Band lib_band;
    lib_band.top = 0;
    lib_band.bottom = band.bottom - band.top;
    lib_band.cell_width = band.cell_width;
    lib_band.cell_height = band.cell_height;
    lib_band.column_pitch = band.column_pitch;
    lib_band.row_pitch = band.row_pitch;
    lib_band.origin_x = band.origin_x;
    lib_band.origin_y = 0;
    const std::vector<screentext::Band> lib_bands{lib_band};

    // The covered rectangle, in the image's band-local coordinates.
    screentext::Options options;
    options.selection = screentext::Rect{covered.x, covered.y - band.top,
                                         covered.width, covered.height};

    const std::size_t region_right =
        std::min<std::size_t>(options.selection->right(), image.width);
    const uint32_t last_right = last_cell_right(lib_band, region_right);

    const auto absorb = [&](const screentext::Result& result, bool aligned) {
        reading.unreadable_cells +=
            static_cast<uint32_t>(result.unmatched_cells);
        reading.ambiguous_cells +=
            static_cast<uint32_t>(result.ambiguous_cells);
        for (const screentext::Run& run : result.runs) {
            append_run(reading, run, band, aligned ? last_right : 0);
        }
    };

    options.search = screentext::Search::AlignedOnly;
    absorb(screentext::read(image, lib_bands, *sources.glyph_sets, options),
           true);

    if (search == Search::Anywhere) {
        // The off-grid pass finds only what the grid pass did not, so the two
        // results concatenate with nothing to reconcile: runs appended, counts
        // summed. This makes Anywhere a strict superset of Aligned.
        options.search = screentext::Search::OffsetOnly;
        absorb(screentext::read(image, lib_bands, *sources.glyph_sets, options),
               false);
    }

    return reading;
}

} // namespace

PixelRect PixelRect::intersected(const PixelRect& other) const {
    const uint32_t left = std::max(x, other.x);
    const uint32_t top = std::max(y, other.y);
    const uint32_t right = std::min(this->right(), other.right());
    const uint32_t bottom = std::min(this->bottom(), other.bottom());

    if (right <= left || bottom <= top) {
        return {};
    }
    return {left, top, right - left, bottom - top};
}

std::vector<Band> bands_of(const FrameMetadata& metadata) {
    std::vector<Band> bands;
    bands.reserve(metadata.regions.size());

    for (const FrameDisplayRegion& region : metadata.regions) {
        Band band;
        band.top = region.start_line;
        band.bottom = region.end_line;
        band.is_teletext = region.is_teletext;

        // The framebuffer's origin is the top-left of the active area -- the
        // renderer resets x and y when display enable goes high -- so the grid
        // starts at the left edge of the frame and the top of the band.
        band.origin_x = 0;
        band.origin_y = region.start_line;

        if (region.is_teletext) {
            // A teletext display is 40 by 25 cells, which is not an assumption
            // about the mode but the shape of the chip: the SAA5050 has no
            // other. Taking the pitch from the band rather than from R9 keeps
            // it right through the interlacing that doubles the frame.
            band.cell_width = TELETEXT_CELL_WIDTH;
            band.column_pitch = TELETEXT_CELL_WIDTH;
            const uint32_t height = region.end_line - region.start_line;
            // A frame captured while the CRTC is being reprogrammed can be
            // shorter than a whole screen, so the pitch is floored at one
            // rather than allowed to reach zero and divide by it downstream.
            band.row_pitch = std::max<uint32_t>(
                1, height / static_cast<uint32_t>(TeletextGrid::ROWS));
            // The chip fills the whole cell; there are no blanked scanlines
            // between teletext rows.
            band.cell_height = band.row_pitch;
        } else {
            band.cell_width = BITMAP_CELL_WIDTH;
            band.column_pitch = BITMAP_CELL_WIDTH;
            band.row_pitch = region.char_scanlines;
            // MODE 3 and MODE 6 blank the spare scanlines of a ten-scanline
            // pitch rather than painting them, so the cell is the glyph and
            // the pitch is the step.
            band.cell_height = std::min<uint32_t>(band.row_pitch, BITMAP_CELL_HEIGHT);
        }

        bands.push_back(band);
    }

    return bands;
}

BandReading read_band(const Band& band,
                      const PixelRect& region,
                      Search search,
                      const BandSources& sources) {
    // The strategy is chosen from the hardware state that was in effect when
    // these scanlines were drawn, not from anything the caller said. A new
    // strategy is added by extending this dispatch; nothing above it, on the
    // wire or in a client, has to know it happened.
    //
    // The search mode is recorded and passed through to the strategies. The
    // teletext strategy has no use for it -- the grid is the grid -- and the
    // bitmap strategy chooses between walking the grid alone and also searching
    // sub-cell offsets on the strength of it.
    if (band.is_teletext) {
        if (sources.teletext == nullptr) {
            return BandReading{};
        }
        return read_teletext_band(band, region, *sources.teletext);
    }

    return read_bitmap_band(band, region, search, sources);
}

screentext::GlyphSet read_soft_font(
    const std::function<uint8_t(uint16_t)>& peek_byte) {
    screentext::GlyphSet set;
    set.name = "soft-font";

    const uint8_t flags = peek_byte(VDU_FONT_FLAGS);

    // Every printable code, ascending, skipping DEL, which prints nothing. A
    // code whose zone is still in ROM is left to the base set; a code redefined
    // to blank is skipped rather than emitted, since an all-zero glyph would
    // match every blank cell and masquerade as a space.
    //
    // When the font is imploded the four upper zones all point at the same
    // $0C00 block, so codes 128, 160, 192 and 224 are the same pixels on
    // screen -- genuinely aliased by the MOS. Emitting all four would make
    // every redefined glyph ambiguous with its own aliases, which is noise, not
    // the font-collision ambiguity the library exists to report. So each
    // backing address is emitted once. The pixels cannot say which alias was
    // printed, so the highest code that owns the address is taken: 224-255 is
    // the range the User Guide teaches for user-defined characters, and the one
    // a program redefining an imploded glyph almost always prints.
    std::vector<uint16_t> bases;    // Backing address of each emitted glyph
    for (uint16_t code = 0x20; code <= 0xFF; ++code) {
        if (code == 0x7F) {
            continue;
        }
        const uint8_t zone = static_cast<uint8_t>(code >> 5); // 1..7
        if ((flags & ZONE_SOFT_BIT[zone]) == 0) {
            continue;
        }

        const uint8_t page = peek_byte(
            static_cast<uint16_t>(VDU_FONT_ZONE_PAGES + (zone - 1)));
        const uint16_t base = static_cast<uint16_t>(page << 8)
            | static_cast<uint16_t>((code & 0x1F) << 3);

        const char32_t codepoint = soft_codepoint(static_cast<uint8_t>(code));

        // Ascending, so a later code aliasing an emitted address is higher:
        // take over its codepoint rather than adding a second, colliding glyph.
        const auto seen = std::find(bases.begin(), bases.end(), base);
        if (seen != bases.end()) {
            set.glyphs[static_cast<size_t>(seen - bases.begin())].codepoint =
                codepoint;
            continue;
        }

        std::vector<uint8_t> rows(8);
        uint8_t any = 0;
        for (int i = 0; i < 8; ++i) {
            rows[i] = peek_byte(static_cast<uint16_t>(base + i));
            any |= rows[i];
        }
        if (any == 0) {
            continue;
        }

        bases.push_back(base);
        set.glyphs.emplace_back(codepoint, screentext::Bitmap::from_rows(rows));
    }

    return set;
}

std::vector<screentext::GlyphSet> assemble_glyph_sets(
    const std::function<uint8_t(uint16_t)>& peek_byte) {
    std::vector<screentext::GlyphSet> sets;
    // The ROM base font, always: it is the fallback whatever the machine, and
    // correct for every OS Beebium ships.
    sets.push_back(screentext::builtin_glyph_set("acorn-mos-1.20"));

    // The soft font, only when the running MOS is one whose font workspace we
    // know how to read. On an unrecognised OS the redefinitions are declined
    // rather than read from addresses that may not mean what they do here --
    // honest degrading, never a wrong glyph.
    if (known_bbc_mos(peek_byte)) {
        sets.push_back(read_soft_font(peek_byte));
    }
    return sets;
}

Reading concatenate_bands_readings(std::vector<BandReading> readings, Layout layout) {
    Reading combined;

    for (BandReading& band : readings) {
        combined.supported = combined.supported || band.supported;
        combined.unreadable_cells += band.unreadable_cells;
        combined.ambiguous_cells += band.ambiguous_cells;

        // Within a band, by baseline then x. Stable, so a strategy that
        // already emits in order keeps the order it chose.
        std::stable_sort(band.runs.begin(), band.runs.end(),
                         [](const TextRun& a, const TextRun& b) {
                             if (a.bounds.y != b.bounds.y) {
                                 return a.bounds.y < b.bounds.y;
                             }
                             return a.bounds.x < b.bounds.x;
                         });

        // Bands arrive top to bottom and do not overlap, and no strategy may
        // produce overlapping runs, so this is concatenation: there is nothing
        // to dedupe and nothing that would need to be.
        combined.runs.insert(combined.runs.end(),
                             std::make_move_iterator(band.runs.begin()),
                             std::make_move_iterator(band.runs.end()));
    }

    combined.text = linearise(combined.runs, layout);
    return combined;
}

std::string linearise(const std::vector<TextRun>& runs, Layout layout) {
    std::string text;
    bool first = true;
    uint32_t previous_baseline = 0;
    bool previous_reached_the_edge = false;

    for (const TextRun& run : runs) {
        if (first) {
            first = false;
        } else if (run.bounds.y == previous_baseline) {
            // Runs sharing a baseline are on the same line. A space keeps two
            // pieces of text found apart on that line from running together.
            text.push_back(' ');
        } else if (layout == Layout::Rows || !previous_reached_the_edge) {
            text.push_back('\n');
        }
        // Flowed and the previous run reached the edge: the line wrapped
        // rather than ended, so it continues with no break at all.

        text += run.text;
        previous_baseline = run.bounds.y;
        previous_reached_the_edge = run.reached_right_edge;
    }

    // A screen is as many rows tall as it is whatever is written on it, so a
    // copy otherwise ends in the blank rows below the text.
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) {
        text.pop_back();
    }

    return text;
}

} // namespace beebium::screen
