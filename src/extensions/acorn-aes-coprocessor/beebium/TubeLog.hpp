// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// TubeLog.hpp, the counting wrapper, a test build only
// version 2.0.0, 20 November 2026.

// TubeLog, the counting wrapper: in a test build only (compiled in by AES_TUBE_LOG), it stands
// between the core and the board, and between the board and the Tube. It records in order every
// access the ARM makes in the Tube's region: the address the ARM issued and the register it
// reached, so that a fault in the address mapping cannot hide itself. It changes nothing it
// passes on.
// Each row: tick, R or W, address, register (0-7, or - for none), value, how many in a row.
// Written to a file row by row, so it cannot overrun as Beebium's ring does. Last row: end, count.
//
// Where it sits (see AesTube.cpp's constructor): in the logged build the core's bus is this
// log, which passes each cycle on to AesMemory, and AesMemory's Tube port is this log too, which
// passes each register access on to AesTube and so to Beebium's Tube chip: core, TubeLog,
// AesMemory, TubeLog, AesTube, Tube chip. In the normal build the core talks to AesMemory, and
// AesMemory to AesTube, directly.

#pragma once

#include "../AesMemory.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

// The wrapper. It implements ArmBus (the core's interface to memory) for the core, and TubePort
// (AesMemory's link to the Tube chip) for the memory. AesTube makes and wires it.
class TubeLog : public ArmBus, public TubePort {
public:
    // The log file's path, from the AES_TUBE_LOG environment variable; empty means no file.
    static std::string path_from_environment() {
        const char* path = std::getenv("AES_TUBE_LOG");
        return path ? path : "";
    }

    // A log writing to `path`, if given, stamping rows with `timing`'s ticks. connect() wires it.
    TubeLog(const std::string& path, AesTiming& timing)
        : timing_(timing), file_(path.empty() ? nullptr : std::fopen(path.c_str(), "w")) {}
    // Closes the file, so that a run ends with its total.
    ~TubeLog() override { close(); }

    // Wires the log to what it passes on to: the Tube (AesTube) and the memory (AesMemory).
    void connect(TubePort& tube, ArmBus& memory) {
        tube_ = &tube;
        memory_ = &memory;
    }

    // Writes the last row and the total, and closes the file.
    void close() {
        if (!file_) return;
        write_row();
        std::fprintf(file_, "end %llu\n", static_cast<unsigned long long>(total_));
        std::fclose(file_);
        file_ = nullptr;
    }

    // ArmBus: each access is passed on to the memory, and noted if it is in the Tube's region.
    uint32_t read_word(uint32_t addr, bool seq) override {
        begin(addr);
        const uint32_t value = memory_->read_word(addr, seq);
        finish(addr, 'R', value);
        return value;
    }
    uint32_t read_byte(uint32_t addr, bool seq) override {
        begin(addr);
        const uint32_t value = memory_->read_byte(addr, seq);
        finish(addr, 'R', value);
        return value;
    }
    void write_word(uint32_t addr, uint32_t value, bool seq) override {
        begin(addr);
        memory_->write_word(addr, value, seq);
        finish(addr, 'W', value);
    }
    void write_byte(uint32_t addr, uint8_t value, bool seq) override {
        begin(addr);
        memory_->write_byte(addr, value, seq);
        finish(addr, 'W', value);
    }
    // An internal cycle is passed on and not noted.
    void internal(uint32_t addr) override {
        memory_->internal(addr);
        take_abort();
    }

    // TubePort: a register access reached the chip; it is passed on and noted with the address
    // the ARM issued.
    uint8_t read(int reg) override {
        const uint8_t value = tube_->read(reg);
        note('R', address_, reg, value);
        reached_ = true;
        return value;
    }
    void write(int reg, uint8_t value) override {
        tube_->write(reg, value);
        note('W', address_, reg, value);
        reached_ = true;
    }
    // The debugger's look is not an access by the ARM, so it is passed on and not counted.
    uint8_t peek(int reg) const override { return tube_->peek(reg); }

private:
    // One row of the log: identical accesses in a row are counted in one row.
    struct Row {
        char direction = 0;
        uint32_t address = 0;
        int reg = -1;
        uint8_t value = 0;
        uint64_t tick = 0, count = 0;
    };

    // Before an access: passes on the mode the core set on this bus, which abort strict needs, and
    // remembers the address, for a register access it may reach.
    void begin(uint32_t addr) {
        memory_->user_mode = user_mode;
        address_ = addr;
        reached_ = false;
    }
    // After an access: one in the Tube's region that reached no register is noted with register
    // "-", so that an access the mapping lost still shows.
    void finish(uint32_t addr, char direction, uint32_t value) {
        if (in_tube(addr) && !reached_) note(direction, addr, -1, uint8_t(value));
        take_abort();
    }
    // Passes the memory's abort line on to this object, because the core reads and clears the
    // line on the bus it holds, which in the logged build is the log.
    void take_abort() {
        abort_line = memory_->abort_line;
        memory_->abort_line = false;
    }
    // Counts one access, adding it to the current row if it repeats it, else starting a new row.
    void note(char direction, uint32_t addr, int reg, uint8_t value) {
        total_++;
        if (row_.count && row_.direction == direction && row_.address == addr && row_.reg == reg &&
            row_.value == value) {
            row_.count++;
            return;
        }
        write_row();
        row_ = Row{direction, addr, reg, value, timing_.ticks(), 1};
    }
    // Writes the current row to the file, flushed at once.
    void write_row() {
        if (!file_ || !row_.count) return;
        char reg[4] = "-";
        if (row_.reg >= 0) std::snprintf(reg, sizeof reg, "%d", row_.reg);
        std::fprintf(file_, "%llu %c %X %s %02X %llu\n", static_cast<unsigned long long>(row_.tick),
                     row_.direction, row_.address, reg, row_.value,
                     static_cast<unsigned long long>(row_.count));
        std::fflush(file_);  // A server stopped with a signal loses nothing already closed.
        row_ = Row{};
    }

    AesTiming& timing_;
    std::FILE* file_;
    TubePort* tube_ = nullptr;
    ArmBus* memory_ = nullptr;
    uint32_t address_ = 0;  // The address of the access under way.
    bool reached_ = false;  // Whether that access reached a register.
    Row row_;
    uint64_t total_ = 0;
};
