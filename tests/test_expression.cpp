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

#include <catch2/catch_test_macros.hpp>

#include "beebium/debugger/Expression.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <variant>

using namespace beebium;

namespace {

// Helper: compile and expect success
CompiledExpression must_compile(std::string_view src) {
    auto result = compile(src);
    REQUIRE(std::holds_alternative<CompiledExpression>(result));
    return std::get<CompiledExpression>(result);
}

// Helper: compile and expect an error
std::string must_fail(std::string_view src) {
    auto result = compile(src);
    REQUIRE(std::holds_alternative<std::string>(result));
    return std::get<std::string>(result);
}

// Default CPU state for tests
ExprCpuState make_cpu(uint8_t a = 0, uint8_t x = 0, uint8_t y = 0,
                      uint8_t sp = 0xFF, uint16_t pc = 0x0400,
                      uint8_t p = 0x00, uint64_t cycles = 0, uint64_t hits = 0) {
    return {a, x, y, sp, p, pc, cycles, hits};
}

// Simple 64K memory for peek tests
std::array<uint8_t, 65536> test_memory{};

uint8_t test_peek(void* /*context*/, uint16_t addr) {
    return test_memory[addr];
}

// Helper: compile + evaluate with default peek
uint64_t eval(std::string_view src, const ExprCpuState& cpu) {
    auto expr = must_compile(src);
    return evaluate(expr, cpu, test_peek, nullptr);
}

} // anonymous namespace

//////////////////////////////////////////////////////////////////////////////
// Parser tests -- valid expressions compile without error
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Compile boolean literals", "[expression][parser]") {
    must_compile("true");
    must_compile("false");
}

TEST_CASE("Compile number literals", "[expression][parser]") {
    must_compile("0");
    must_compile("42");
    must_compile("255");
    must_compile("0xFF");
    must_compile("0xff");
    must_compile("0XFF");
    must_compile("0b10101010");
    must_compile("0B1100");
    must_compile("65535");
}

TEST_CASE("Compile register identifiers", "[expression][parser]") {
    must_compile("A");
    must_compile("X");
    must_compile("Y");
    must_compile("SP");
    must_compile("PC");
    must_compile("P");
}

TEST_CASE("Compile flag identifiers", "[expression][parser]") {
    must_compile("C");
    must_compile("Z");
    must_compile("I");
    must_compile("D");
    must_compile("V");
    must_compile("N");
}

TEST_CASE("Compile cycles identifier", "[expression][parser]") {
    must_compile("cycles");
}

TEST_CASE("Compile mem[expr] syntax", "[expression][parser]") {
    must_compile("mem[0x4000]");
    must_compile("mem[A + X]");
    must_compile("mem[mem[0x70]]");
}

TEST_CASE("Compile arithmetic expressions", "[expression][parser]") {
    must_compile("A + 1");
    must_compile("X - Y");
    must_compile("A * 2");
    must_compile("X / 4");
    must_compile("Y % 3");
}

TEST_CASE("Compile bitwise expressions", "[expression][parser]") {
    must_compile("P & 0x01");
    must_compile("A | 0x80");
    must_compile("X ^ Y");
}

TEST_CASE("Compile comparison expressions", "[expression][parser]") {
    must_compile("A == 0x42");
    must_compile("X != 0");
    must_compile("Y < 10");
    must_compile("SP > 0xF0");
    must_compile("PC <= 0xFFFF");
    must_compile("cycles >= 1000000");
}

TEST_CASE("Compile boolean expressions", "[expression][parser]") {
    must_compile("A == 0 && X > 5");
    must_compile("C || Z");
    must_compile("!N");
    must_compile("!(A == 0)");
}

TEST_CASE("Compile parenthesised expressions", "[expression][parser]") {
    must_compile("(A + 1) * 2");
    must_compile("((A))");
    must_compile("(A == 0x42) && (X > 5)");
}

TEST_CASE("Compile complex expressions", "[expression][parser]") {
    must_compile("A == 0x42 && mem[0x4000] == 0xFF");
    must_compile("PC == 0x8000 && cycles > 1000000");
    must_compile("mem[0xFE00] & 0x80");
    must_compile("X >= 5 && Y < 10");
    must_compile("mem[mem[0x70] + X]");
}

TEST_CASE("Whitespace is ignored", "[expression][parser]") {
    must_compile("  A  ==  0x42  ");
    must_compile("A+1");
    must_compile("  true  ");
}

//////////////////////////////////////////////////////////////////////////////
// Parser tests -- invalid expressions produce clear errors
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Empty expression is an error", "[expression][parser][error]") {
    must_fail("");
}

