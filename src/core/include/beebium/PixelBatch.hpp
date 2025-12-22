#ifndef BEEBIUM_PIXEL_BATCH_HPP
#define BEEBIUM_PIXEL_BATCH_HPP

#include <cstdint>

namespace beebium {

// Pixel batch types - stored in pixels[0].x field
enum class PixelBatchType : uint8_t {
    Nothing = 0,      // No video data (blanking)
    Bitmap = 1,       // Standard bitmap modes (0-6)
    Teletext = 2,     // Mode 7 teletext
};

// Video data flags - stored in pixels[1].x field
enum VideoDataFlag : uint8_t {
    VIDEO_FLAG_NONE = 0,
    VIDEO_FLAG_HSYNC = 0x01,      // Horizontal sync active
    VIDEO_FLAG_VSYNC = 0x02,      // Vertical sync active
    VIDEO_FLAG_DISPLAY = 0x04,    // Display enable (visible area)
    VIDEO_FLAG_INTERLACE = 0x08,  // Odd field of interlaced frame
};

// A single pixel with 4-bit RGB and 4-bit metadata
// Layout: bits 0-3: blue, 4-7: green, 8-11: red, 12-15: x (metadata)
struct VideoDataPixelBits {
    uint16_t b : 4;  // Blue (0-15)
    uint16_t g : 4;  // Green (0-15)
    uint16_t r : 4;  // Red (0-15)
    uint16_t x : 4;  // Metadata/type field
};

union VideoDataPixel {
    VideoDataPixelBits bits;
    uint16_t value;

    constexpr VideoDataPixel() : value(0) {}
    constexpr VideoDataPixel(uint16_t v) : value(v) {}
    constexpr VideoDataPixel(uint8_t r, uint8_t g, uint8_t b, uint8_t x = 0)
        : value(static_cast<uint16_t>((b & 0xF) | ((g & 0xF) << 4) |
                                       ((r & 0xF) << 8) | ((x & 0xF) << 12))) {}
};
static_assert(sizeof(VideoDataPixel) == 2, "VideoDataPixel must be 2 bytes");

// 8 pixels packed together (16 bytes) - represents 0.5us of video output
// Can be accessed as two 64-bit values for efficient copying
union PixelBatchArray {
    VideoDataPixel pixels[8];
    uint64_t values[2];

    constexpr PixelBatchArray() : values{0, 0} {}
};
static_assert(sizeof(PixelBatchArray) == 16, "PixelBatchArray must be 16 bytes");

// Optional debug metadata (enabled at compile time)
#ifndef BEEBIUM_VIDEO_TRACK_METADATA
#define BEEBIUM_VIDEO_TRACK_METADATA 0
#endif

#if BEEBIUM_VIDEO_TRACK_METADATA
struct PixelBatchMetadata {
    uint16_t screen_address = 0;  // Screen memory address being read
    uint16_t crtc_address = 0;    // CRTC memory address output
    uint8_t raster_line = 0;      // Current raster line within character
    uint8_t flags = 0;            // Debug flags
};
#endif

// A batch of 8 pixels - 0.5us of video output
// Emitted by the core at 2MHz rate (one per CPU clock cycle)
struct PixelBatch {
    PixelBatchArray pixels;
#if BEEBIUM_VIDEO_TRACK_METADATA
    PixelBatchMetadata metadata;
#endif

    // Set the data type in pixels[0].x
    void set_type(PixelBatchType type) {
        pixels.pixels[0].bits.x = static_cast<uint8_t>(type);
    }

    PixelBatchType type() const {
        return static_cast<PixelBatchType>(pixels.pixels[0].bits.x);
    }

    // Set flags in pixels[1].x
    void set_flags(uint8_t flags) {
        pixels.pixels[1].bits.x = flags;
    }

    uint8_t flags() const {
        return pixels.pixels[1].bits.x;
    }

    // Convenience flag accessors
    bool hsync() const { return (flags() & VIDEO_FLAG_HSYNC) != 0; }
    bool vsync() const { return (flags() & VIDEO_FLAG_VSYNC) != 0; }
    bool display_enable() const { return (flags() & VIDEO_FLAG_DISPLAY) != 0; }

    // Fill all 8 pixels with a single color (for blanking, borders, etc.)
    void fill(VideoDataPixel color) {
        for (int i = 0; i < 8; ++i) {
            pixels.pixels[i] = color;
        }
    }

    // Clear to black
    void clear() {
        pixels.values[0] = 0;
        pixels.values[1] = 0;
    }
};

#if BEEBIUM_VIDEO_TRACK_METADATA
static_assert(sizeof(PixelBatch) == 24, "PixelBatch with metadata must be 24 bytes");
#else
static_assert(sizeof(PixelBatch) == 16, "PixelBatch must be 16 bytes");
#endif

// Standard BBC Micro physical colors (as 4-bit RGB values)
// The BBC has 8 physical colors: 0-7 map to 3-bit RGB
namespace bbc_colors {
    constexpr VideoDataPixel BLACK   {0, 0, 0};
    constexpr VideoDataPixel RED     {15, 0, 0};
    constexpr VideoDataPixel GREEN   {0, 15, 0};
    constexpr VideoDataPixel YELLOW  {15, 15, 0};
    constexpr VideoDataPixel BLUE    {0, 0, 15};
    constexpr VideoDataPixel MAGENTA {15, 0, 15};
    constexpr VideoDataPixel CYAN    {0, 15, 15};
    constexpr VideoDataPixel WHITE   {15, 15, 15};

    // Physical color lookup table (index 0-7)
    constexpr VideoDataPixel PALETTE[8] = {
        BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE
    };
}

} // namespace beebium

#endif // BEEBIUM_PIXEL_BATCH_HPP
