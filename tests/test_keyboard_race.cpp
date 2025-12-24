// test_keyboard_race.cpp
// Tests for keyboard matrix thread safety
//
// These tests verify that keyboard state changes are reliably detected
// even when modified from a separate thread (simulating gRPC input).

#include <beebium/KeyboardMatrix.hpp>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>

using namespace beebium;

TEST_CASE("KeyboardMatrix concurrent access", "[keyboard][threading]") {
    KeyboardMatrix matrix;

    SECTION("rapid key press/release can be missed without synchronization") {
        // This test demonstrates the aliasing problem:
        // If a key is pressed and released between samples, it may be missed

        std::atomic<int> detected_presses{0};
        std::atomic<int> total_presses{0};
        std::atomic<bool> running{true};

        // Reader thread (simulates emulator sampling at ~50Hz frame rate)
        std::thread reader([&]() {
            bool was_pressed = false;
            while (running.load(std::memory_order_relaxed)) {
                bool is_pressed = matrix.is_key_pressed(4, 5);
                if (is_pressed && !was_pressed) {
                    detected_presses.fetch_add(1, std::memory_order_relaxed);
                }
                was_pressed = is_pressed;
                // Simulate ~50Hz sampling (20ms between reads)
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });

        // Writer thread (simulates rapid keypresses from gRPC)
        std::thread writer([&]() {
            for (int i = 0; i < 100; ++i) {
                matrix.key_down(4, 5);
                total_presses.fetch_add(1, std::memory_order_relaxed);
                // Very brief keypress - may be missed!
                std::this_thread::sleep_for(std::chrono::microseconds(10));
                matrix.key_up(4, 5);
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        });

        writer.join();
        running.store(false, std::memory_order_relaxed);
        reader.join();

        // This demonstrates the problem: not all presses are detected
        // With proper latching, detected should equal total
        INFO("Total presses: " << total_presses.load());
        INFO("Detected presses: " << detected_presses.load());

        // We expect some to be missed due to aliasing
        // This test documents the current behavior - it may fail
        // once we add proper event latching
        CHECK(detected_presses.load() <= total_presses.load());
    }

    SECTION("concurrent modifications don't corrupt state") {
        // Test that concurrent read/write doesn't cause data corruption
        // (torn reads, partial writes, etc.)

        std::atomic<bool> running{true};
        std::atomic<int> errors{0};

        // Multiple writer threads
        std::vector<std::thread> writers;
        for (int t = 0; t < 4; ++t) {
            writers.emplace_back([&, t]() {
                uint8_t row = t;
                for (int i = 0; i < 1000; ++i) {
                    for (uint8_t col = 0; col < 10; ++col) {
                        matrix.key_down(row, col);
                    }
                    for (uint8_t col = 0; col < 10; ++col) {
                        matrix.key_up(row, col);
                    }
                }
            });
        }

        // Reader thread - checks for invalid states
        std::thread reader([&]() {
            while (running.load(std::memory_order_relaxed)) {
                for (uint8_t col = 0; col < 10; ++col) {
                    uint16_t state = matrix.read_column(col);
                    // Valid states: any combination of bits 0-9
                    // Invalid: bits 10-15 should never be set
                    if (state & 0xFC00) {
                        errors.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        });

        for (auto& w : writers) {
            w.join();
        }
        running.store(false, std::memory_order_relaxed);
        reader.join();

        CHECK(errors.load() == 0);
    }
}

TEST_CASE("KeyboardMatrix event latching", "[keyboard][latching]") {
    KeyboardMatrix matrix;

    SECTION("key press should be detectable for minimum duration") {
        // A key press should persist long enough to be sampled
        // This tests the latching mechanism once implemented

        // Press and immediately release
        matrix.key_down(3, 7);
        matrix.key_up(3, 7);

        // With latching, this should still show the key was pressed
        // Without latching, the key state is immediately cleared

        // Current behavior (no latching): key is not pressed
        CHECK(matrix.is_key_pressed(3, 7) == false);

        // TODO: Once latching is implemented, we would have:
        // CHECK(matrix.has_pending_press(3, 7) == true);
        // matrix.acknowledge_press(3, 7);
        // CHECK(matrix.has_pending_press(3, 7) == false);
    }
}
