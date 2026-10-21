// Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
//
// This file is part of Beebium.
//
// This file is derived from the Intel 8271 model in beebjit
// (https://github.com/scarybeasts/beebjit), file intel_fdc.c, Copyright (C) Chris Evans
// <scarybeasts@gmail.com>, which is licensed under version 3 of the GNU General Public License
// (GPL-3.0 only, per beebjit's COPYING). This file is therefore licensed under version 3 of the
// GNU General Public License only, not "or any later version", unlike the rest of Beebium.
//
// The translation to C++ and the Beebium integration are Copyright (C) 2026 Robert Smallshire
// <robert@smallshire.org.uk>.
//
// This file is free software: you can redistribute it and/or modify it under the terms of
// version 3 of the GNU General Public License as published by the Free Software Foundation. It
// is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
// Public License for more details. You should have received a copy of the GNU General Public
// License along with Beebium. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "DiscDrive.hpp"
#include "DiscWriteBack.hpp"
#include "IbmDiscFormat.hpp"
#include "beebium/ResetCause.hpp"

#include <array>
#include <initializer_list>
#include <cstdint>
#include <string_view>

namespace beebium {

// Intel 8271 floppy disc controller, as fitted to the BBC Model B.
//
// The 8271 is a microcontroller running a program ROM, with a bit processor
// that handles the FM data stream. Its behaviour is modelled in terms of the
// internal register file R0-R31 that the program uses, because software can
// read and write every one of those registers through the special register
// commands, and some copy protection does.
//
// The model is derived from beebjit's 8271 model by Chris Evans, which he
// built from his reading of the chip's program ROM and checked against a real
// Model B; see also his write-up "Reverse engineering a forgotten 1970s Intel
// dual core beast: 8271" (2020). docs/discussion/intel-8271-study.md compares
// the emulations and records what the real chip does where the evidence
// settles it. Where the study records an assumption, a test named for it pins
// the choice.
//
// Timing: tick() is called at 1MHz. The disc side works one FM byte (64us,
// one pulse word) at a time while the selected drive turns; seek, settle
// and head-load waits are millisecond timers. Host register accesses take
// effect at once (the real chip takes 27-211us to act on some of them).
//
// Drive interface: the drive output port drives the shared cable. SELECT0
// and SELECT1 select the drives, LOAD HEAD turns both drives' motors (MOTOR
// ON is common on the cable), SIDE selects the surface on both.
class Intel8271 {
public:
    // Status register bits
    static constexpr uint8_t STATUS_BUSY = 0x80;
    static constexpr uint8_t STATUS_COMMAND_FULL = 0x40;
    static constexpr uint8_t STATUS_PARAMETER_FULL = 0x20;
    static constexpr uint8_t STATUS_RESULT_READY = 0x10;
    static constexpr uint8_t STATUS_INT = 0x08;
    static constexpr uint8_t STATUS_DATA_REQUEST = 0x04;

    // Drive control output port bits (special register &23)
    static constexpr uint8_t OUT_SELECT_1 = 0x80;
    static constexpr uint8_t OUT_SELECT_0 = 0x40;
    static constexpr uint8_t OUT_SIDE = 0x20;
    static constexpr uint8_t OUT_LOW_HEAD_CURRENT = 0x10;
    static constexpr uint8_t OUT_LOAD_HEAD = 0x08;
    static constexpr uint8_t OUT_DIRECTION = 0x04;
    static constexpr uint8_t OUT_STEP = 0x02;
    static constexpr uint8_t OUT_WRITE_ENABLE = 0x01;

    // Result codes
    static constexpr uint8_t RESULT_OK = 0x00;
    static constexpr uint8_t RESULT_CLOCK_ERROR = 0x08;
    static constexpr uint8_t RESULT_LATE_DMA = 0x0A;
    static constexpr uint8_t RESULT_ID_CRC_ERROR = 0x0C;
    static constexpr uint8_t RESULT_DATA_CRC_ERROR = 0x0E;
    static constexpr uint8_t RESULT_NOT_READY = 0x10;
    static constexpr uint8_t RESULT_WRITE_PROTECTED = 0x12;
    static constexpr uint8_t RESULT_SECTOR_NOT_FOUND = 0x18;
    static constexpr uint8_t RESULT_DELETED_DATA = 0x20;

    Intel8271() = default;
    Intel8271(const Intel8271&) = delete;
    Intel8271& operator=(const Intel8271&) = delete;

    // =========================================================================
    // Host interface
    // =========================================================================

    // The chip-select registers, A1:A0 = 0-3. Reads: status, result, and the
    // sector byte counter MSB and LSB (undocumented, observed on a Model B).
    // Writes: command, parameter, reset, nothing.
    uint8_t read_register(uint8_t reg) {
        switch (reg & 3) {
            case 1: {
                uint8_t result = regs_[R_RESULT];
                result_ready_ = false;
                regs_[R_STATUS] &= ~STATUS_INT;
                return result;
            }
            default:
                return peek_register(reg);
        }
    }

    uint8_t peek_register(uint8_t reg) const {
        switch (reg & 3) {
            case 0: return status();
            case 1: return regs_[R_RESULT];
            case 2: return regs_[R_COUNT_MSB];
            default: return regs_[R_COUNT_LSB];
        }
    }

    void write_register(uint8_t reg, uint8_t value) {
        switch (reg & 3) {
            case 0: command_written(value); break;
            case 1: parameter_written(value); break;
            case 2:
                // Reset is the value 1; the chip is held in reset while the
                // register stays at 1, which is not modelled.
                if (value == 1) break_reset();
                break;
            default: break;
        }
    }

    // The data register, reached by DACK.
    uint8_t read_data() {
        regs_[R_STATUS] &= ~(STATUS_DATA_REQUEST | STATUS_INT);
        return regs_[R_DATA];
    }

