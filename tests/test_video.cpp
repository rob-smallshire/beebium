#include <catch2/catch_test_macros.hpp>
#include <beebium/PixelBatch.hpp>
#include <beebium/OutputQueue.hpp>
#include <beebium/FrameAllocator.hpp>
#include <beebium/FrameBuffer.hpp>
#include <beebium/FrameRenderer.hpp>
#include <beebium/devices/Crtc6845.hpp>
#include <beebium/devices/VideoUla.hpp>
#include <beebium/Machines.hpp>
#include <filesystem>
#include <fstream>

using namespace beebium;

// ============================================================================
// PPM writer utility
// ============================================================================

namespace {

// Write BGRA32 framebuffer to PPM file
bool write_ppm(const std::filesystem::path& filepath,
               const uint32_t* pixels,
               size_t width, size_t height) {
    std::ofstream file(filepath, std::ios::binary);
    if (!file) return false;

    // PPM header (P6 = binary RGB)
    file << "P6\n" << width << " " << height << "\n255\n";

    // Convert BGRA32 to RGB bytes
    for (size_t i = 0; i < width * height; ++i) {
        uint32_t pixel = pixels[i];
        uint8_t b = (pixel >> 0) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t r = (pixel >> 16) & 0xFF;
        file.put(static_cast<char>(r));
        file.put(static_cast<char>(g));
        file.put(static_cast<char>(b));
    }

    return file.good();
}

// Load a ROM file into a vector
std::vector<uint8_t> load_rom(const std::filesystem::path& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open ROM: " + filepath.string());
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    return data;
}

#ifdef BEEBIUM_ROM_DIR
// Check if ROM files exist
bool roms_available() {
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    return std::filesystem::exists(rom_dirpath / "OS12.ROM") &&
           std::filesystem::exists(rom_dirpath / "BASIC2.ROM");
}
#endif

} // anonymous namespace

// ============================================================================
// PixelBatch tests
// ============================================================================

TEST_CASE("PixelBatch initialization", "[video]") {
    PixelBatch unit{};
    unit.clear();

    SECTION("default values after clear") {
        CHECK(unit.type() == PixelBatchType::Nothing);
        CHECK(unit.flags() == VIDEO_FLAG_NONE);
        CHECK_FALSE(unit.hsync());
        CHECK_FALSE(unit.vsync());
        CHECK_FALSE(unit.display_enable());
    }

    SECTION("set flags") {
        unit.set_flags(VIDEO_FLAG_HSYNC | VIDEO_FLAG_DISPLAY);
        CHECK(unit.hsync());
        CHECK_FALSE(unit.vsync());
        CHECK(unit.display_enable());
    }

    SECTION("set type") {
        unit.set_type(PixelBatchType::Bitmap);
        CHECK(unit.type() == PixelBatchType::Bitmap);

        unit.set_type(PixelBatchType::Teletext);
        CHECK(unit.type() == PixelBatchType::Teletext);
    }

    SECTION("fill with color") {
        unit.fill(bbc_colors::RED);
        for (int i = 0; i < 8; ++i) {
            CHECK(unit.pixels.pixels[i].bits.r == 15);
            CHECK(unit.pixels.pixels[i].bits.g == 0);
            CHECK(unit.pixels.pixels[i].bits.b == 0);
        }
    }
}

TEST_CASE("VideoDataPixel bit packing", "[video]") {
    VideoDataPixel pixel{};

    SECTION("RGB components via bits") {
        pixel.bits.r = 0xF;  // 4-bit red
        pixel.bits.g = 0x8;  // 4-bit green
        pixel.bits.b = 0x4;  // 4-bit blue

        CHECK(pixel.bits.r == 0xF);
        CHECK(pixel.bits.g == 0x8);
        CHECK(pixel.bits.b == 0x4);
    }

    SECTION("constructor with RGB values") {
        VideoDataPixel p(15, 8, 4);  // r=15, g=8, b=4
        CHECK(p.bits.r == 15);
        CHECK(p.bits.g == 8);
        CHECK(p.bits.b == 4);
    }

    SECTION("raw value access") {
        VideoDataPixel p(0xF, 0x8, 0x4);  // r=F, g=8, b=4
        // Layout: bits 0-3=b, 4-7=g, 8-11=r, 12-15=x
        // So value = 0x0F84
        CHECK(p.value == 0x0F84);
    }
}