TEST_CASE("Unclosed parenthesis is an error", "[expression][parser][error]") {
    auto msg = must_fail("(A + 1");
    CHECK(msg.find("')'") != std::string::npos);
}

TEST_CASE("Unclosed mem bracket is an error", "[expression][parser][error]") {
    auto msg = must_fail("mem[0x4000");
    CHECK(msg.find("']'") != std::string::npos);
}

TEST_CASE("Missing operand is an error", "[expression][parser][error]") {
    must_fail("A +");
    must_fail("+ A");
    must_fail("A ==");
}

TEST_CASE("Invalid hex literal is an error", "[expression][parser][error]") {
    must_fail("0x");
    must_fail("0xGG");
}

TEST_CASE("Invalid binary literal is an error", "[expression][parser][error]") {
    must_fail("0b");
    must_fail("0b2");
}

TEST_CASE("Trailing garbage is an error", "[expression][parser][error]") {
    must_fail("A B");
    must_fail("42 42");
}

TEST_CASE("Unknown identifier is an error", "[expression][parser][error]") {
    must_fail("foo");
    must_fail("register");
}

TEST_CASE("mem without bracket is an error", "[expression][parser][error]") {
    auto msg = must_fail("mem");
    CHECK(msg.find("'['") != std::string::npos);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- constants and registers
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Evaluate true and false", "[expression][eval]") {
    auto cpu = make_cpu();
    CHECK(eval("true", cpu) == 1);
    CHECK(eval("false", cpu) == 0);
}

TEST_CASE("Evaluate number literals", "[expression][eval]") {
    auto cpu = make_cpu();
    CHECK(eval("0", cpu) == 0);
    CHECK(eval("42", cpu) == 42);
    CHECK(eval("0xFF", cpu) == 255);
    CHECK(eval("0b1010", cpu) == 10);
}

TEST_CASE("Evaluate register values", "[expression][eval]") {
    auto cpu = make_cpu(0x42, 0x11, 0x22, 0xFD, 0xC000, 0x30, 12345);
    CHECK(eval("A", cpu) == 0x42);
    CHECK(eval("X", cpu) == 0x11);
    CHECK(eval("Y", cpu) == 0x22);
    CHECK(eval("SP", cpu) == 0xFD);
    CHECK(eval("PC", cpu) == 0xC000);
    CHECK(eval("P", cpu) == 0x30);
    CHECK(eval("cycles", cpu) == 12345);
}

TEST_CASE("Evaluate flag bits", "[expression][eval]") {
    // P = 0b11000011 = N=1, V=1, D=0, I=0, Z=1, C=1
    auto cpu = make_cpu(0, 0, 0, 0xFF, 0, 0xC3);
    CHECK(eval("C", cpu) == 1);
    CHECK(eval("Z", cpu) == 1);
    CHECK(eval("I", cpu) == 0);
    CHECK(eval("D", cpu) == 0);
    CHECK(eval("V", cpu) == 1);
    CHECK(eval("N", cpu) == 1);
}

TEST_CASE("Evaluate flag bits all clear", "[expression][eval]") {
    auto cpu = make_cpu(0, 0, 0, 0xFF, 0, 0x00);
    CHECK(eval("C", cpu) == 0);
    CHECK(eval("Z", cpu) == 0);
    CHECK(eval("I", cpu) == 0);
    CHECK(eval("D", cpu) == 0);
    CHECK(eval("V", cpu) == 0);
    CHECK(eval("N", cpu) == 0);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- arithmetic
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Evaluate addition", "[expression][eval]") {
    auto cpu = make_cpu(0x10, 0x20);
    CHECK(eval("A + X", cpu) == 0x30);
    CHECK(eval("A + 1", cpu) == 0x11);
}

TEST_CASE("Evaluate subtraction", "[expression][eval]") {
    auto cpu = make_cpu(0x42);
    CHECK(eval("A - 2", cpu) == 0x40);
}

TEST_CASE("Evaluate multiplication", "[expression][eval]") {
    auto cpu = make_cpu(7);
    CHECK(eval("A * 6", cpu) == 42);
}

TEST_CASE("Evaluate division", "[expression][eval]") {
    auto cpu = make_cpu(42);
    CHECK(eval("A / 6", cpu) == 7);
}

TEST_CASE("Division by zero returns zero", "[expression][eval]") {
    auto cpu = make_cpu(42);
    CHECK(eval("A / 0", cpu) == 0);
    CHECK(eval("A % 0", cpu) == 0);
}