    uint8_t peek_data() const { return regs_[R_DATA]; }

    void write_data(uint8_t value) {
        regs_[R_STATUS] &= ~(STATUS_DATA_REQUEST | STATUS_INT);
        regs_[R_DATA] = value;
    }

    // The INT output. Held low for one tick after an abort, so that a
    // completion following an unserviced data request is a fresh edge.
    bool interrupt() const {
        return (regs_[R_STATUS] & STATUS_INT) && !interrupt_held_low_;
    }

    // =========================================================================
    // Clock
    // =========================================================================

    void tick() {
        interrupt_held_low_ = false;
        write_back_.tick(drives_);

        if (timer_ticks_ > 0 && --timer_ticks_ == 0) {
            timer_fired();
        }

        bool turning = false;
        for (DiscDrive* drive : drives_) {
            if (drive && drive->motor_on()) turning = true;
        }
        if (!turning) return;
        if (++byte_phase_ < TICKS_PER_BYTE) return;
        byte_phase_ = 0;

        byte_slot();
        for (DiscDrive* drive : drives_) {
            if (drive && drive->motor_on()) drive->advance_head();
        }
    }

    // =========================================================================
    // Reset
    // =========================================================================

    void reset(ResetCause cause) {
        break_reset();
        if (cause == ResetCause::PowerOn) {
            regs_.fill(0);
            result_ready_ = false;
            out_data_ = 0;
            out_clocks_ = 0;
            index_was_asserted_ = false;
            byte_phase_ = 0;
        }
    }

    // =========================================================================
    // Drives
    // =========================================================================

    void attach_drive(int drive_num, DiscDrive* drive) {
        if (drive_num >= 0 && drive_num < 2) {
            drives_[static_cast<size_t>(drive_num)] = drive;
        }
    }

    DiscDrive* attached_drive(int drive_num) const {
        if (drive_num < 0 || drive_num > 1) return nullptr;
        return drives_[static_cast<size_t>(drive_num)];
    }

    // =========================================================================
    // Inspection
    // =========================================================================

    uint8_t status() const {
        // The internal status register also holds the mode bits (1:0), and
        // the bits the program uses for its own purposes; only BUSY, INT and
        // the data request reach the outside, with RESULT READY kept apart.
        uint8_t s = regs_[R_STATUS] & (STATUS_BUSY | STATUS_INT | STATUS_DATA_REQUEST);
        if (result_ready_) s |= STATUS_RESULT_READY;
        return s;
    }

    uint8_t internal_register(uint8_t index) const { return regs_[index & 31]; }
    uint8_t drive_output() const { return drive_out_; }
    std::string_view name() const { return "8271"; }

private:
    // =========================================================================
    // Register file. Several registers serve more than one purpose in the
    // chip's program; the names give the main one.
    // =========================================================================

    static constexpr uint8_t R_POINTER = 0x00;         // parameter pointer; size MSB copy
    static constexpr uint8_t R_PARAM_COUNT = 0x01;     // parameters awaited; seek retries
    static constexpr uint8_t R_DATA_MARK = 0x02;       // mark for write commands
    static constexpr uint8_t R_PARAM_5 = 0x03;
    static constexpr uint8_t R_PARAM_4 = 0x04;         // sector increment
    static constexpr uint8_t R_PARAM_3 = 0x05;         // size code and count
    static constexpr uint8_t R_PARAM_2 = 0x06;         // sector
    static constexpr uint8_t R_PARAM_1 = 0x07;         // track
    static constexpr uint8_t R_HEADER_POINTER = 0x08;  // where the next ID byte goes
    static constexpr uint8_t R_SEEK_COUNT = 0x0A;      // steps to go; ID sector
    static constexpr uint8_t R_SEEK_TARGET_COPY = 0x0B; // second copy of the target; ID head
    static constexpr uint8_t R_SEEK_TARGET = 0x0C;     // physical target; ID track
    static constexpr uint8_t R_ID_SECTOR = 0x0A;
    static constexpr uint8_t R_ID_TRACK = 0x0C;
    static constexpr uint8_t R_STEP_RATE = 0x0D;
    static constexpr uint8_t R_SETTLE_TIME = 0x0E;
    static constexpr uint8_t R_HEAD_LOAD = 0x0F;       // load time (3:0), unload count (7:4)
    static constexpr uint8_t R_DRIVE_0_TRACKS = 0x10;  // bad track 1, bad track 2, current track
    static constexpr uint8_t R_COUNT_LSB = 0x13;
    static constexpr uint8_t R_COUNT_MSB = 0x14;
    static constexpr uint8_t R_DRIVE_INPUT = 0x15;
    static constexpr uint8_t R_RESULT = 0x16;
    static constexpr uint8_t R_STATUS = 0x17;          // also the mode register
    static constexpr uint8_t R_DRIVE_1_TRACKS = 0x18;
    static constexpr uint8_t R_LATCHED_INPUT = 0x1B;
    static constexpr uint8_t R_INDEX_COUNT = 0x1C;
    static constexpr uint8_t R_DATA = 0x1D;
    static constexpr uint8_t R_PARAMETER = 0x1E;
    static constexpr uint8_t R_COMMAND = 0x1F;

    static constexpr uint8_t MODE_NON_DMA = 0x01;
    static constexpr uint8_t MODE_SINGLE_ACTUATOR = 0x02;
    // Internal status bits the program uses while data bytes are being
    // handed to the host.
    static constexpr uint8_t STATUS_TRANSFERRING = 0x30;

    // Drive control input bits (special register &22)
    static constexpr uint8_t IN_ALWAYS = 0x81;
    static constexpr uint8_t IN_TRACK_0 = 0x02;
    static constexpr uint8_t IN_READY_0 = 0x04;
    static constexpr uint8_t IN_WRITE_PROTECT = 0x08;
    static constexpr uint8_t IN_INDEX = 0x10;
    static constexpr uint8_t IN_READY_1 = 0x40;