TEST_CASE("BBC colors palette", "[video]") {
    CHECK(bbc_colors::BLACK.bits.r == 0);
    CHECK(bbc_colors::BLACK.bits.g == 0);
    CHECK(bbc_colors::BLACK.bits.b == 0);

    CHECK(bbc_colors::RED.bits.r == 15);
    CHECK(bbc_colors::RED.bits.g == 0);
    CHECK(bbc_colors::RED.bits.b == 0);

    CHECK(bbc_colors::WHITE.bits.r == 15);
    CHECK(bbc_colors::WHITE.bits.g == 15);
    CHECK(bbc_colors::WHITE.bits.b == 15);

    CHECK(bbc_colors::PALETTE[0].value == bbc_colors::BLACK.value);
    CHECK(bbc_colors::PALETTE[7].value == bbc_colors::WHITE.value);
}

// ============================================================================
// OutputQueue tests
// ============================================================================

TEST_CASE("OutputQueue basic operations", "[video]") {
    OutputQueue<PixelBatch> queue(64);  // Small queue for testing

    SECTION("initial state") {
        CHECK(queue.size() == 0);
        CHECK(queue.capacity() == 64);
        CHECK(queue.available() == 64);
        CHECK_FALSE(queue.full());
        CHECK(queue.empty());
    }

    SECTION("produce and consume") {
        auto producer = queue.get_producer_buffer();
        CHECK(producer.total() == 64);

        // Write one unit
        producer.a[0].set_flags(VIDEO_FLAG_HSYNC);
        queue.produce(1);

        CHECK(queue.size() == 1);
        CHECK(queue.available() == 63);

        // Read it back
        auto consumer = queue.get_consumer_buffer();
        CHECK(consumer.total() == 1);
        CHECK(consumer.a[0].hsync());

        queue.consume(1);
        CHECK(queue.size() == 0);
    }

    SECTION("push convenience method") {
        PixelBatch unit{};
        unit.set_flags(VIDEO_FLAG_VSYNC);

        bool ok = queue.push(unit);
        CHECK(ok);
        CHECK(queue.size() == 1);

        auto consumer = queue.get_consumer_buffer();
        CHECK(consumer.a[0].vsync());
    }

    SECTION("wrap around") {
        // Fill half the queue
        queue.produce(32);
        queue.consume(32);

        // Now write more - should wrap around
        auto producer = queue.get_producer_buffer();
        CHECK(producer.total() == 64);  // Full space available after consume

        // Produce 48 items (wraps around)
        queue.produce(48);
        CHECK(queue.size() == 48);

        auto consumer = queue.get_consumer_buffer();
        CHECK(consumer.total() == 48);
    }

    SECTION("reset") {
        queue.produce(10);
        CHECK(queue.size() == 10);

        queue.reset();
        CHECK(queue.size() == 0);
        CHECK(queue.empty());
    }
}

// ============================================================================
// FrameAllocator tests
// ============================================================================

TEST_CASE("HeapFrameAllocator", "[video]") {
    HeapFrameAllocator allocator;

    SECTION("allocate and release") {
        auto buffer = allocator.allocate(1024);
        CHECK(buffer.size() == 1024);
        CHECK(buffer.data() != nullptr);

        // Write to buffer
        buffer[0] = 0xDEADBEEF;
        buffer[1023] = 0xCAFEBABE;
        CHECK(buffer[0] == 0xDEADBEEF);
        CHECK(buffer[1023] == 0xCAFEBABE);

        allocator.release(buffer);
    }

    SECTION("multiple allocations") {
        auto b1 = allocator.allocate(100);
        auto b2 = allocator.allocate(200);

        CHECK(b1.data() != b2.data());

        allocator.release(b1);
        allocator.release(b2);
    }
}

// ============================================================================
// FrameBuffer tests
// ============================================================================

