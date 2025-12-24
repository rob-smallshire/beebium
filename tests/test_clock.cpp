// Test compile-time clock subscription system

#include <catch2/catch_test_macros.hpp>
#include <beebium/Clock.hpp>

using namespace beebium;

namespace {

// Test device that subscribes to both edges at 2MHz
struct BothEdgesDevice {
    static constexpr ClockEdge clock_edges = ClockEdge::Both;
    static constexpr ClockRate clock_rate = ClockRate::Rate_2MHz;

    int rising_count = 0;
    int falling_count = 0;

    void tick_rising() { ++rising_count; }
    void tick_falling() { ++falling_count; }
};

// Test device that subscribes only to falling edge at 1MHz
struct FallingOnly1MHz {
    static constexpr ClockEdge clock_edges = ClockEdge::Falling;
    static constexpr ClockRate clock_rate = ClockRate::Rate_1MHz;

    int falling_count = 0;

    void tick_falling() { ++falling_count; }
};

// Test device that subscribes only to rising edge at 2MHz
struct RisingOnly2MHz {
    static constexpr ClockEdge clock_edges = ClockEdge::Rising;
    static constexpr ClockRate clock_rate = ClockRate::Rate_2MHz;

    int rising_count = 0;

    void tick_rising() { ++rising_count; }
};

// Test device with dynamic clock rate
struct DynamicRateDevice {
    static constexpr ClockEdge clock_edges = ClockEdge::Falling;

    bool fast_mode = false;
    int falling_count = 0;

    ClockRate clock_rate() const {
        return fast_mode ? ClockRate::Rate_2MHz : ClockRate::Rate_1MHz;
    }

    void tick_falling() { ++falling_count; }
};

} // anonymous namespace

TEST_CASE("Clock dispatches to both edges at 2MHz", "[clock]") {
    BothEdgesDevice device;
    auto clock = make_clock(make_clock_binding(device));

    // Cycle 0: falling edge (even)
    clock.tick(0);
    CHECK(device.rising_count == 0);
    CHECK(device.falling_count == 1);

    // Cycle 1: rising edge (odd)
    clock.tick(1);
    CHECK(device.rising_count == 1);
    CHECK(device.falling_count == 1);

    // Cycle 2: falling edge
    clock.tick(2);
    CHECK(device.rising_count == 1);
    CHECK(device.falling_count == 2);

    // Cycle 3: rising edge
    clock.tick(3);
    CHECK(device.rising_count == 2);
    CHECK(device.falling_count == 2);
}

TEST_CASE("Clock dispatches falling edge only at 1MHz", "[clock]") {
    FallingOnly1MHz device;
    auto clock = make_clock(make_clock_binding(device));

    // Run 8 cycles (4 at 2MHz = 2 at 1MHz for falling edges)
    for (uint64_t i = 0; i < 8; ++i) {
        clock.tick(i);
    }

    // 1MHz falling edge occurs on even cycles: 0, 2, 4, 6
    // That's 4 falling edges
    CHECK(device.falling_count == 4);
}

TEST_CASE("Clock dispatches rising edge only at 2MHz", "[clock]") {
    RisingOnly2MHz device;
    auto clock = make_clock(make_clock_binding(device));

    // Run 8 cycles
    for (uint64_t i = 0; i < 8; ++i) {
        clock.tick(i);
    }

    // Rising edge occurs on odd cycles: 1, 3, 5, 7
    CHECK(device.rising_count == 4);
}

TEST_CASE("Clock handles dynamic clock rate", "[clock]") {
    DynamicRateDevice device;
    auto clock = make_clock(make_clock_binding(device));

    // Start in 1MHz mode
    device.fast_mode = false;

    // Run 4 cycles - should get 2 falling edges (cycles 0, 2)
    for (uint64_t i = 0; i < 4; ++i) {
        clock.tick(i);
    }
    CHECK(device.falling_count == 2);

    // Switch to 2MHz mode
    device.fast_mode = true;

    // Run 4 more cycles - should get 2 more falling edges (cycles 4, 6)
    // But in 2MHz mode, we tick every cycle's falling edge
    for (uint64_t i = 4; i < 8; ++i) {
        clock.tick(i);
    }
    // Falling edges at cycles 4, 6 in 2MHz mode = 2 more
    CHECK(device.falling_count == 4);
}

TEST_CASE("Clock with multiple subscribers", "[clock]") {
    BothEdgesDevice device1;
    FallingOnly1MHz device2;
    RisingOnly2MHz device3;

    auto clock = make_clock(
        make_clock_binding(device1),
        make_clock_binding(device2),
        make_clock_binding(device3)
    );

    // Run 4 cycles
    for (uint64_t i = 0; i < 4; ++i) {
        clock.tick(i);
    }

    // device1 (both edges, 2MHz): 2 rising, 2 falling
    CHECK(device1.rising_count == 2);
    CHECK(device1.falling_count == 2);

    // device2 (falling only, 1MHz): 2 falling (cycles 0, 2)
    CHECK(device2.falling_count == 2);

    // device3 (rising only, 2MHz): 2 rising (cycles 1, 3)
    CHECK(device3.rising_count == 2);
}

TEST_CASE("Clock edge pattern matches BBC Micro timing", "[clock]") {
    // Verify the edge pattern matches BBC Micro's 2MHz clock
    // Cycle 0: falling (even) - memory access, 1MHz falling
    // Cycle 1: rising (odd)  - 1MHz rising
    // Cycle 2: falling (even) - memory access, 1MHz falling
    // Cycle 3: rising (odd)  - 1MHz rising
    // ...

    BothEdgesDevice device;
    auto clock = make_clock(make_clock_binding(device));

    // Check edge pattern
    for (uint64_t cycle = 0; cycle < 10; ++cycle) {
        int prev_rising = device.rising_count;
        int prev_falling = device.falling_count;

        clock.tick(cycle);

        if (cycle & 1) {
            // Odd cycle: rising edge
            CHECK(device.rising_count == prev_rising + 1);
            CHECK(device.falling_count == prev_falling);
        } else {
            // Even cycle: falling edge
            CHECK(device.rising_count == prev_rising);
            CHECK(device.falling_count == prev_falling + 1);
        }
    }
}

TEST_CASE("Clock size reflects number of subscribers", "[clock]") {
    BothEdgesDevice d1;
    FallingOnly1MHz d2;

    auto clock1 = make_clock(make_clock_binding(d1));
    CHECK(clock1.size() == 1);

    auto clock2 = make_clock(make_clock_binding(d1), make_clock_binding(d2));
    CHECK(clock2.size() == 2);
}