    // Command groups, (command & 0x3C) >> 2
    enum Group : uint8_t {
        SCAN = 0, SCAN_DELETED = 1, WRITE = 2, WRITE_DELETED = 3,
        READ = 4, READ_DELETED = 5, READ_ID = 6, VERIFY = 7,
        FORMAT = 8, UNUSED_9 = 9, SEEK = 10, READ_DRIVE_STATUS = 11,
        UNUSED_12 = 12, SPECIFY = 13, WRITE_SPECIAL = 14, READ_SPECIAL = 15,
    };

    static constexpr int TICKS_PER_BYTE = 64;
    static constexpr int TICKS_PER_MS = 1000;
    static constexpr uint8_t GAP2_SKIP_BYTES = 11;
    static constexpr int ID_RECOVERY_CELLS = 4 * 16;
    static constexpr int SYNC_CELLS = 32;

    enum class ParameterMode { None, Command, Specify };
    enum class Purpose { Seek, Read, Write, ReadId, Format };
    enum class Timer { None, SeekStep, PostSeek };
    enum class IndexAction { None, Timeout, Unload, StartFormat, EndFormat, StartReadId };

    enum class Reader {
        Idle, RecoverAfterId, SyncForId, IdMark, IdField, IdCrc,
        SkipGap2, SyncForData, DataMark, DataField, DataCrc,
    };

    // =========================================================================
    // Status and results
    // =========================================================================

    uint8_t group() const { return static_cast<uint8_t>((regs_[R_COMMAND] & 0x3C) >> 2); }

    void set_result(uint8_t result) {
        regs_[R_RESULT] = result;
        result_ready_ = true;
    }

    // =========================================================================
    // Host side: command and parameters
    // =========================================================================

    void command_written(uint8_t command) {
        // A command written while busy is accepted: it does not cancel the
        // disc activity already under way.
        regs_[R_COMMAND] = command;
        regs_[R_STATUS] = static_cast<uint8_t>((regs_[R_STATUS] | STATUS_BUSY) & ~STATUS_COMMAND_FULL);
        set_result(0);
        // Defaults that make the single-record (128-byte) commands work
        regs_[R_PARAM_3] = 1;
        regs_[R_PARAM_4] = 1;

        uint8_t count = (command & 0x18) ? (command & 0x03) : 5;
        regs_[R_PARAM_COUNT] = count;
        if (count == 0) {
            start_command();
            return;
        }
        regs_[R_POINTER] = R_PARAM_1;
        parameter_mode_ = ParameterMode::Command;
    }

    void parameter_written(uint8_t value) {
        regs_[R_PARAMETER] = value;
        result_ready_ = false;
        switch (parameter_mode_) {
            case ParameterMode::None:
                break;
            case ParameterMode::Command:
                write_special(regs_[R_POINTER], value);
                --regs_[R_POINTER];
                if (--regs_[R_PARAM_COUNT] == 0) start_command();
                break;
            case ParameterMode::Specify:
                write_special(regs_[R_POINTER], value);
                ++regs_[R_POINTER];
                if (--regs_[R_PARAM_COUNT] == 0) finish_simple_command();
                break;
        }
    }

    void start_command() {
        read_drive_status();
        parameter_mode_ = ParameterMode::None;

        uint8_t select = regs_[R_COMMAND] & (OUT_SELECT_1 | OUT_SELECT_0);
        out_clocks_ = select;   // the chip uses this register as scratch here
        if (select != (drive_out_ & (OUT_SELECT_1 | OUT_SELECT_0))) {
            // A new selection drops every output but the side
            set_drive_out(static_cast<uint8_t>(select | (drive_out_ & OUT_SIDE)));
        }
        regs_[R_COMMAND] &= 0x3C;
        dispatch();
    }

    void dispatch() {
        switch (group()) {
            case READ_DRIVE_STATUS:
                set_result(read_drive_status());
                regs_[R_LATCHED_INPUT] = regs_[R_DRIVE_INPUT];
                finish_simple_command();
                break;
            case SPECIFY:
                // The first parameter names the first register; three values follow
                regs_[R_POINTER] = regs_[R_PARAM_1];
                regs_[R_PARAM_COUNT] = 3;
                parameter_mode_ = ParameterMode::Specify;
                break;
            case WRITE_SPECIAL:
                write_special(regs_[R_PARAM_1], regs_[R_PARAM_2]);
                regs_[R_STATUS] &= ~STATUS_BUSY;
                break;
            case READ_SPECIAL:
                set_result(read_special(regs_[R_PARAM_1]));
                finish_simple_command();
                break;
            case READ_ID:
                // A non-zero second parameter skips the seek and the wait for
                // the index; later IDs of the same command come this way.
                if (regs_[R_PARAM_2] == 0) {
                    seek(Purpose::ReadId);
                } else {
                    start_syncing_for_id();
                }
                break;
            case SEEK:
                seek(Purpose::Seek);
                break;
            case READ: case READ_DELETED: case VERIFY: case SCAN: case SCAN_DELETED:
                seek(Purpose::Read);
                break;
            case WRITE:
                regs_[R_DATA_MARK] = ibm_disc_format::k_data_mark_data_pattern;
                seek(Purpose::Write);
                break;
            case WRITE_DELETED:
                regs_[R_DATA_MARK] = ibm_disc_format::k_deleted_data_mark_data_pattern;
                seek(Purpose::Write);
                break;
            case FORMAT:
                seek(Purpose::Format);
                break;
            default:
                // Groups 9 and 12 have no command; what the chip does with
                // them is unknown. They complete without effect.
                finish_simple_command();
                break;
        }
    }