TEST_CASE("FrameBuffer double buffering", "[video]") {
    FrameBuffer fb(nullptr, 16, 16);  // Small buffer for testing

    SECTION("dimensions") {
        CHECK(fb.width() == 16);
        CHECK(fb.height() == 16);
        CHECK(fb.pixel_count() == 256);
    }

    SECTION("initial version") {
        CHECK(fb.version() == 0);
    }

    SECTION("write and swap") {
        // Write to front buffer
        fb.write_pixel(0, 0, 0xFF0000FF);  // Red pixel
        fb.write_pixel(15, 15, 0xFF00FF00);  // Green pixel

        // Swap buffers
        fb.swap();
        CHECK(fb.version() == 1);

        // Read from back buffer
        auto frame = fb.read_frame();
        CHECK(frame[0] == 0xFF0000FF);
        CHECK(frame[255] == 0xFF00FF00);
    }

    SECTION("version increments") {
        fb.swap();
        CHECK(fb.version() == 1);
        fb.swap();
        CHECK(fb.version() == 2);
        fb.swap();
        CHECK(fb.version() == 3);
    }

    SECTION("clear") {
        fb.clear(0xFFFFFFFF);  // White
        fb.swap();

        auto frame = fb.read_frame();
        CHECK(frame[0] == 0xFFFFFFFF);
        CHECK(frame[128] == 0xFFFFFFFF);
    }
}

// ============================================================================
// FrameRenderer tests
// ============================================================================

TEST_CASE("FrameRenderer basic rendering", "[video]") {
    FrameBuffer fb(nullptr, 64, 64);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(256);

    SECTION("initial state") {
        CHECK(renderer.x() == 0);
        CHECK(renderer.y() == 0);
    }

    SECTION("process display units") {
        // Create a display unit with test pixels
        PixelBatch unit{};
        unit.clear();  // Ensure clean state
        unit.set_type(PixelBatchType::Bitmap);
        unit.set_flags(VIDEO_FLAG_DISPLAY);

        // Verify flags are set correctly
        CHECK(unit.display_enable());

        // Set all 8 pixels to red (4-bit: r=F, g=0, b=0)
        for (int i = 0; i < 8; ++i) {
            unit.pixels.pixels[i] = bbc_colors::RED;
        }
        // Restore flags after pixel writes (fill might overwrite them)
        unit.set_flags(VIDEO_FLAG_DISPLAY);

        queue.push(unit);

        // Process
        size_t consumed = renderer.process(queue);
        CHECK(consumed == 1);
        CHECK(renderer.x() == 8);  // Advanced by 8 pixels
    }

    SECTION("HSYNC moves to next line") {
        // First: display unit
        PixelBatch display_unit{};
        display_unit.set_flags(VIDEO_FLAG_DISPLAY);
        queue.push(display_unit);
        renderer.process(queue);

        // HSYNC unit
        PixelBatch hsync_unit{};
        hsync_unit.set_flags(VIDEO_FLAG_HSYNC);
        queue.push(hsync_unit);
        renderer.process(queue);

        CHECK(renderer.x() == 0);
        CHECK(renderer.y() == 1);
    }

    SECTION("VSYNC resets to top") {
        // Simulate some scanlines - need distinct HSYNC pulses (rising edges)
        for (int i = 0; i < 10; ++i) {
            // First a non-HSYNC unit to create rising edge
            PixelBatch gap{};
            gap.clear();
            queue.push(gap);
            renderer.process(queue);

            // Then HSYNC unit
            PixelBatch hsync_unit{};
            hsync_unit.clear();
            hsync_unit.set_flags(VIDEO_FLAG_HSYNC);
            queue.push(hsync_unit);
            renderer.process(queue);
        }

        CHECK(renderer.y() == 10);

        // Gap before VSYNC
        PixelBatch gap{};
        gap.clear();
        queue.push(gap);
        renderer.process(queue);

        // VSYNC
        PixelBatch vsync_unit{};
        vsync_unit.clear();
        vsync_unit.set_flags(VIDEO_FLAG_VSYNC);
        queue.push(vsync_unit);
        renderer.process(queue);

        CHECK(renderer.y() == 0);
        CHECK(fb.version() == 1);  // Frame was swapped
    }

    SECTION("reset") {
        renderer.reset();
        CHECK(renderer.x() == 0);
        CHECK(renderer.y() == 0);
    }
}

// ============================================================================
// CRTC timing tests
// ============================================================================