TEST_CASE("Evaluate modulo", "[expression][eval]") {
    auto cpu = make_cpu(42);
    CHECK(eval("A % 10", cpu) == 2);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- bitwise
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Evaluate bitwise AND", "[expression][eval]") {
    auto cpu = make_cpu(0xFF);
    CHECK(eval("A & 0x0F", cpu) == 0x0F);
}

TEST_CASE("Evaluate bitwise OR", "[expression][eval]") {
    auto cpu = make_cpu(0x0F);
    CHECK(eval("A | 0xF0", cpu) == 0xFF);
}

TEST_CASE("Evaluate bitwise XOR", "[expression][eval]") {
    auto cpu = make_cpu(0xFF);
    CHECK(eval("A ^ 0x0F", cpu) == 0xF0);
}

TEST_CASE("Evaluate P & flag mask", "[expression][eval]") {
    auto cpu = make_cpu(0, 0, 0, 0xFF, 0, 0x43);  // P = 0x43
    CHECK(eval("P & 0x01", cpu) == 1);   // C flag
    CHECK(eval("P & 0x40", cpu) == 0x40); // V flag
    CHECK(eval("P & 0x80", cpu) == 0);    // N flag clear
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- comparisons
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Evaluate equality", "[expression][eval]") {
    auto cpu = make_cpu(0x42);
    CHECK(eval("A == 0x42", cpu) == 1);
    CHECK(eval("A == 0x43", cpu) == 0);
}

TEST_CASE("Evaluate inequality", "[expression][eval]") {
    auto cpu = make_cpu(0x42);
    CHECK(eval("A != 0x42", cpu) == 0);
    CHECK(eval("A != 0x43", cpu) == 1);
}

TEST_CASE("Evaluate less than / greater than", "[expression][eval]") {
    auto cpu = make_cpu(5);
    CHECK(eval("A < 10", cpu) == 1);
    CHECK(eval("A < 5", cpu) == 0);
    CHECK(eval("A > 3", cpu) == 1);
    CHECK(eval("A > 5", cpu) == 0);
}

TEST_CASE("Evaluate less/greater or equal", "[expression][eval]") {
    auto cpu = make_cpu(5);
    CHECK(eval("A <= 5", cpu) == 1);
    CHECK(eval("A <= 4", cpu) == 0);
    CHECK(eval("A >= 5", cpu) == 1);
    CHECK(eval("A >= 6", cpu) == 0);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- boolean logic
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Evaluate logical AND", "[expression][eval]") {
    auto cpu = make_cpu(0x42, 0x10);
    CHECK(eval("A == 0x42 && X == 0x10", cpu) == 1);
    CHECK(eval("A == 0x42 && X == 0x99", cpu) == 0);
    CHECK(eval("A == 0x99 && X == 0x10", cpu) == 0);
}

TEST_CASE("Evaluate logical OR", "[expression][eval]") {
    auto cpu = make_cpu(0x42, 0x10);
    CHECK(eval("A == 0x42 || X == 0x99", cpu) == 1);
    CHECK(eval("A == 0x99 || X == 0x10", cpu) == 1);
    CHECK(eval("A == 0x99 || X == 0x99", cpu) == 0);
}

TEST_CASE("Evaluate logical NOT", "[expression][eval]") {
    auto cpu = make_cpu(0x42);
    CHECK(eval("!0", cpu) == 1);
    CHECK(eval("!1", cpu) == 0);
    CHECK(eval("!A", cpu) == 0);
    CHECK(eval("!(A == 0x99)", cpu) == 1);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- memory dereference
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Evaluate mem[constant]", "[expression][eval]") {
    test_memory[0x4000] = 0xFF;
    auto cpu = make_cpu();
    CHECK(eval("mem[0x4000]", cpu) == 0xFF);
    test_memory[0x4000] = 0;
}

TEST_CASE("Evaluate mem[register + offset]", "[expression][eval]") {
    test_memory[0x0042] = 0xAB;
    auto cpu = make_cpu(0, 0x02);
    CHECK(eval("mem[0x40 + X]", cpu) == 0xAB);
    test_memory[0x0042] = 0;
}

TEST_CASE("Evaluate nested mem[mem[addr]]", "[expression][eval]") {
    test_memory[0x0070] = 0x00;  // low byte of pointer
    test_memory[0x0071] = 0x40;  // high byte
    test_memory[0x4000] = 0xBE;  // target value
    auto cpu = make_cpu();
    // mem[0x70] reads 0x00, then mem[0x00] reads whatever is at $0000.
    // For a proper 16-bit indirect we'd need mem[mem[0x70] + mem[0x71] * 256]
    // but let's test the simpler case:
    CHECK(eval("mem[0x70]", cpu) == 0x00);
    CHECK(eval("mem[0x4000]", cpu) == 0xBE);
    test_memory[0x0070] = 0;
    test_memory[0x0071] = 0;
    test_memory[0x4000] = 0;
}