    // =========================================================================
    // Special registers: R0-R31, and the bit processor's registers at &20-&27
    // =========================================================================

    uint8_t read_special(uint8_t reg) const {
        reg &= 0x3F;
        if (reg < 32) return regs_[reg];
        switch (reg & 7) {
            case 2: return drive_input();
            case 3: return drive_out_;
            case 4: return out_clocks_;
            case 5: return out_data_;
            default: return 0;
        }
    }

    void write_special(uint8_t reg, uint8_t value) {
        reg &= 0x3F;
        if (reg < 32) {
            regs_[reg] = value;
            return;
        }
        switch (reg & 7) {
            case 3: set_drive_out(value); break;
            case 4: out_clocks_ = value; break;
            case 5: out_data_ = value; break;
            default: break;
        }
    }

    // =========================================================================
    // Completion
    // =========================================================================

    void finish_command(uint8_t result) {
        if (result != RESULT_OK) {
            set_drive_out(drive_out_ & ~(OUT_DIRECTION | OUT_STEP | OUT_WRITE_ENABLE));
        }
        set_result(static_cast<uint8_t>(result | regs_[R_RESULT]));
        regs_[R_STATUS] |= STATUS_INT;
        finish_simple_command();
    }

    void finish_simple_command() {
        regs_[R_STATUS] &= ~(STATUS_BUSY | STATUS_TRANSFERRING);
        stop_activity();

        uint8_t unload_count = regs_[R_HEAD_LOAD] >> 4;
        if (unload_count == 0) {
            unload_head();
        } else if (unload_count != 0x0F) {
            regs_[R_INDEX_COUNT] = unload_count;
            index_action_ = IndexAction::Unload;
        }
    }

    void stop_activity() {
        parameter_mode_ = ParameterMode::None;
        index_action_ = IndexAction::None;
        timer_ = Timer::None;
        timer_ticks_ = 0;
        reader_ = Reader::Idle;
        writing_ = false;
    }

    // Abandon a command mid-flight: leave a whole byte on the disc if writing,
    // and drop INT so a following completion is a new edge.
    void abort() {
        if ((drive_out_ & OUT_WRITE_ENABLE) && current_drive()) {
            current_drive()->write_pulses(ibm_disc_format::fm_to_2us_pulses(0xFF, 0xFF));
            write_back_.note_write();
        }
        interrupt_held_low_ = true;
    }

    void break_reset() {
        abort();
        stop_activity();
        set_drive_out(0);
        // Status (and with it the mode) is cleared; result and data are not.
        regs_[R_STATUS] = 0;
    }

    // One sector (or ID) is done: move to the next, or finish
    void sector_complete() {
        if (!check_ready()) return;
        set_drive_out(drive_out_ & ~OUT_WRITE_ENABLE);
        stop_activity();

        --regs_[R_PARAM_3];
        if ((regs_[R_PARAM_3] & 0x1F) == 0) {
            finish_command(RESULT_OK);
            return;
        }
        regs_[R_PARAM_2] = static_cast<uint8_t>(regs_[R_PARAM_2] + (regs_[R_PARAM_4] & 0x3F));
        dispatch();
    }

    // =========================================================================
    // Drives
    // =========================================================================

    DiscDrive* current_drive() const {
        switch (drive_out_ & (OUT_SELECT_1 | OUT_SELECT_0)) {
            case OUT_SELECT_0: return drives_[0];
            case OUT_SELECT_1: return drives_[1];
            default: return nullptr;   // none, or both
        }
    }

    void set_drive_out(uint8_t value) {
        drive_out_ = value;
        bool motor = (value & OUT_LOAD_HEAD) != 0;
        uint8_t side = (value & OUT_SIDE) ? 1 : 0;
        if (drives_[0]) {
            drives_[0]->set_selected((value & OUT_SELECT_0) != 0);
            drives_[0]->set_motor(motor);
            drives_[0]->set_side(side);
        }
        if (drives_[1]) {
            drives_[1]->set_selected((value & OUT_SELECT_1) != 0);
            drives_[1]->set_motor(motor);
            drives_[1]->set_side(side);
        }
    }

    void unload_head() {
        set_drive_out(drive_out_ & ~(OUT_SELECT_1 | OUT_SELECT_0 | OUT_LOAD_HEAD));
    }

    // The drive inputs, as seen while the selected drive turns. READY needs
    // a disc turning in the drive, not only select and motor: with no disc
    // the index sensor never gives an edge, and a real drive does not
    // report ready.
    uint8_t drive_input() const {
        uint8_t input = IN_ALWAYS;
        DiscDrive* drive = current_drive();
        if (!drive || !drive->motor_on()) return input;
        if (drive->at_track_0()) input |= IN_TRACK_0;
        if (drive->has_disc()) {
            if (drive_out_ & OUT_SELECT_0) input |= IN_READY_0;
            if (drive_out_ & OUT_SELECT_1) input |= IN_READY_1;
        }
        if (drive->is_write_protected()) input |= IN_WRITE_PROTECT;
        if (drive->index_level()) input |= IN_INDEX;
        return input;
    }

    // Sample the drive inputs. The ready bits latch low: once a drive has
    // been seen not ready, it stays so until a READ DRIVE STATUS command.
    uint8_t read_drive_status() {
        uint8_t input = drive_input();
        regs_[R_DRIVE_INPUT] = input;
        input &= static_cast<uint8_t>(regs_[R_LATCHED_INPUT] | 0xBB);
        regs_[R_LATCHED_INPUT] = input;
        return input;
    }

    bool check_ready() {
        read_drive_status();
        uint8_t ready = (drive_out_ & OUT_SELECT_1) ? IN_READY_1 : IN_READY_0;
        if (!(regs_[R_LATCHED_INPUT] & ready)) {
            finish_command(RESULT_NOT_READY);
            return false;
        }
        return true;
    }

