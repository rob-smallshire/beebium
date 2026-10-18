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

#include <beebium/debugger/Expression.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <string>

namespace beebium {

// ============================================================================
// VM evaluator
// ============================================================================

uint64_t evaluate(const CompiledExpression& expr,
                  const ExprCpuState& cpu,
                  PeekFunction peek, void* peek_context,
                  VariableReader read_variable, void* variable_context) {
    // Small fixed-size stack (expressions are shallow)
    std::array<uint64_t, 32> stack{};
    int sp = 0;
    int const_idx = 0;

    auto push = [&](uint64_t v) { if (sp < 32) stack[static_cast<size_t>(sp++)] = v; };
    auto pop = [&]() -> uint64_t {
        if (sp <= 0) return 0;
        return stack[static_cast<size_t>(--sp)];
    };

    for (ExprOp op : expr.ops) {
        switch (op) {
        case ExprOp::PushConst:
            push(expr.constants[static_cast<size_t>(const_idx++)]);
            break;
        case ExprOp::PushA:      push(cpu.a); break;
        case ExprOp::PushX:      push(cpu.x); break;
        case ExprOp::PushY:      push(cpu.y); break;
        case ExprOp::PushSP:     push(cpu.sp); break;
        case ExprOp::PushPC:     push(cpu.pc); break;
        case ExprOp::PushP:      push(cpu.p); break;
        case ExprOp::PushC:      push((cpu.p >> 0) & 1); break;
        case ExprOp::PushZ:      push((cpu.p >> 1) & 1); break;
        case ExprOp::PushI:      push((cpu.p >> 2) & 1); break;
        case ExprOp::PushD:      push((cpu.p >> 3) & 1); break;
        case ExprOp::PushV:      push((cpu.p >> 6) & 1); break;
        case ExprOp::PushN:      push((cpu.p >> 7) & 1); break;
        case ExprOp::PushCycles: push(cpu.cycles); break;
        case ExprOp::PushHits:   push(cpu.hits); break;
        case ExprOp::MemPeek: {
            uint16_t addr = static_cast<uint16_t>(pop());
            push(peek ? peek(peek_context, addr) : 0);
            break;
        }
        case ExprOp::Add:    { auto b = pop(); auto a = pop(); push(a + b); break; }
        case ExprOp::Sub:    { auto b = pop(); auto a = pop(); push(a - b); break; }
        case ExprOp::Mul:    { auto b = pop(); auto a = pop(); push(a * b); break; }
        case ExprOp::Div:    { auto b = pop(); auto a = pop(); push(b ? a / b : 0); break; }
        case ExprOp::Mod:    { auto b = pop(); auto a = pop(); push(b ? a % b : 0); break; }
        case ExprOp::BitAnd: { auto b = pop(); auto a = pop(); push(a & b); break; }
        case ExprOp::BitOr:  { auto b = pop(); auto a = pop(); push(a | b); break; }
        case ExprOp::BitXor: { auto b = pop(); auto a = pop(); push(a ^ b); break; }
        case ExprOp::Eq:     { auto b = pop(); auto a = pop(); push(a == b ? 1ULL : 0ULL); break; }
        case ExprOp::Ne:     { auto b = pop(); auto a = pop(); push(a != b ? 1ULL : 0ULL); break; }
        case ExprOp::Lt:     { auto b = pop(); auto a = pop(); push(a < b ? 1ULL : 0ULL); break; }
        case ExprOp::Gt:     { auto b = pop(); auto a = pop(); push(a > b ? 1ULL : 0ULL); break; }
        case ExprOp::Le:     { auto b = pop(); auto a = pop(); push(a <= b ? 1ULL : 0ULL); break; }
        case ExprOp::Ge:     { auto b = pop(); auto a = pop(); push(a >= b ? 1ULL : 0ULL); break; }
        case ExprOp::LogAnd: { auto b = pop(); auto a = pop(); push((a && b) ? 1ULL : 0ULL); break; }
        case ExprOp::LogOr:  { auto b = pop(); auto a = pop(); push((a || b) ? 1ULL : 0ULL); break; }
        case ExprOp::LogNot: { auto a = pop(); push(a ? 0u : 1u); break; }
        case ExprOp::PushVar: {
            const auto slot = static_cast<uint32_t>(expr.constants[static_cast<size_t>(const_idx++)]);
            push(read_variable ? read_variable(variable_context, slot) : 0);
            break;
        }
        }
    }

    return sp > 0 ? stack[0] : 0;
}

// ============================================================================
// Parser + compiler (recursive descent)
// ============================================================================

namespace {

struct BuiltIn {
    std::string_view name;
    ExprOp op;
    std::string_view description;
};

constexpr BuiltIn BUILT_INS[] = {
    {"A", ExprOp::PushA, "The accumulator."},
    {"X", ExprOp::PushX, "The X index register."},
    {"Y", ExprOp::PushY, "The Y index register."},
    {"SP", ExprOp::PushSP, "The stack pointer."},
    {"PC", ExprOp::PushPC, "The program counter."},
    {"P", ExprOp::PushP, "The processor status register."},
    {"C", ExprOp::PushC, "The carry flag, 0 or 1."},
    {"Z", ExprOp::PushZ, "The zero flag, 0 or 1."},
    {"I", ExprOp::PushI, "The interrupt-disable flag, 0 or 1."},
    {"D", ExprOp::PushD, "The decimal flag, 0 or 1."},
    {"V", ExprOp::PushV, "The overflow flag, 0 or 1."},
    {"N", ExprOp::PushN, "The negative flag, 0 or 1."},
    {"cycles", ExprOp::PushCycles, "The CPU cycle count."},
    {"hits", ExprOp::PushHits, "How many times this breakpoint or watchpoint has matched."},
};

struct Parser {
    std::string_view src;
    const ExprScope* scope = nullptr;
    size_t pos = 0;
    CompiledExpression result;
    std::string error;