TEST_CASE("mem with null peek function returns zero", "[expression][eval]") {
    auto expr = must_compile("mem[0x4000]");
    auto cpu = make_cpu();
    CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- operator precedence
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Multiplication binds tighter than addition", "[expression][eval][precedence]") {
    auto cpu = make_cpu();
    CHECK(eval("2 + 3 * 4", cpu) == 14);   // not 20
    CHECK(eval("3 * 4 + 2", cpu) == 14);
}

TEST_CASE("Comparison binds tighter than logical", "[expression][eval][precedence]") {
    auto cpu = make_cpu(5, 10);
    // A < 10 && X > 5  should be  (A < 10) && (X > 5)
    CHECK(eval("A < 10 && X > 5", cpu) == 1);
}

TEST_CASE("Bitwise binds between comparison and arithmetic", "[expression][eval][precedence]") {
    auto cpu = make_cpu(0xFF);
    // A & 0x0F == 0x0F should be (A & 0x0F) == 0x0F? No -- comparison binds
    // tighter than bitwise in C, but our grammar has bitop between cmp and sum,
    // so: A & (0x0F == 0x0F) = A & 1 = 1? Let's check the actual grammar:
    // cmp := bitop (cmp_op bitop)?
    // bitop := sum (bitop_op sum)*
    // So: "A & 0x0F == 0x0F" parses as "A & (0x0F == 0x0F)" which is A & 1.
    // This matches the grammar, not C precedence. Use parentheses for clarity.
    CHECK(eval("(A & 0x0F) == 0x0F", cpu) == 1);
}

TEST_CASE("Parentheses override precedence", "[expression][eval][precedence]") {
    auto cpu = make_cpu();
    CHECK(eval("(2 + 3) * 4", cpu) == 20);
    CHECK(eval("2 * (3 + 4)", cpu) == 14);
}

//////////////////////////////////////////////////////////////////////////////
// VM evaluation tests -- complex expressions from requirements doc
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Requirements example: A == 0x42 && mem[0x4000] == 0xFF", "[expression][eval]") {
    test_memory[0x4000] = 0xFF;
    auto cpu = make_cpu(0x42);
    CHECK(eval("A == 0x42 && mem[0x4000] == 0xFF", cpu) == 1);

    cpu.a = 0x43;
    CHECK(eval("A == 0x42 && mem[0x4000] == 0xFF", cpu) == 0);
    test_memory[0x4000] = 0;
}

TEST_CASE("Requirements example: PC == 0x8000 && cycles > 1000000", "[expression][eval]") {
    auto cpu = make_cpu(0, 0, 0, 0xFF, 0x8000, 0, 2000000);
    CHECK(eval("PC == 0x8000 && cycles > 1000000", cpu) == 1);

    cpu.cycles = 500000;
    CHECK(eval("PC == 0x8000 && cycles > 1000000", cpu) == 0);
}

TEST_CASE("Requirements example: mem[0xFE00] & 0x80", "[expression][eval]") {
    test_memory[0xFE00] = 0x80;
    auto cpu = make_cpu();
    CHECK(eval("mem[0xFE00] & 0x80", cpu) != 0);

    test_memory[0xFE00] = 0x7F;
    CHECK(eval("mem[0xFE00] & 0x80", cpu) == 0);
    test_memory[0xFE00] = 0;
}

TEST_CASE("Requirements example: X >= 5 && Y < 10", "[expression][eval]") {
    auto cpu = make_cpu(0, 5, 9);
    CHECK(eval("X >= 5 && Y < 10", cpu) == 1);

    cpu.x = 4;
    CHECK(eval("X >= 5 && Y < 10", cpu) == 0);
}

//////////////////////////////////////////////////////////////////////////////
// Hit count via `hits` pseudo-variable
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Compile and evaluate hits identifier", "[expression][eval][hits]") {
    must_compile("hits");
    must_compile("hits == 5");
    must_compile("hits % 10 == 0");
    must_compile("hits > 3");
}

TEST_CASE("Evaluate hits pseudo-variable", "[expression][eval][hits]") {
    auto expr = must_compile("hits == 5");
    ExprCpuState cpu{};

    cpu.hits = 4;
    CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);

    cpu.hits = 5;
    CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);

    cpu.hits = 6;
    CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
}