    bool check_write_protect() {
        if (regs_[R_LATCHED_INPUT] & IN_WRITE_PROTECT) {
            finish_command(RESULT_WRITE_PROTECTED);
            return false;
        }
        return true;
    }

    // =========================================================================
    // Seek, head load, settle
    // =========================================================================

    uint8_t* track_registers() {
        return &regs_[(drive_out_ & OUT_SELECT_1) ? R_DRIVE_1_TRACKS : R_DRIVE_0_TRACKS];
    }

    void seek(Purpose purpose) {
        purpose_ = purpose;
        reseek();
    }

    // Seek to the logical track, plus any retries after a track mismatch
    void reseek() {
        reader_ = Reader::Idle;
        uint8_t* tracks = track_registers();
        uint8_t target = static_cast<uint8_t>(regs_[R_PARAM_1] + regs_[R_PARAM_COUNT]);
        // Step past each bad track at or below the target. The arithmetic is
        // eight-bit and wraps, as on the chip.
        if (target > 0) {
            if (tracks[0] <= target) ++target;
            if (tracks[1] <= target) ++target;
        }
        regs_[R_SEEK_TARGET] = target;
        regs_[R_SEEK_TARGET_COPY] = target;

        if (target >= 43) {
            drive_out_ |= OUT_LOW_HEAD_CURRENT;
        } else {
            drive_out_ &= ~OUT_LOW_HEAD_CURRENT;
        }

        // A seek to track 0 steps out until TRK0, whatever the register says
        uint8_t current = (target == 0) ? 0xFF : tracks[2];
        stepped_ = false;
        if (target == current) {
            load_head();
            return;
        }
        if (target > current) {
            regs_[R_SEEK_COUNT] = static_cast<uint8_t>(target - current);
            drive_out_ |= OUT_DIRECTION;
        } else {
            regs_[R_SEEK_COUNT] = static_cast<uint8_t>(current - target);
            drive_out_ &= ~OUT_DIRECTION;
        }
        // Step pulses are about 10us: too short to see in the register
        drive_out_ &= ~OUT_STEP;

        // The track register takes the target before the head moves
        tracks[2] = target;
        if (regs_[R_STATUS] & MODE_SINGLE_ACTUATOR) {
            regs_[R_DRIVE_0_TRACKS + 2] = target;
            regs_[R_DRIVE_1_TRACKS + 2] = target;
        }
        seek_step();
    }

    void seek_step() {
        DiscDrive* drive = current_drive();
        bool at_track_0 = drive && drive->at_track_0();
        if ((at_track_0 && regs_[R_SEEK_TARGET] == 0) || regs_[R_SEEK_COUNT] == 0) {
            load_head();
            return;
        }
        stepped_ = true;
        --regs_[R_SEEK_COUNT];
        if (drive) {
            if (drive_out_ & OUT_DIRECTION) {
                drive->step_in();
            } else {
                drive->step_out();
            }
        }
        // Step rate in 2ms units; 0 leaves it to the drive, taken as 3ms
        uint8_t rate = regs_[R_STEP_RATE];
        start_timer(Timer::SeekStep, rate == 0 ? 3 : rate * 2);
    }

    void load_head() {
        int wait_ms = 0;
        if (!(drive_out_ & OUT_LOAD_HEAD)) {
            set_drive_out(drive_out_ | OUT_LOAD_HEAD);
            wait_ms = (regs_[R_HEAD_LOAD] & 0x0F) * 4;   // 4ms units
        } else if (stepped_) {
            wait_ms = regs_[R_SETTLE_TIME] * 2;          // 2ms units
        }
        if (wait_ms > 0) {
            start_timer(Timer::PostSeek, wait_ms);
        } else {
            post_seek();
        }
    }

    void post_seek() {
        timer_ = Timer::None;
        if (!check_ready()) return;

        switch (purpose_) {
            case Purpose::Seek:
                finish_command(RESULT_OK);
                break;
            case Purpose::ReadId:
                index_action_ = IndexAction::StartReadId;
                break;
            case Purpose::Format:
                set_up_sector_size();
                index_action_ = IndexAction::StartFormat;
                check_write_protect();
                break;
            case Purpose::Read:
            case Purpose::Write:
                set_up_sector_size();
                start_index_timeout();
                start_syncing_for_id();
                if (purpose_ == Purpose::Write) check_write_protect();
                break;
        }
    }

    void start_timer(Timer timer, int ms) {
        timer_ = timer;
        timer_ticks_ = ms * TICKS_PER_MS;
    }

    void timer_fired() {
        Timer timer = timer_;
        timer_ = Timer::None;
        switch (timer) {
            case Timer::SeekStep: seek_step(); break;
            case Timer::PostSeek: post_seek(); break;
            case Timer::None: break;
        }
    }

    // =========================================================================
    // Sector size and the byte counter
    // =========================================================================

    uint32_t sector_size() const { return 128u << (regs_[R_PARAM_3] >> 5); }

    void set_up_sector_size() {
        uint8_t msb = static_cast<uint8_t>(sector_size() / 128 - 1);
        regs_[R_COUNT_LSB] = 0x80;
        regs_[R_COUNT_MSB] = msb;
        regs_[R_POINTER] = msb;
    }

    void reload_byte_counter() {
        regs_[R_COUNT_LSB] = 0x80;
        regs_[R_COUNT_MSB] = regs_[R_POINTER];
    }

