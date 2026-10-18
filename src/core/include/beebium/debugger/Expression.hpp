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

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace beebium {

// Bytecode opcodes for the expression VM.
enum class ExprOp : uint8_t {
    // Push a 32-bit constant onto the stack
    PushConst,

    // Push a CPU register value
    PushA, PushX, PushY, PushSP, PushPC, PushP,

    // Push individual status flag bits (0 or 1)
    PushC, PushZ, PushI, PushD, PushV, PushN,

    // Push the cycle counter
    PushCycles,

    // Push the hit counter (number of times this breakpoint/watchpoint has matched)
    PushHits,

    // Memory dereference: pop address, push peek(address)
    MemPeek,

    // Arithmetic (pop two, push result)
    Add, Sub, Mul, Div, Mod,

    // Bitwise (pop two, push result)
    BitAnd, BitOr, BitXor,

    // Comparison (pop two, push 0 or 1)
    Eq, Ne, Lt, Gt, Le, Ge,

    // Logical (pop two, push 0 or 1)
    LogAnd, LogOr,

    // Unary (pop one, push result)
    LogNot,

    // Push a variable the compiling scope named: the next constant is its
    // slot, which the evaluator's VariableReader resolves.
    PushVar,
};

// Compiled expression bytecode.
struct CompiledExpression {
    std::vector<ExprOp> ops;
    std::vector<uint64_t> constants;  // indexed by position in ops stream

    // Source text (for display/debugging)
    std::string source;

    bool empty() const { return ops.empty(); }
};

// CPU state snapshot for expression evaluation (avoids coupling to M6502).
struct ExprCpuState {
    uint8_t a, x, y, sp, p;
    uint16_t pc;
    uint64_t cycles;
    uint64_t hits;  // hit counter for the current breakpoint/watchpoint
};

// Function type for side-effect-free memory read.
using PeekFunction = uint8_t(*)(void* context, uint16_t addr);

// Function type reading the variable in a slot a scope assigned.
using VariableReader = uint64_t(*)(void* context, uint32_t slot);

// The names a condition may use beyond the built-ins (the CPU registers and
// flags, cycles, hits, mem[], true and false), each with the slot the
// evaluator reads it from, and what to say about a name that is not one of
// them.
struct ExprScope {
    std::vector<std::pair<std::string, uint32_t>> variables;
    std::function<std::string(std::string_view name)> unknown_name;
};

// Evaluate a compiled expression against CPU state, reading any variables
// through `read_variable`.
// Returns the top-of-stack value (truthy if non-zero).
uint64_t evaluate(const CompiledExpression& expr,
                  const ExprCpuState& cpu,
                  PeekFunction peek, void* peek_context,
                  VariableReader read_variable = nullptr, void* variable_context = nullptr);

// A name every condition can use, whatever the machine: the CPU's registers
// and flags, cycles and hits.
struct ExprBuiltIn {
    std::string_view name;
    std::string_view description;  // one line
};

// The built-in names, in display order (mem[], true and false aside).
const std::vector<ExprBuiltIn>& expression_built_ins();

// Parse and compile an expression string, resolving names beyond the built-ins
// against `scope`. Returns the compiled expression on success, or a string
// error message.
std::variant<CompiledExpression, std::string> compile(std::string_view source,
                                                      const ExprScope& scope = {});


} // namespace beebium