    bool has_error() const { return !error.empty(); }

    void skip_whitespace() {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])))
            ++pos;
    }

    char peek_char() const {
        return pos < src.size() ? src[pos] : '\0';
    }

    bool match(char c) {
        skip_whitespace();
        if (pos < src.size() && src[pos] == c) { ++pos; return true; }
        return false;
    }

    bool match(std::string_view s) {
        skip_whitespace();
        if (pos + s.size() <= src.size() && src.substr(pos, s.size()) == s) {
            // Don't match a prefix of a longer identifier (e.g. "mem" in "memory")
            if (s.size() > 0 && std::isalpha(static_cast<unsigned char>(s.back()))) {
                size_t next = pos + s.size();
                if (next < src.size() && std::isalnum(static_cast<unsigned char>(src[next])))
                    return false;
            }
            pos += s.size();
            return true;
        }
        return false;
    }

    void emit(ExprOp op) { result.ops.push_back(op); }

    void emit_const(uint32_t value) {
        result.ops.push_back(ExprOp::PushConst);
        result.constants.push_back(value);
    }

    void set_error(const std::string& msg) {
        if (error.empty()) {
            error = msg + " at position " + std::to_string(pos);
        }
    }

    // Grammar: expr := or_expr
    void parse_expr() { parse_or(); }

    // or := and ('||' and)*
    void parse_or() {
        parse_and();
        while (!has_error() && match("||")) {
            parse_and();
            emit(ExprOp::LogOr);
        }
    }

    // and := cmp ('&&' cmp)*
    void parse_and() {
        parse_cmp();
        while (!has_error() && match("&&")) {
            parse_cmp();
            emit(ExprOp::LogAnd);
        }
    }

    // cmp := bitop (('==' | '!=' | '<=' | '>=' | '<' | '>') bitop)?
    void parse_cmp() {
        parse_bitop();
        if (has_error()) return;
        skip_whitespace();
        if (match("=="))      { parse_bitop(); emit(ExprOp::Eq); }
        else if (match("!=")) { parse_bitop(); emit(ExprOp::Ne); }
        else if (match("<=")) { parse_bitop(); emit(ExprOp::Le); }
        else if (match(">=")) { parse_bitop(); emit(ExprOp::Ge); }
        else if (match("<"))  { parse_bitop(); emit(ExprOp::Lt); }
        else if (match(">"))  { parse_bitop(); emit(ExprOp::Gt); }
    }

    // bitop := sum (('&' | '|' | '^') sum)*
    void parse_bitop() {
        parse_sum();
        while (!has_error()) {
            skip_whitespace();
            // Avoid consuming '&&' or '||' as bitwise operators
            if (pos + 1 < src.size() && src[pos] == '&' && src[pos + 1] == '&') break;
            if (pos + 1 < src.size() && src[pos] == '|' && src[pos + 1] == '|') break;

            if (match('&'))      { parse_sum(); emit(ExprOp::BitAnd); }
            else if (match('|')) { parse_sum(); emit(ExprOp::BitOr); }
            else if (match('^')) { parse_sum(); emit(ExprOp::BitXor); }
            else break;
        }
    }

    // sum := term (('+' | '-') term)*
    void parse_sum() {
        parse_term();
        while (!has_error()) {
            skip_whitespace();
            if (match('+'))      { parse_term(); emit(ExprOp::Add); }
            else if (match('-')) { parse_term(); emit(ExprOp::Sub); }
            else break;
        }
    }

    // term := factor (('*' | '/' | '%') factor)*
    void parse_term() {
        parse_factor();
        while (!has_error()) {
            skip_whitespace();
            if (match('*'))      { parse_factor(); emit(ExprOp::Mul); }
            else if (match('/')) { parse_factor(); emit(ExprOp::Div); }
            else if (match('%')) { parse_factor(); emit(ExprOp::Mod); }
            else break;
        }
    }

    // factor := number | 'true' | 'false' | register | 'mem[' expr ']'
    //         | '(' expr ')' | '!' factor
    void parse_factor() {
        if (has_error()) return;
        skip_whitespace();

        if (pos >= src.size()) {
            set_error("unexpected end of expression");
            return;
        }

        // Parenthesised expression
        if (match('(')) {
            parse_expr();
            if (!match(')')) set_error("expected ')'");
            return;
        }

        // Logical not
        if (match('!')) {
            parse_factor();
            emit(ExprOp::LogNot);
            return;
        }

        // A name, read whole: a built-in (the CPU's registers and flags, which
        // are upper case, then cycles, hits, true, false and mem[...]), or
        // else a variable the scope names.
        if (std::isalpha(static_cast<unsigned char>(src[pos])) || src[pos] == '_') {
            parse_name();
            return;
        }

        // Number literals: 0x hex, 0b binary, decimal
        if (std::isdigit(static_cast<unsigned char>(src[pos]))) {
            parse_number();
            return;
        }

        set_error(std::string("unexpected character '") + src[pos] + "'");
    }

    void parse_name() {
        const size_t start = pos;
        while (pos < src.size() &&
               (std::isalnum(static_cast<unsigned char>(src[pos])) || src[pos] == '_')) {
            ++pos;
        }
        const std::string_view name = src.substr(start, pos - start);

        for (const auto& built_in : BUILT_INS) {
            if (name == built_in.name) { emit(built_in.op); return; }
        }
        if (name == "true")  { emit_const(1); return; }
        if (name == "false") { emit_const(0); return; }
        if (name == "mem") {
            if (!match('[')) { set_error("expected '[' after 'mem'"); return; }
            parse_expr();
            if (!match(']')) { set_error("expected ']'"); return; }
            emit(ExprOp::MemPeek);
            return;
        }
        if (scope) {
            for (const auto& [variable, slot] : scope->variables) {
                if (name == variable) {
                    emit(ExprOp::PushVar);
                    result.constants.push_back(slot);
                    return;
                }
            }
        }
        pos = start;
        if (scope && scope->unknown_name) {
            if (error.empty()) error = scope->unknown_name(name);
        } else {
            set_error("unknown name '" + std::string(name) + "'");
        }
    }

    void parse_number() {
        size_t start = pos;
        uint64_t value = 0;

        if (pos + 1 < src.size() && src[pos] == '0' && (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
            // Hex: 0xFF
            pos += 2;
            if (pos >= src.size() || !std::isxdigit(static_cast<unsigned char>(src[pos]))) {
                set_error("expected hex digit after '0x'");
                return;
            }
            auto [ptr, ec] = std::from_chars(src.data() + pos, src.data() + src.size(), value, 16);
            if (ec != std::errc{}) { set_error("invalid hex number"); return; }
            pos = static_cast<size_t>(ptr - src.data());
        } else if (pos + 1 < src.size() && src[pos] == '0' && (src[pos + 1] == 'b' || src[pos + 1] == 'B')) {
            // Binary: 0b1010
            pos += 2;
            if (pos >= src.size() || (src[pos] != '0' && src[pos] != '1')) {
                set_error("expected binary digit after '0b'");
                return;
            }
            auto [ptr, ec] = std::from_chars(src.data() + pos, src.data() + src.size(), value, 2);
            if (ec != std::errc{}) { set_error("invalid binary number"); return; }
            pos = static_cast<size_t>(ptr - src.data());
        } else {
            // Decimal
            auto [ptr, ec] = std::from_chars(src.data() + pos, src.data() + src.size(), value, 10);
            if (ec != std::errc{}) { set_error("invalid number"); return; }
            pos = static_cast<size_t>(ptr - src.data());
        }

        (void)start;
        emit_const(static_cast<uint32_t>(value));
    }
};

} // anonymous namespace

const std::vector<ExprBuiltIn>& expression_built_ins() {
    static const std::vector<ExprBuiltIn> built_ins = [] {
        std::vector<ExprBuiltIn> names;
        for (const auto& built_in : BUILT_INS) {
            names.push_back({built_in.name, built_in.description});
        }
        return names;
    }();
    return built_ins;
}

std::variant<CompiledExpression, std::string> compile(std::string_view source,
                                                      const ExprScope& scope) {
    if (source.empty()) {
        return std::string("empty expression");
    }

    Parser parser;
    parser.src = source;
    parser.scope = &scope;
    parser.result.source = std::string(source);
    parser.parse_expr();

    if (parser.has_error()) {
        return parser.error;
    }

    // Check for trailing garbage
    parser.skip_whitespace();
    if (parser.pos < source.size()) {
        return std::string("unexpected character '") + source[parser.pos]
               + "' at position " + std::to_string(parser.pos);
    }

    return std::move(parser.result);
}


} // namespace beebium