    // Count one byte of the sector; true at the last. The counter runs in
    // 128-byte chunks, LSB from &80 down.
    bool count_byte() {
        if (--regs_[R_COUNT_LSB] != 0) return false;
        if (--regs_[R_COUNT_MSB] != 0xFF) {
            regs_[R_COUNT_LSB] = 0x80;
            return false;
        }
        regs_[R_COUNT_MSB] = 0;
        regs_[R_STATUS] &= ~STATUS_TRANSFERRING;
        return true;
    }

    // =========================================================================
    // Data transfer to and from the host
    // =========================================================================

    bool transferring() const {
        return (regs_[R_STATUS] & STATUS_TRANSFERRING) == STATUS_TRANSFERRING;
    }

    // Ask the host for (or offer it) a byte. The previous request must have
    // been met; on a BBC only non-DMA transfers work, and scan needs DMA.
    bool request_byte() {
        uint8_t g = group();
        bool late = !(regs_[R_STATUS] & MODE_NON_DMA) || g == SCAN || g == SCAN_DELETED ||
                    (regs_[R_STATUS] & STATUS_DATA_REQUEST);
        if (late) {
            abort();
            finish_command(RESULT_LATE_DMA);
            return false;
        }
        regs_[R_STATUS] |= STATUS_INT | STATUS_DATA_REQUEST;
        return true;
    }

    // =========================================================================
    // Index
    // =========================================================================

    void start_index_timeout() {
        regs_[R_INDEX_COUNT] = 3;
        index_action_ = IndexAction::Timeout;
    }

    void index_edge() {
        switch (index_action_) {
            case IndexAction::None:
                break;
            case IndexAction::Timeout:
                if (--regs_[R_INDEX_COUNT] == 0) finish_command(RESULT_SECTOR_NOT_FOUND);
                break;
            case IndexAction::Unload:
                if (--regs_[R_INDEX_COUNT] == 0) {
                    unload_head();
                    index_action_ = IndexAction::None;
                }
                break;
            case IndexAction::StartFormat:
                // Format has no timeout: it ends at the index after the last
                // sector, however long the sectors. GAP5 (an index address
                // mark) is not written.
                index_action_ = IndexAction::None;
                --regs_[R_PARAM_2];   // GAP3 counts the CRC's trailing FF
                load_format_sector(regs_[R_PARAM_5]);   // GAP1
                // Writing begins in this slot
                set_drive_out(drive_out_ | OUT_WRITE_ENABLE);
                produce_byte();
                break;
            case IndexAction::EndFormat:
                sector_complete();
                break;
            case IndexAction::StartReadId:
                start_index_timeout();
                start_syncing_for_id();
                break;
        }
    }

    // =========================================================================
    // One FM byte of disc time for the selected drive
    // =========================================================================

    void byte_slot() {
        DiscDrive* drive = current_drive();
        if (!drive || !drive->motor_on()) return;

        uint32_t pulses = drive->read_pulses();

        bool index = drive->index_level();
        bool edge = index && !index_was_asserted_;
        index_was_asserted_ = index;
        if (edge) index_edge();

        // The write gate writes whatever the output registers hold, even
        // outside a command.
        if (drive_out_ & OUT_WRITE_ENABLE) {
            drive->write_pulses(ibm_disc_format::fm_to_2us_pulses(out_clocks_, out_data_));
            write_back_.note_write();
        }

        if (writing_) {
            produce_byte();
        } else {
            out_data_ = regs_[R_DATA];
            if (reader_ != Reader::Idle) {
                for (int i = 0; i < 16; ++i) {
                    shift_cell((pulses & 0xC0000000u) != 0);
                    pulses <<= 2;
                    if (reader_ == Reader::Idle || writing_) break;
                }
            }
        }
    }

    // =========================================================================
    // Read side: the FM bit stream, one 4us cell at a time
    // =========================================================================

    void start_syncing_for_id() {
        regs_[R_HEADER_POINTER] = R_ID_TRACK;
        start_syncing(Reader::SyncForId);
    }

    void start_syncing(Reader reader) {
        reader_ = reader;
        sync_cells_ = 0;
        shift_ = 0;
        shift_cells_ = 0;
    }

    void shift_cell(bool bit) {
        switch (reader_) {
            case Reader::Idle:
                return;
            case Reader::RecoverAfterId:
                // After an ID that is not wanted the chip needs a few bytes
                // before it can sync again.
                if (++sync_cells_ == ID_RECOVERY_CELLS) start_syncing_for_id();
                return;
            case Reader::SyncForId:
            case Reader::SyncForData: {
                // Sync is a run of 00 bytes: clock cells 1, data cells 0. A 1
                // in a data cell after enough of them starts an address mark.
                bool expected = (sync_cells_ % 2) == 0;
                if (bit == expected) {
                    ++sync_cells_;
                } else if (sync_cells_ >= SYNC_CELLS && (sync_cells_ % 2) == 1) {
                    reader_ = (reader_ == Reader::SyncForId) ? Reader::IdMark : Reader::DataMark;
                    shift_ = 0x3;
                    shift_cells_ = 2;
                } else {
                    sync_cells_ = bit ? 1 : 0;
                }
                return;
            }
            default:
                break;
        }

        shift_ = static_cast<uint16_t>((shift_ << 1) | (bit ? 1 : 0));
        if (++shift_cells_ < 16) return;
        shift_cells_ = 0;

        uint8_t clocks = 0;
        uint8_t data = 0;
        for (int i = 0; i < 8; ++i) {
            clocks = static_cast<uint8_t>((clocks << 1) | ((shift_ >> (15 - 2 * i)) & 1));
            data = static_cast<uint8_t>((data << 1) | ((shift_ >> (14 - 2 * i)) & 1));
        }
        shift_ = 0;

        // Within a synced stream, a byte with missing clocks is not
        // delivered; marks and the GAP2 skip take whatever comes.
        bool takes_any = reader_ == Reader::IdMark || reader_ == Reader::DataMark ||
                         reader_ == Reader::SkipGap2;
        if (clocks != 0xFF && !takes_any) return;
        byte_read(data, clocks);
    }