TEST_CASE("Crtc6845 timing state machine", "[video]") {
    Crtc6845 crtc;

    SECTION("initial state after reset") {
        crtc.reset();
        auto output = crtc.tick();

        // First tick should be at address 0
        CHECK(output.address == 0);
    }

    SECTION("address increments") {
        crtc.reset();

        // Configure for a simple setup: 40 chars wide
        crtc.write(0, 0);  // Select R0
        crtc.write(1, 39); // R0 = 39 (40 chars total - 1)

        // Tick and check address increments
        auto out1 = crtc.tick();
        auto out2 = crtc.tick();

        // Address should increment (unless we're at line end)
        CHECK(out2.address >= out1.address);
    }

    SECTION("HSYNC generation") {
        crtc.reset();

        // Configure minimal setup
        crtc.write(0, 0);   // R0
        crtc.write(1, 7);   // 8 chars per line
        crtc.write(0, 2);   // R2 - HSYNC position
        crtc.write(1, 4);   // HSYNC at char 4
        crtc.write(0, 3);   // R3 - HSYNC width
        crtc.write(1, 0x02); // 2 chars wide

        // Tick through a line and look for HSYNC
        bool saw_hsync = false;
        for (int i = 0; i < 16; ++i) {
            auto output = crtc.tick();
            if (output.hsync) saw_hsync = true;
        }

        CHECK(saw_hsync);
    }

    SECTION("register read/write") {
        crtc.write(0, 12);  // Select R12 (start addr high)
        crtc.write(1, 0x30); // Set value

        crtc.write(0, 13);  // Select R13 (start addr low)
        crtc.write(1, 0x00); // Set value

        // Read back
        crtc.write(0, 12);
        CHECK(crtc.read(1) == 0x30);
    }
}

// ============================================================================
// Video ULA tests
// ============================================================================

TEST_CASE("VideoUla pixel generation", "[video]") {
    VideoUla ula;

    SECTION("initial state") {
        ula.reset();
        // Default should be some reasonable mode
    }

    SECTION("palette writes") {
        // Palette register is at offset 1 (not 0)
        // Upper nibble = logical color index, lower nibble = physical color
        // Write to palette: logical color 0 = physical color 7
        ula.write(1, 0x07);  // Palette write

        // Write to palette: logical color 1 = physical color 3
        ula.write(1, 0x13);

        // Physical color is XORed with 7, so:
        // 0x07 XOR 0x07 = 0 (black)
        // 0x03 XOR 0x07 = 4 (blue)
        CHECK(ula.palette(0) == 0);  // 7 XOR 7 = 0
        CHECK(ula.palette(1) == 4);  // 3 XOR 7 = 4
    }

    SECTION("control register") {
        // Set Mode 0 (8 pixels per byte, 2 colors)
        // Control register is at offset 0 (not 1)
        // 0x9C = 10011100: flash=0, teletext=0, line_width=3, fast_clock=1, cursor=4
        ula.write(0, 0x9C);  // Control register

        CHECK_FALSE(ula.teletext_mode());
        CHECK(ula.fast_clock());  // Modes 0-6 use fast clock
    }

    SECTION("teletext mode detection") {
        // Set Mode 7 (teletext)
        // Control register is at offset 0
        // 0x02 = teletext bit set
        ula.write(0, 0x02);  // Control for teletext

        CHECK(ula.teletext_mode());
        CHECK_FALSE(ula.fast_clock());  // Mode 7 uses slow clock
    }

    SECTION("emit blank produces black") {
        ula.reset();

        PixelBatch unit{};
        ula.emit_blank(unit);

        // Blank should produce black pixels
        for (int i = 0; i < 8; ++i) {
            CHECK(unit.pixels.pixels[i].value == 0);
        }
    }

    SECTION("emit pixels after byte") {
        ula.reset();

        // Feed a byte
        ula.byte(0xFF, false);

        // Emit to PixelBatch
        PixelBatch unit{};
        ula.emit_pixels(unit);

        // Should have emitted 8 pixels (values depend on mode/palette)
        // Just verify it doesn't crash and produces something
    }
}

// ============================================================================
// Integration test
// ============================================================================

TEST_CASE("Video pipeline integration", "[video][integration]") {
    // Create all components
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 64, 32);
    FrameRenderer renderer(&fb);
    OutputQueue<PixelBatch> queue(1024);
    Crtc6845 crtc;
    VideoUla ula;

    crtc.reset();
    ula.reset();
    fb.clear(0);

    SECTION("generate and render a scanline") {
        // Configure CRTC for small test
        crtc.write(0, 0);   // R0
        crtc.write(1, 7);   // 8 chars per line

        // Generate a few display cycles
        for (int i = 0; i < 8; ++i) {
            auto crtc_out = crtc.tick();

            // Feed ULA
            ula.byte(0xAA, crtc_out.cursor != 0);

            // Generate PixelBatch
            PixelBatch unit;
            if (crtc_out.display) {
                ula.emit_pixels(unit);
                unit.set_flags(VIDEO_FLAG_DISPLAY);
            } else {
                ula.emit_blank(unit);
            }

            if (crtc_out.hsync) unit.set_flags(unit.flags() | VIDEO_FLAG_HSYNC);
            if (crtc_out.vsync) unit.set_flags(unit.flags() | VIDEO_FLAG_VSYNC);

            // Push to queue
            queue.push(unit);
        }

        // Render
        size_t rendered = renderer.process(queue);
        CHECK(rendered == 8);
    }
}