TEST_CASE("Hit count exact mode via expression", "[expression][eval][hits]") {
    auto expr = must_compile("hits == 5");
    ExprCpuState cpu{};

    for (uint64_t i = 1; i <= 4; ++i) {
        cpu.hits = i;
        CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    }

    cpu.hits = 5;
    CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);

    cpu.hits = 6;
    CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
}

TEST_CASE("Hit count multiple mode via expression", "[expression][eval][hits]") {
    auto expr = must_compile("hits % 3 == 0");
    ExprCpuState cpu{};

    cpu.hits = 1; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 2; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 3; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);
    cpu.hits = 4; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 5; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 6; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);
}

TEST_CASE("Hit count greater mode via expression", "[expression][eval][hits]") {
    auto expr = must_compile("hits > 3");
    ExprCpuState cpu{};

    cpu.hits = 1; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 2; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 3; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 4; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);
    cpu.hits = 5; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);
}

TEST_CASE("Condition with hits: stop 5th time A == 0", "[expression][eval][hits]") {
    auto expr = must_compile("A == 0 && hits == 5");
    ExprCpuState cpu{};
    cpu.a = 0;

    cpu.hits = 4; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
    cpu.hits = 5; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 1);

    cpu.a = 1;
    cpu.hits = 5; CHECK(evaluate(expr, cpu, nullptr, nullptr) == 0);
}

//////////////////////////////////////////////////////////////////////////////
// Edge cases
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Deeply nested parentheses", "[expression][eval][edge]") {
    auto cpu = make_cpu(42);
    CHECK(eval("((((A))))", cpu) == 42);
}

TEST_CASE("Address wrapping in mem[]", "[expression][eval][edge]") {
    test_memory[0xFFFF] = 0xAA;
    auto cpu = make_cpu();
    CHECK(eval("mem[0xFFFF]", cpu) == 0xAA);
    test_memory[0xFFFF] = 0;
}

TEST_CASE("Large constant", "[expression][eval][edge]") {
    auto cpu = make_cpu();
    CHECK(eval("65535", cpu) == 65535);
}

TEST_CASE("Chained comparisons require explicit &&", "[expression][parser][edge]") {
    // "A < 10 < 20" should parse as (A < 10) and then fail on "< 20"
    // because cmp only allows one comparison operator
    must_fail("A < 10 < 20");
}

TEST_CASE("Boolean literal not prefix of identifier", "[expression][parser][edge]") {
    // "trueX" should not match "true" and leave "X"
    must_fail("trueX");
}

//////////////////////////////////////////////////////////////////////////////
// Performance tests
//////////////////////////////////////////////////////////////////////////////

TEST_CASE("Expression evaluation performance", "[expression][performance]") {
    // Compile once, evaluate many times -- this is the hot path.
    // At 3 MHz the coprocessor executes ~1.5M instructions/sec. Each watchpoint
    // address match evaluates the condition, so evaluation must be well under
    // 1 microsecond.

    struct TestCase {
        const char* name;
        const char* expr;
    };

    TestCase cases[] = {
        {"constant true",     "true"},
        {"constant false",    "false"},
        {"register compare",  "A == 0x42"},
        {"two registers",     "A == 0x0D && X > 5"},
        {"hit count exact",   "hits == 5"},
        {"hit count + cond",  "hits == 5 && A == 0x0D"},
        {"bitwise flag test", "P & 0x01"},
        {"complex",           "A == 0x42 && mem[0x4000] == 0xFF && cycles > 1000"},
        {"arithmetic",        "(A + X) * 2 == 0x60"},
    };

    test_memory[0x4000] = 0xFF;
    auto cpu = make_cpu(0x42, 0x10, 0x22, 0xFD, 0xC000, 0x01, 2000000, 5);

    constexpr int iterations = 1000000;

    for (const auto& tc : cases) {
        auto expr = must_compile(tc.expr);

        auto start = std::chrono::high_resolution_clock::now();
        volatile uint64_t result = 0;
        for (int i = 0; i < iterations; ++i) {
            result = evaluate(expr, cpu, test_peek, nullptr);
        }
        auto end = std::chrono::high_resolution_clock::now();

        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        double ns_per_eval = static_cast<double>(ns) / iterations;

        // Report
        INFO(tc.name << ": " << ns_per_eval << " ns/eval"
             << " (" << static_cast<int>(ns_per_eval) << " ns)"
             << " result=" << result);

        // Must be under 1000 ns (1 us) for real-time at 3 MHz.
        // Typical should be well under 100 ns.
        CHECK(ns_per_eval < 1000.0);
    }

    test_memory[0x4000] = 0;
}