    void byte_read(uint8_t data, uint8_t clocks) {
        if (transferring()) {
            if (!request_byte()) return;
            regs_[R_DATA] = data;
        }

        switch (reader_) {
            case Reader::IdMark:
                if (clocks == ibm_disc_format::k_mark_clock_pattern &&
                    data == ibm_disc_format::k_id_mark_data_pattern) {
                    crc_ = crc_add(crc_init(), data);
                    if (group() == READ_ID) regs_[R_STATUS] |= STATUS_TRANSFERRING;
                    reader_ = Reader::IdField;
                } else {
                    start_syncing_for_id();
                }
                break;

            case Reader::IdField:
                crc_ = crc_add(crc_, data);
                write_special(regs_[R_HEADER_POINTER], data);
                --regs_[R_HEADER_POINTER];
                if ((regs_[R_HEADER_POINTER] & 0x07) == 0) {
                    regs_[R_STATUS] &= ~STATUS_TRANSFERRING;
                    disc_crc_ = 0;
                    crc_bytes_ = 0;
                    reader_ = Reader::IdCrc;
                }
                break;

            case Reader::IdCrc:
                disc_crc_ = static_cast<uint16_t>((disc_crc_ << 8) | data);
                if (++crc_bytes_ < 2) break;
                // An ID CRC error ends the command, even for an ID not wanted
                if (disc_crc_ != crc_) {
                    finish_command(RESULT_ID_CRC_ERROR);
                } else if (group() == READ_ID) {
                    sector_complete();
                } else if (regs_[R_ID_TRACK] != regs_[R_PARAM_1]) {
                    // Wrong track: try the next track, then the one after
                    if (++regs_[R_PARAM_COUNT] == 3) {
                        finish_command(RESULT_SECTOR_NOT_FOUND);
                    } else {
                        reseek();
                    }
                } else if (regs_[R_ID_SECTOR] == regs_[R_PARAM_2]) {
                    gap2_skip_ = GAP2_SKIP_BYTES;
                    reader_ = Reader::SkipGap2;
                } else {
                    sync_cells_ = 0;
                    reader_ = Reader::RecoverAfterId;
                }
                break;

            case Reader::SkipGap2:
                if (--gap2_skip_ != 0) break;
                if (purpose_ == Purpose::Write) {
                    reader_ = Reader::Idle;
                    start_write_sector();
                } else {
                    start_syncing(Reader::SyncForData);
                }
                break;

            case Reader::DataMark: {
                bool normal = data == ibm_disc_format::k_data_mark_data_pattern;
                bool deleted = data == ibm_disc_format::k_deleted_data_mark_data_pattern;
                if (clocks != ibm_disc_format::k_mark_clock_pattern || !(normal || deleted)) {
                    finish_command(RESULT_CLOCK_ERROR);
                    break;
                }
                // A deleted sector is read but not handed over unless the
                // command asks for deleted data; verify hands over nothing.
                bool hand_over = group() != VERIFY;
                if (deleted) {
                    set_result(RESULT_DELETED_DATA);
                    if (!(regs_[R_COMMAND] & 0x04)) hand_over = false;
                }
                if (hand_over) regs_[R_STATUS] |= STATUS_TRANSFERRING;
                crc_ = crc_add(crc_init(), data);
                reader_ = Reader::DataField;
                break;
            }

            case Reader::DataField:
                crc_ = crc_add(crc_, data);
                if (count_byte()) {
                    disc_crc_ = 0;
                    crc_bytes_ = 0;
                    reader_ = Reader::DataCrc;
                }
                break;

            case Reader::DataCrc:
                disc_crc_ = static_cast<uint16_t>((disc_crc_ << 8) | data);
                if (++crc_bytes_ < 2) break;
                if (disc_crc_ != crc_) {
                    finish_command(RESULT_DATA_CRC_ERROR);
                } else {
                    sector_complete();
                }
                break;

            default:
                break;
        }
    }

    // =========================================================================
    // Write side: a program of steps, one byte prepared per slot and
    // written in the next
    // =========================================================================

    struct WriteStep {
        enum Kind : uint8_t {
            Fill,        // `count` copies of `byte`
            CrcFill,     // the same, counted in the CRC (format's E5 fill)
            Mark,        // an address mark: clocks C7, starts the CRC
            Host,        // `count` bytes from the host
            CrcHigh,
            CrcLow,
            EndOfSector, // the sector is written
            FormatNext,  // a formatted sector is done: another, or GAP4
        };
        Kind kind;
        uint8_t byte = 0;
        uint32_t count = 1;
    };

    static constexpr uint32_t UNTIL_INDEX = 0xFFFFFFFF;

    void load_program(std::initializer_list<WriteStep> steps) {
        size_t i = 0;
        for (const WriteStep& step : steps) program_[i++] = step;
        program_length_ = i;
        step_ = 0;
        step_bytes_ = 0;
        writing_ = true;
    }

    // Writing a sector starts 11 bytes after its ID: 6 x 00, the data mark,
    // the host's bytes, the CRC, and the FF the CRC generator follows it with.
    void start_write_sector() {
        reload_byte_counter();
        load_program({
            {WriteStep::Fill, 0x00, 6},
            {WriteStep::Mark, regs_[R_DATA_MARK]},
            {WriteStep::Host, 0, sector_size()},
            {WriteStep::CrcHigh},
            {WriteStep::CrcLow},
            {WriteStep::Fill, 0xFF, 1},
            {WriteStep::EndOfSector},
        });
        set_drive_out(drive_out_ | OUT_WRITE_ENABLE);
        produce_byte();
    }