// ============================================================================
// Boot screenshot test
// ============================================================================

#ifdef BEEBIUM_ROM_DIR

TEST_CASE("Boot screenshot capture", "[video][boot][integration]") {
    REQUIRE(roms_available());

    // Load ROMs
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "OS12.ROM");
    auto basic_rom = load_rom(rom_dirpath / "BASIC2.ROM");

    // Create machine with video output
    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Create video pipeline
    HeapFrameAllocator allocator;
    FrameBuffer fb(&allocator, 640, 512);  // Full PAL resolution
    FrameRenderer renderer(&fb);
    fb.clear(0);

    // Boot detection: look for "BBC Computer" at screen address 0x7C28
    // This is Mode 7 screen memory (0x7C00 base + offset)
    const uint16_t check_addr = 0x7C28;
    const char* expected_str = "BBC Computer";
    const size_t expected_len = 12;

    // Run boot sequence with periodic video drain
    constexpr uint64_t max_cycles = 3'000'000;  // ~1.5 seconds at 2MHz
    constexpr uint64_t drain_interval = 10'000;

    bool boot_complete = false;
    uint64_t drain_counter = 0;

    while (machine.cycle_count() < max_cycles && !boot_complete) {
        machine.step();
        ++drain_counter;

        // Periodically drain video queue to renderer
        if (drain_counter >= drain_interval) {
            drain_counter = 0;
            if (machine.memory().video_output.has_value()) {
                renderer.process(machine.memory().video_output.value());
            }
        }

        // Check for boot completion
        bool matches = true;
        for (size_t i = 0; i < expected_len && matches; ++i) {
            if (machine.read(check_addr + i) != static_cast<uint8_t>(expected_str[i])) {
                matches = false;
            }
        }
        boot_complete = matches;
    }

    REQUIRE(boot_complete);
    INFO("Boot completed at cycle " << machine.cycle_count());

    // After boot completes, create a fresh framebuffer and renderer
    HeapFrameAllocator fresh_allocator;
    FrameBuffer fresh_fb(&fresh_allocator, 640, 512);
    FrameRenderer fresh_renderer(&fresh_fb);

    // Drain current queue into fresh renderer
    if (machine.memory().video_output.has_value()) {
        fresh_renderer.process(machine.memory().video_output.value());
    }

    // Run enough cycles for 2 full frames to ensure we capture a complete one
    for (int i = 0; i < 80'000; ++i) {
        machine.step();
        if (machine.memory().video_output.has_value()) {
            fresh_renderer.process(machine.memory().video_output.value());
        }
    }

    // Force swap to ensure rendered frame is in read buffer
    fresh_fb.swap();

    // Use the fresh framebuffer for the screenshot
    auto& fb_for_screenshot = fresh_fb;

    // Create output directory
    auto output_dirpath = std::filesystem::path("test_output");
    std::filesystem::create_directories(output_dirpath);

    // Save screenshot
    auto frame = fb_for_screenshot.read_frame();
    auto output_filepath = output_dirpath / "boot_screenshot.ppm";
    bool saved = write_ppm(output_filepath, frame.data(), fb_for_screenshot.width(), fb_for_screenshot.height());

    REQUIRE(saved);
    INFO("Screenshot saved to " << output_filepath.string());

    // Verify file was created
    CHECK(std::filesystem::exists(output_filepath));
    CHECK(std::filesystem::file_size(output_filepath) > 0);

    // Verify Mode 7 is active
    CHECK(machine.memory().video_ula.teletext_mode());

    // Count pixel types in framebuffer to verify rendering worked correctly
    size_t white_count = 0;
    size_t grey_count = 0;
    size_t black_count = 0;

    for (size_t i = 0; i < fb_for_screenshot.width() * fb_for_screenshot.height(); ++i) {
        uint32_t pixel = frame[i];
        uint8_t r = (pixel >> 16) & 0xFF;
        uint8_t g = (pixel >> 8) & 0xFF;
        uint8_t b = pixel & 0xFF;

        if (r == 255 && g == 255 && b == 255) {
            ++white_count;
        } else if (r == 0 && g == 0 && b == 0) {
            ++black_count;
        } else if (r == g && g == b && r > 0) {
            // Grey pixel (from gamma blending)
            ++grey_count;
        }
    }

    // The boot screen should have visible text
    CHECK(white_count > 500);   // At least 500 white pixels for "BBC Computer 32K" text
    CHECK(black_count > 10000); // Mostly black background

    // With gamma blending enabled, we should have grey pixels at character edges
    // This verifies the SAA5050 antialiasing and 6→8 pixel expansion is working
    CHECK(grey_count > 100);    // At least 100 grey pixels from blending
}