    // A formatted sector: gap FFs, 6 x 00, the ID mark, four ID bytes from
    // the host, CRC and FF, GAP2 (10 more FFs, 6 x 00), the data mark, the
    // E5 fill, CRC and FF.
    void load_format_sector(uint8_t gap_ffs) {
        load_program({
            {WriteStep::Fill, 0xFF, gap_ffs == 0 ? 256u : gap_ffs},
            {WriteStep::Fill, 0x00, 6},
            {WriteStep::Mark, ibm_disc_format::k_id_mark_data_pattern},
            {WriteStep::Host, 0, 4},
            {WriteStep::CrcHigh},
            {WriteStep::CrcLow},
            {WriteStep::Fill, 0xFF, 11},
            {WriteStep::Fill, 0x00, 6},
            {WriteStep::Mark, ibm_disc_format::k_data_mark_data_pattern},
            {WriteStep::CrcFill, 0xE5, sector_size()},
            {WriteStep::CrcHigh},
            {WriteStep::CrcLow},
            {WriteStep::Fill, 0xFF, 1},
            {WriteStep::FormatNext},
        });
    }

    void format_next() {
        --regs_[R_PARAM_3];
        if ((regs_[R_PARAM_3] & 0x1F) == 0) {
            // Last sector written: FFs to the index, where the command ends
            regs_[R_PARAM_3] = 1;
            index_action_ = IndexAction::EndFormat;
            load_program({{WriteStep::Fill, 0xFF, UNTIL_INDEX}});
        } else {
            load_format_sector(regs_[R_PARAM_2]);   // GAP3
        }
    }

    // Prepare the next byte to write.
    void produce_byte() {
        while (writing_ && step_ < program_length_) {
            const WriteStep& step = program_[step_];
            switch (step.kind) {
                case WriteStep::EndOfSector:
                    writing_ = false;
                    sector_complete();
                    return;
                case WriteStep::FormatNext:
                    format_next();
                    continue;
                default:
                    break;
            }
            uint32_t length = (step.kind == WriteStep::Fill || step.kind == WriteStep::CrcFill ||
                               step.kind == WriteStep::Host) ? step.count : 1;
            if (step.count != UNTIL_INDEX && step_bytes_ >= length) {
                ++step_;
                step_bytes_ = 0;
                continue;
            }
            ++step_bytes_;
            switch (step.kind) {
                case WriteStep::Fill:
                    put(step.byte, 0xFF);
                    break;
                case WriteStep::CrcFill:
                    put(step.byte, 0xFF);
                    crc_ = crc_add(crc_, step.byte);
                    break;
                case WriteStep::Mark:
                    crc_ = crc_add(crc_init(), step.byte);
                    put(step.byte, ibm_disc_format::k_mark_clock_pattern);
                    break;
                case WriteStep::Host: {
                    uint8_t byte = regs_[R_DATA];
                    put(byte, 0xFF);
                    crc_ = crc_add(crc_, byte);
                    if (purpose_ == Purpose::Write) count_byte();
                    break;
                }
                case WriteStep::CrcHigh:
                    put(static_cast<uint8_t>(crc_ >> 8), 0xFF);
                    break;
                case WriteStep::CrcLow:
                    put(static_cast<uint8_t>(crc_ & 0xFF), 0xFF);
                    break;
                default:
                    break;
            }
            request_next_host_byte();
            return;
        }
    }

    // When the byte after the one just prepared comes from the host, ask for
    // it now: the host has one byte time to supply it.
    void request_next_host_byte() {
        const WriteStep& step = program_[step_];
        bool next_is_host = false;
        if (step.kind == WriteStep::Mark) {
            next_is_host = step_ + 1 < program_length_ && program_[step_ + 1].kind == WriteStep::Host;
            if (next_is_host) regs_[R_STATUS] |= STATUS_TRANSFERRING;
        } else if (step.kind == WriteStep::Host) {
            next_is_host = step_bytes_ < step.count;
            if (!next_is_host) regs_[R_STATUS] &= ~STATUS_TRANSFERRING;
        }
        if (next_is_host) request_byte();
    }

    void put(uint8_t data, uint8_t clocks) {
        out_data_ = data;
        out_clocks_ = clocks;
    }

    static uint16_t crc_init() { return ibm_disc_format::crc_init(false); }
    static uint16_t crc_add(uint16_t crc, uint8_t byte) { return ibm_disc_format::crc_add_byte(crc, byte); }

    // =========================================================================
    // State
    // =========================================================================

    std::array<uint8_t, 32> regs_{};
    bool result_ready_ = false;
    uint8_t drive_out_ = 0;
    uint8_t out_data_ = 0;     // bit processor data register: the byte to write
    uint8_t out_clocks_ = 0;   // bit processor clocks register
    bool interrupt_held_low_ = false;

    std::array<DiscDrive*, 2> drives_{nullptr, nullptr};
    DiscWriteBack write_back_;

    ParameterMode parameter_mode_ = ParameterMode::None;
    Purpose purpose_ = Purpose::Seek;
    Timer timer_ = Timer::None;
    int timer_ticks_ = 0;
    IndexAction index_action_ = IndexAction::None;
    bool stepped_ = false;
    bool index_was_asserted_ = false;
    int byte_phase_ = 0;

    // Read side
    Reader reader_ = Reader::Idle;
    int sync_cells_ = 0;
    uint16_t shift_ = 0;
    int shift_cells_ = 0;
    uint16_t crc_ = 0;
    uint16_t disc_crc_ = 0;
    int crc_bytes_ = 0;
    uint8_t gap2_skip_ = 0;

    // Write side
    std::array<WriteStep, 16> program_{};
    size_t program_length_ = 0;
    size_t step_ = 0;
    uint32_t step_bytes_ = 0;
    bool writing_ = false;
};

} // namespace beebium