TEST_CASE("Teletext rendering produces blended pixels", "[video][boot][teletext]") {
    REQUIRE(roms_available());

    // Load ROMs
    const auto rom_dirpath = std::filesystem::path(BEEBIUM_ROM_DIR);
    auto mos_rom = load_rom(rom_dirpath / "OS12.ROM");
    auto basic_rom = load_rom(rom_dirpath / "BASIC2.ROM");

    // Create machine with video output
    ModelB machine;
    machine.memory().load_mos(mos_rom.data(), mos_rom.size());
    machine.memory().load_basic(basic_rom.data(), basic_rom.size());
    machine.memory().enable_video_output();
    machine.reset();

    // Boot until "BBC Computer" appears
    const uint16_t check_addr = 0x7C28;
    const char* expected_str = "BBC Computer";
    bool boot_complete = false;

    for (uint64_t i = 0; i < 3'000'000 && !boot_complete; ++i) {
        machine.step();

        bool matches = true;
        for (size_t j = 0; expected_str[j] && matches; ++j) {
            if (machine.read(check_addr + j) != static_cast<uint8_t>(expected_str[j])) {
                matches = false;
            }
        }
        boot_complete = matches;
    }
    REQUIRE(boot_complete);
    REQUIRE(machine.memory().video_ula.teletext_mode());

    // Verify screen memory contains expected "BBC Computer" text
    CHECK(machine.read(0x7C28) == 'B');
    CHECK(machine.read(0x7C29) == 'B');
    CHECK(machine.read(0x7C2A) == 'C');

    // Drain old queue data and run more cycles to get fresh video
    auto& queue = machine.memory().video_output.value();
    queue.consume(queue.size());  // Clear stale data

    // Run enough cycles for 2 full frames (~80,000 steps at 2MHz = ~1 frame at 50Hz)
    for (int i = 0; i < 80'000; ++i) {
        machine.step();
    }

    // Get video queue and count pixel types
    REQUIRE(machine.memory().video_output.has_value());

    auto buffers = queue.get_consumer_buffer();
    REQUIRE(buffers.total() > 0);

    size_t white_count = 0;
    size_t black_count = 0;
    size_t grey_count = 0;
    size_t display_batches = 0;

    auto count_pixels = [&](const PixelBatch& batch) {
        if (!(batch.flags() & VIDEO_FLAG_DISPLAY)) return;
        ++display_batches;

        for (int p = 0; p < 8; ++p) {
            const auto& pixel = batch.pixels.pixels[p];
            bool is_white = pixel.bits.r == 15 && pixel.bits.g == 15 && pixel.bits.b == 15;
            bool is_black = pixel.bits.r == 0 && pixel.bits.g == 0 && pixel.bits.b == 0;

            if (is_white) ++white_count;
            else if (is_black) ++black_count;
            else ++grey_count;
        }
    };

    for (size_t i = 0; i < buffers.a.size(); ++i) {
        count_pixels(buffers.a[i]);
    }
    for (size_t i = 0; i < buffers.b.size(); ++i) {
        count_pixels(buffers.b[i]);
    }

    // Verify we have some text (white pixels on black background)
    CHECK(white_count > 0);
    CHECK(black_count > 0);

    // With gamma blending, we should have grey pixels at text edges
    // This verifies the SAA5050 6→8 pixel expansion is working correctly
    CHECK(grey_count > 0);
}

#endif // BEEBIUM_ROM_DIR
