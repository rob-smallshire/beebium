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

#include "IbmDiscFormat.hpp"
#include "DiscDrive.hpp"

#include <array>
#include <cstdint>

namespace beebium {

// Pulse-driven WD1770 Floppy Disc Controller emulation.
//
// Unlike the original sector-level WD1770 which called drive->read_sector(),
// this implementation reads individual FM/MFM-encoded bytes from the drive's
// pulse stream, scanning for address marks and matching sector IDs -- exactly
// how real hardware operates.
//
// This means the WD1770 is format-agnostic: it doesn't know or care whether
// the underlying disc is an SSD, HFE, or any other format. It just reads
// pulses. The same code works for FM (single density, 8271-era discs) and
// MFM (double density, ADFS discs).
//
// The external register interface is identical to the sector-level WD1770:
//   Offset 0: Status (read) / Command (write)
//   Offset 1: Track register (read/write)
//   Offset 2: Sector register (read/write)
//   Offset 3: Data register (read/write)
class WD1770 {
public:
    // Status register bits
    static constexpr uint8_t STATUS_BUSY        = 0x01;
    static constexpr uint8_t STATUS_DRQ         = 0x02;  // Type II/III
    static constexpr uint8_t STATUS_INDEX       = 0x02;  // Type I
    static constexpr uint8_t STATUS_TRACK0      = 0x04;  // Type I
    static constexpr uint8_t STATUS_LOST_DATA   = 0x04;  // Type II/III
    static constexpr uint8_t STATUS_CRC_ERROR   = 0x08;
    static constexpr uint8_t STATUS_SEEK_ERROR  = 0x10;  // Type I
    static constexpr uint8_t STATUS_RNF         = 0x10;  // Type II/III
    static constexpr uint8_t STATUS_SPIN_UP     = 0x20;  // Type I
    static constexpr uint8_t STATUS_RECORD_TYPE = 0x20;  // Type II/III (deleted mark)
    static constexpr uint8_t STATUS_WRITE_PROT  = 0x40;
    static constexpr uint8_t STATUS_MOTOR_ON    = 0x80;

    // Command codes (upper 4 bits)
    static constexpr uint8_t CMD_RESTORE        = 0x00;
    static constexpr uint8_t CMD_SEEK           = 0x10;
    static constexpr uint8_t CMD_STEP           = 0x20;
    static constexpr uint8_t CMD_STEP_IN        = 0x40;
    static constexpr uint8_t CMD_STEP_OUT       = 0x60;
    static constexpr uint8_t CMD_READ_SECTOR    = 0x80;
    static constexpr uint8_t CMD_WRITE_SECTOR   = 0xA0;
    static constexpr uint8_t CMD_READ_ADDRESS   = 0xC0;
    static constexpr uint8_t CMD_READ_TRACK     = 0xE0;
    static constexpr uint8_t CMD_WRITE_TRACK    = 0xF0;
    static constexpr uint8_t CMD_FORCE_INT      = 0xD0;

    // Command flags
    static constexpr uint8_t FLAG_UPDATE_TRACK  = 0x10;
    static constexpr uint8_t FLAG_MULTI_SECTOR  = 0x10;

    // Density mode
    enum class Density { SingleFM, DoubleMFM };

    WD1770() = default;
    ~WD1770() = default;

    WD1770(const WD1770&) = delete;
    WD1770& operator=(const WD1770&) = delete;
    WD1770(WD1770&&) = default;
    WD1770& operator=(WD1770&&) = default;

    // =========================================================================
    // Register Interface
    // =========================================================================

    uint8_t read(uint16_t offset) {
        switch (offset & 0x03) {
            case 0: {
                intrq_ = false;
                uint8_t result = status_;
                if (drq_) result |= STATUS_DRQ;
                return result;
            }
            case 1: return track_;
            case 2: return sector_;
            case 3:
                drq_ = false;
                return data_;
            default: return 0xFF;
        }
    }

    // Read a register for inspection: as read(), but reading the status leaves
    // INTRQ set and reading the data register leaves DRQ set.
    uint8_t peek(uint16_t offset) const {
        switch (offset & 0x03) {
            case 0: return drq_ ? static_cast<uint8_t>(status_ | STATUS_DRQ) : status_;
            case 1: return track_;
            case 2: return sector_;
            default: return data_;
        }
    }

    void write(uint16_t offset, uint8_t value) {
        switch (offset & 0x03) {
            case 0: execute_command(value); break;
            case 1: if (!(status_ & STATUS_BUSY)) track_ = value; break;
            case 2: if (!(status_ & STATUS_BUSY)) sector_ = value; break;
            case 3: data_ = value; drq_ = false; break;
        }
    }

    // =========================================================================
    // Clock Tick (1MHz)
    // =========================================================================

    void tick() {
        // Write-back flush on write inactivity.
        //
        // Once disc writes stop for a short period, flush dirty tracks to the
        // host image. This is deliberately independent of motor spin-down
        // (~2 seconds) and of eject quiescence: it fires promptly (well within
        // the motor-on window) so saved data reaches the host file quickly, and
        // because it always completes long before the motor-off/eject path can
        // run, it cannot race a concurrent eject for the same disc.
        if (writes_pending_flush_) {
            if (++write_idle_ticks_ >= FLUSH_IDLE_TICKS) {
                flush_dirty_drives(/*sync_to_disk=*/true);
                writes_pending_flush_ = false;
                write_idle_ticks_ = 0;
            }
        }

        // Motor spindown when idle
        if (!(status_ & STATUS_BUSY)) {
            if (motor_on_) {
                if (++idle_ticks_ >= MOTOR_OFF_DELAY_TICKS) {
                    spin_down();
                    idle_ticks_ = 0;
                }
            }
            return;
        }

        // Motor spin-up delay
        if (spin_up_delay_ > 0) {
            --spin_up_delay_;
            return;
        }

        // Step delay (Type I commands)
        if (step_delay_ > 0) {
            --step_delay_;
            return;
        }

        // Byte delay (inter-byte timing for Type II/III)
        if (byte_delay_ > 0) {
            --byte_delay_;
            return;
        }

        // Dispatch to current command's state machine
        switch (phase_) {
            case Phase::Idle: break;

            // Type I
            case Phase::Restore: tick_restore(); break;
            case Phase::Seek: tick_seek(); break;
            case Phase::Step: tick_step(); break;

            // Type II - Read Sector
            case Phase::SearchingForIdRead:
            case Phase::ReadingIdField:
            case Phase::CheckingIdForRead:
            case Phase::SearchingForDataRead:
            case Phase::ReadingSectorData:
                tick_read_sector_phase();
                break;

            // Type II - Write Sector
            case Phase::SearchingForIdWrite:
            case Phase::ReadingIdFieldWrite:
            case Phase::CheckingIdForWrite:
            case Phase::SearchingForDataWrite:
            case Phase::WritingGap2:
            case Phase::WritingDataMark:
            case Phase::WritingSectorData:
            case Phase::WritingCrc:
                tick_write_sector_phase();
                break;

            // Type III - Read Address
            case Phase::SearchingForIdReadAddr:
            case Phase::ReadingIdFieldReadAddr:
            case Phase::TransferringIdField:
                tick_read_address_phase();
                break;

            // Type III - Read Track
            case Phase::ReadingTrackData:
                tick_read_track_phase();
                break;

            // Type III - Write Track
            case Phase::WritingTrackData:
                tick_write_track_phase();
                break;
        }
    }

    // =========================================================================
    // Status Signals
    // =========================================================================

    bool drq() const { return drq_; }
    bool intrq() const { return intrq_; }
    bool busy() const { return (status_ & STATUS_BUSY) != 0; }
    bool nmi_pending() const { return drq_ || intrq_; }

    // =========================================================================
    // Drive Attachment
    // =========================================================================

    void attach_drive(int drive_num, DiscDrive* drive) {
        if (drive_num >= 0 && drive_num < 2) {
            drives_[static_cast<size_t>(drive_num)] = drive;
        }
    }

    // =========================================================================
    // External Control Signals
    // =========================================================================

    void set_side(uint8_t side) { selected_side_ = side & 1; }
    void set_drive(uint8_t drive) { selected_drive_ = drive & 1; }
    void set_density(Density density) { density_ = density; }

    // Convenience: set_density from bool (true = MFM, false = FM).
    // Prefer set_density(Density) for clarity.
    void set_density(bool double_density) {
        density_ = double_density ? Density::DoubleMFM : Density::SingleFM;
    }

    uint8_t selected_side() const { return selected_side_; }
    uint8_t selected_drive() const { return selected_drive_; }
    Density density() const { return density_; }
    bool is_double_density() const { return density_ == Density::DoubleMFM; }

    // =========================================================================
    // Configuration
    // =========================================================================

    void set_spin_up_delay_enabled(bool enabled) { spin_up_delay_enabled_ = enabled; }
    bool spin_up_delay_enabled() const { return spin_up_delay_enabled_; }

    // =========================================================================
    // Reset
    // =========================================================================

    void reset() {
        status_ = 0x00;
        track_ = 0x00;
        sector_ = 0x01;
        data_ = 0x00;
        current_command_ = 0x00;
        phase_ = Phase::Idle;
        drq_ = false;
        intrq_ = false;
        selected_side_ = 0;
        selected_drive_ = 0;
        density_ = Density::SingleFM;
        step_direction_ = 1;
        step_delay_ = 0;
        byte_delay_ = 0;
        byte_counter_ = 0;
        index_count_ = 0;
        if (motor_on_) spin_down();
        idle_ticks_ = 0;
        spin_up_delay_ = 0;
    }

    const char* name() const { return "WD1770"; }

private:
    // =========================================================================
    // Internal Phases
    // =========================================================================

    enum class Phase {
        Idle,

        // Type I
        Restore,
        Seek,
        Step,

        // Type II - Read Sector
        SearchingForIdRead,      // Scanning for ID address mark
        ReadingIdField,          // Reading 4 ID bytes + 2 CRC
        CheckingIdForRead,       // Comparing ID against requested sector
        SearchingForDataRead,    // Found matching ID, looking for data mark
        ReadingSectorData,       // Transferring sector data via DRQ

        // Type II - Write Sector
        SearchingForIdWrite,
        ReadingIdFieldWrite,
        CheckingIdForWrite,
        SearchingForDataWrite,   // Wait for data mark position
        WritingGap2,             // Writing gap before data field
        WritingDataMark,
        WritingSectorData,       // Receiving bytes via DRQ, encoding to pulses
        WritingCrc,

        // Type III - Read Address
        SearchingForIdReadAddr,
        ReadingIdFieldReadAddr,
        TransferringIdField,     // Sending 6 ID bytes via DRQ

        // Type III - Read Track
        ReadingTrackData,

        // Type III - Write Track
        WritingTrackData,
    };

    // Timing constants
    static constexpr int US_PER_FM_BYTE = 64;    // 64us per FM byte at 250kbps
    static constexpr int US_PER_MFM_BYTE = 32;   // 32us per MFM byte at 250kbps
    static constexpr int MOTOR_OFF_DELAY_TICKS = 2'000'000;
    static constexpr int SPIN_UP_DELAY_TICKS = 1'200'000;
    // Flush dirty tracks after this much write inactivity (~250ms at 1MHz).
    // Much shorter than MOTOR_OFF_DELAY_TICKS so writes reach the host file
    // promptly and always before the motor-off/eject path can run.
    static constexpr int FLUSH_IDLE_TICKS = 250'000;
    static constexpr int MAX_INDEX_REVOLUTIONS = 5;  // RNF after 5 revolutions

    int byte_period() const { return is_double_density() ? US_PER_MFM_BYTE : US_PER_FM_BYTE; }

    // =========================================================================
    // Pulse-Level Byte Access
    // =========================================================================

    // Read the next FM byte from the current drive position and advance.
    // Returns the decoded data byte. Also provides clock byte for mark detection.
    struct FmByte {
        uint8_t clocks;
        uint8_t data;
        bool index_pulse;
    };

    FmByte read_next_fm_byte() {
        DiscDrive* drive = get_current_drive();
        if (!drive) return {0, 0, false};

        uint32_t pulses = drive->read_pulses();
        bool index = drive->advance_head();
        auto [c, d, iffy] = ibm_disc_format::fm_from_2us_pulses(pulses);
        return {c, d, index};
    }

    // Read the next MFM byte from the current drive position and advance.
    struct MfmByte {
        uint8_t data;
        uint16_t raw;   // Raw 16-bit pulse data (for sync detection)
        bool index_pulse;
    };

    MfmByte read_next_mfm_byte() {
        DiscDrive* drive = get_current_drive();
        if (!drive) return {0, 0, false};

        uint32_t pulses = drive->read_pulses();
        uint16_t half;
        if (drive->pulse_sub_position() == 0) {
            half = static_cast<uint16_t>(pulses >> 16);
        } else {
            half = static_cast<uint16_t>(pulses & 0xFFFF);
        }
        bool index = drive->advance_head_half();
        uint8_t data = ibm_disc_format::mfm_from_2us_pulses(half);
        return {data, half, index};
    }

    // Read one byte from disc (FM or MFM depending on density setting).
    // Returns the data byte. Sets index_occurred_ if index pulse seen.
    uint8_t read_byte_from_disc() {
        if (is_double_density()) {
            auto [data, raw, index] = read_next_mfm_byte();
            if (index) on_index_pulse();
            last_mfm_raw_ = raw;
            return data;
        } else {
            auto [clocks, data, index] = read_next_fm_byte();
            if (index) on_index_pulse();
            last_fm_clocks_ = clocks;
            return data;
        }
    }

    // Write one FM byte to the disc at current position and advance.
    void write_fm_byte_to_disc(uint8_t data, uint8_t clocks = 0xFF) {
        DiscDrive* drive = get_current_drive();
        if (!drive) return;
        uint32_t pulses = ibm_disc_format::fm_to_2us_pulses(clocks, data);
        drive->write_pulses(pulses);
        note_disc_write();
        bool index = drive->advance_head();
        if (index) on_index_pulse();
    }

    // Write one MFM byte to the disc at current position and advance.
    void write_mfm_byte_to_disc(uint8_t data) {
        DiscDrive* drive = get_current_drive();
        if (!drive) return;
        uint16_t mfm_pulses = ibm_disc_format::mfm_to_2us_pulses(write_last_mfm_bit_, data);

        uint32_t word = drive->read_pulses();
        if (drive->pulse_sub_position() == 0) {
            word = (static_cast<uint32_t>(mfm_pulses) << 16) | (word & 0x0000FFFF);
        } else {
            word = (word & 0xFFFF0000) | static_cast<uint32_t>(mfm_pulses);
        }
        drive->write_pulses(word);
        note_disc_write();
        bool index = drive->advance_head_half();
        if (index) on_index_pulse();
    }

    // Check if last read byte was an FM address mark (special clock pattern).
    bool is_fm_address_mark() const {
        return last_fm_clocks_ == ibm_disc_format::k_mark_clock_pattern;
    }

    // Check if last read MFM raw value was an A1 sync word.
    bool is_mfm_a1_sync() const {
        return last_mfm_raw_ == ibm_disc_format::k_mfm_a1_sync;
    }

    void on_index_pulse() {
        ++index_count_;
    }

    // =========================================================================
    // Command Execution
    // =========================================================================

    void execute_command(uint8_t cmd) {
        // Force Interrupt (Type IV) can be issued at any time
        if ((cmd & 0xF0) == CMD_FORCE_INT) {
            status_ &= ~STATUS_BUSY;
            phase_ = Phase::Idle;
            drq_ = false;
            uint8_t flags = cmd & 0x0F;
            if (flags == 0) {
                // Terminate without interrupt
            } else {
                intrq_ = true;
            }
            return;
        }

        if (status_ & STATUS_BUSY) return;

        current_command_ = cmd;
        status_ = STATUS_BUSY;
        intrq_ = false;

        spin_up();

        // Step rates: 6ms, 12ms, 20ms, 30ms
        static constexpr int step_rates[] = {6000, 12000, 20000, 30000};
        selected_step_rate_ = step_rates[cmd & 0x03];

        uint8_t cmd_type = cmd & 0xE0;
        if (cmd_type == 0x00) {
            if ((cmd & 0xF0) == CMD_SEEK) {
                phase_ = Phase::Seek;
                step_delay_ = selected_step_rate_;
            } else {
                phase_ = Phase::Restore;
                step_delay_ = selected_step_rate_;
            }
        } else if (cmd_type == 0x20 || cmd_type == 0x40 || cmd_type == 0x60) {
            if (cmd_type == 0x40) step_direction_ = 1;
            else if (cmd_type == 0x60) step_direction_ = -1;
            phase_ = Phase::Step;
            step_delay_ = selected_step_rate_;
        } else if (cmd_type == 0x80) {
            start_read_sector();
        } else if (cmd_type == 0xA0) {
            start_write_sector();
        } else {
            uint8_t cmd_nibble = cmd & 0xF0;
            if (cmd_nibble == CMD_READ_ADDRESS) {
                start_read_address();
            } else if (cmd_nibble == CMD_READ_TRACK) {
                start_read_track();
            } else {
                start_write_track();
            }
        }
    }

    // =========================================================================
    // Type I Commands
    // =========================================================================

    void tick_restore() {
        DiscDrive* drive = get_current_drive();
        if (!drive) { complete_command(); return; }
        if (drive->at_track_0()) {
            track_ = 0;
            complete_command();
            update_track0_status();
        } else {
            drive->step_out();
            step_delay_ = selected_step_rate_;
        }
    }

    void tick_seek() {
        DiscDrive* drive = get_current_drive();
        if (!drive) { complete_command(); return; }
        if (track_ == data_) {
            complete_command();
            update_track0_status();
        } else if (track_ < data_) {
            drive->step_in();
            ++track_;
            step_direction_ = 1;
            step_delay_ = selected_step_rate_;
        } else {
            drive->step_out();
            --track_;
            step_direction_ = -1;
            step_delay_ = selected_step_rate_;
        }
    }

    void tick_step() {
        DiscDrive* drive = get_current_drive();
        if (!drive) { complete_command(); return; }
        if (step_direction_ > 0) {
            drive->step_in();
            if (current_command_ & FLAG_UPDATE_TRACK) ++track_;
        } else {
            drive->step_out();
            if (current_command_ & FLAG_UPDATE_TRACK) {
                if (track_ > 0) --track_;
            }
        }
        complete_command();
        update_track0_status();
    }

    // =========================================================================
    // Type II: Read Sector (pulse-driven)
    // =========================================================================

    void start_read_sector() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }
        index_count_ = 0;
        phase_ = Phase::SearchingForIdRead;
        byte_delay_ = byte_period();
    }

    void tick_read_sector_phase() {
        // Check for 5-revolution timeout
        if (index_count_ >= MAX_INDEX_REVOLUTIONS) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        switch (phase_) {
            case Phase::SearchingForIdRead: {
                uint8_t byte = read_byte_from_disc();
                if (!is_double_density()) {
                    // FM: look for ID mark (clock=0xC7, data=0xFE)
                    if (is_fm_address_mark() && byte == ibm_disc_format::k_id_mark_data_pattern) {
                        id_crc_ = ibm_disc_format::crc_init(false);
                        id_crc_ = ibm_disc_format::crc_add_byte(id_crc_, byte);
                        id_byte_count_ = 0;
                        phase_ = Phase::ReadingIdField;
                    }
                } else {
                    // MFM: look for 3xA1 sync then ID mark
                    if (is_mfm_a1_sync()) {
                        ++mfm_sync_count_;
                    } else if (mfm_sync_count_ >= 3 && byte == ibm_disc_format::k_id_mark_data_pattern) {
                        id_crc_ = ibm_disc_format::crc_init(true);
                        id_crc_ = ibm_disc_format::crc_add_byte(id_crc_, byte);
                        id_byte_count_ = 0;
                        phase_ = Phase::ReadingIdField;
                        mfm_sync_count_ = 0;
                    } else {
                        mfm_sync_count_ = 0;
                    }
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::ReadingIdField: {
                uint8_t byte = read_byte_from_disc();
                id_field_buf_[id_byte_count_++] = byte;
                if (id_byte_count_ <= 4) {
                    id_crc_ = ibm_disc_format::crc_add_byte(id_crc_, byte);
                }
                if (id_byte_count_ == 6) {
                    // ID field complete: track, side, sector, size, CRC_hi, CRC_lo
                    uint16_t received_crc = (static_cast<uint16_t>(id_field_buf_[4]) << 8) |
                                             id_field_buf_[5];
                    id_crc_ok_ = (id_crc_ == received_crc);
                    phase_ = Phase::CheckingIdForRead;
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::CheckingIdForRead: {
                uint8_t id_track = id_field_buf_[0];
                uint8_t id_sector = id_field_buf_[2];
                uint8_t id_size = id_field_buf_[3];

                // Does this sector match what we're looking for?
                if (id_track == track_ && id_sector == sector_ && id_crc_ok_) {
                    data_size_ = 128u << id_size;
                    byte_counter_ = 0;
                    search_counter_ = 0;
                    phase_ = Phase::SearchingForDataRead;
                } else {
                    // Not the right sector, keep searching
                    phase_ = Phase::SearchingForIdRead;
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::SearchingForDataRead: {
                uint8_t byte = read_byte_from_disc();
                ++search_counter_;

                bool found_data_mark = false;
                bool is_deleted = false;

                if (!is_double_density()) {
                    if (is_fm_address_mark()) {
                        if (byte == ibm_disc_format::k_data_mark_data_pattern) {
                            found_data_mark = true;
                        } else if (byte == ibm_disc_format::k_deleted_data_mark_data_pattern) {
                            found_data_mark = true;
                            is_deleted = true;
                        }
                    }
                } else {
                    if (is_mfm_a1_sync()) {
                        ++mfm_sync_count_;
                    } else if (mfm_sync_count_ >= 3) {
                        if (byte == ibm_disc_format::k_data_mark_data_pattern) {
                            found_data_mark = true;
                        } else if (byte == ibm_disc_format::k_deleted_data_mark_data_pattern) {
                            found_data_mark = true;
                            is_deleted = true;
                        }
                        mfm_sync_count_ = 0;
                    } else {
                        mfm_sync_count_ = 0;
                    }
                }

                if (found_data_mark) {
                    if (is_deleted) {
                        status_ |= STATUS_RECORD_TYPE;
                    }
                    data_crc_ = ibm_disc_format::crc_init(is_double_density());
                    data_crc_ = ibm_disc_format::crc_add_byte(data_crc_,
                        is_deleted ? ibm_disc_format::k_deleted_data_mark_data_pattern
                                   : ibm_disc_format::k_data_mark_data_pattern);
                    byte_counter_ = 0;
                    phase_ = Phase::ReadingSectorData;
                } else if (search_counter_ > 50) {
                    // Data mark not found after ID -- continue searching for next ID
                    phase_ = Phase::SearchingForIdRead;
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::ReadingSectorData: {
                if (drq_) {
                    // Host hasn't read previous byte yet -- wait.
                    // If we've delivered all bytes and DRQ was just cleared,
                    // complete the command (INTRQ fires separately from last DRQ).
                    byte_delay_ = byte_period();
                    return;
                }

                // Check if the previous byte was the last one. Complete the
                // command NOW (after DRQ was cleared by the host reading the
                // last byte). This ensures INTRQ fires as a separate NMI edge
                // from the last DRQ, matching real WD1770 behaviour where
                // command completion is signalled after the host acknowledges
                // the final byte.
                if (byte_counter_ >= data_size_) {
                    if (current_command_ & FLAG_MULTI_SECTOR) {
                        ++sector_;
                        index_count_ = 0;
                        phase_ = Phase::SearchingForIdRead;
                    } else {
                        complete_command();
                    }
                    byte_delay_ = byte_period();
                    break;
                }

                uint8_t byte = read_byte_from_disc();
                data_crc_ = ibm_disc_format::crc_add_byte(data_crc_, byte);
                data_ = byte;
                drq_ = true;
                ++byte_counter_;

                byte_delay_ = byte_period();
                break;
            }

            default: break;
        }
    }

    // =========================================================================
    // Type II: Write Sector (pulse-driven)
    // =========================================================================

    void start_write_sector() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }
        if (drive->is_write_protected()) {
            status_ |= STATUS_WRITE_PROT;
            complete_command();
            return;
        }
        index_count_ = 0;
        mfm_sync_count_ = 0;
        write_last_mfm_bit_ = false;
        phase_ = Phase::SearchingForIdWrite;
        byte_delay_ = byte_period();
    }

    void tick_write_sector_phase() {
        if (index_count_ >= MAX_INDEX_REVOLUTIONS) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        switch (phase_) {
            case Phase::SearchingForIdWrite: {
                uint8_t byte = read_byte_from_disc();
                if (!is_double_density()) {
                    if (is_fm_address_mark() && byte == ibm_disc_format::k_id_mark_data_pattern) {
                        id_crc_ = ibm_disc_format::crc_init(false);
                        id_crc_ = ibm_disc_format::crc_add_byte(id_crc_, byte);
                        id_byte_count_ = 0;
                        phase_ = Phase::ReadingIdFieldWrite;
                    }
                } else {
                    if (is_mfm_a1_sync()) {
                        ++mfm_sync_count_;
                    } else if (mfm_sync_count_ >= 3 && byte == ibm_disc_format::k_id_mark_data_pattern) {
                        id_crc_ = ibm_disc_format::crc_init(true);
                        id_crc_ = ibm_disc_format::crc_add_byte(id_crc_, byte);
                        id_byte_count_ = 0;
                        phase_ = Phase::ReadingIdFieldWrite;
                        mfm_sync_count_ = 0;
                    } else {
                        mfm_sync_count_ = 0;
                    }
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::ReadingIdFieldWrite: {
                uint8_t byte = read_byte_from_disc();
                id_field_buf_[id_byte_count_++] = byte;
                if (id_byte_count_ <= 4) {
                    id_crc_ = ibm_disc_format::crc_add_byte(id_crc_, byte);
                }
                if (id_byte_count_ == 6) {
                    uint16_t received_crc = (static_cast<uint16_t>(id_field_buf_[4]) << 8) |
                                             id_field_buf_[5];
                    id_crc_ok_ = (id_crc_ == received_crc);
                    phase_ = Phase::CheckingIdForWrite;
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::CheckingIdForWrite: {
                uint8_t id_track = id_field_buf_[0];
                uint8_t id_sector = id_field_buf_[2];

                if (id_track == track_ && id_sector == sector_ && id_crc_ok_) {
                    data_size_ = 128u << id_field_buf_[3];
                    byte_counter_ = 0;
                    // Write gap2 + sync + data mark, then request data bytes
                    gap_counter_ = 0;
                    phase_ = Phase::WritingGap2;
                } else {
                    phase_ = Phase::SearchingForIdWrite;
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::WritingGap2: {
                // Write gap bytes between ID field and data field
                if (!is_double_density()) {
                    if (gap_counter_ < ibm_disc_format::k_std_gap2_FFs) {
                        write_fm_byte_to_disc(0xFF);
                    } else if (gap_counter_ < ibm_disc_format::k_std_gap2_FFs + ibm_disc_format::k_std_sync_00s) {
                        write_fm_byte_to_disc(0x00);
                    } else {
                        phase_ = Phase::WritingDataMark;
                    }
                } else {
                    if (gap_counter_ < 22) {
                        write_mfm_byte_to_disc(0x4E);
                    } else if (gap_counter_ < 34) {
                        write_mfm_byte_to_disc(0x00);
                    } else {
                        // Write 3xA1 sync (handled in WritingDataMark)
                        phase_ = Phase::WritingDataMark;
                    }
                }
                ++gap_counter_;
                byte_delay_ = byte_period();
                break;
            }

            case Phase::WritingDataMark: {
                // Write the data address mark
                data_crc_ = ibm_disc_format::crc_init(is_double_density());
                uint8_t mark = (current_command_ & 0x01)
                    ? ibm_disc_format::k_deleted_data_mark_data_pattern
                    : ibm_disc_format::k_data_mark_data_pattern;

                if (!is_double_density()) {
                    write_fm_byte_to_disc(mark, ibm_disc_format::k_mark_clock_pattern);
                } else {
                    // 3xA1 sync then mark
                    // For simplicity, write the mark directly
                    // (A1 sync writes would need raw pulse access)
                    write_mfm_byte_to_disc(mark);
                }
                data_crc_ = ibm_disc_format::crc_add_byte(data_crc_, mark);
                byte_counter_ = 0;
                drq_ = true;  // Request first data byte from host
                phase_ = Phase::WritingSectorData;
                byte_delay_ = byte_period();
                break;
            }

            case Phase::WritingSectorData: {
                if (drq_) {
                    // Host hasn't written the byte yet -- wait
                    byte_delay_ = byte_period();
                    return;
                }
                // Write the byte the host provided
                uint8_t byte = data_;
                data_crc_ = ibm_disc_format::crc_add_byte(data_crc_, byte);

                if (!is_double_density()) {
                    write_fm_byte_to_disc(byte);
                } else {
                    write_mfm_byte_to_disc(byte);
                }

                ++byte_counter_;
                if (byte_counter_ >= data_size_) {
                    // Write CRC
                    gap_counter_ = 0;
                    phase_ = Phase::WritingCrc;
                } else {
                    drq_ = true;  // Request next byte
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::WritingCrc: {
                uint8_t crc_byte = (gap_counter_ == 0)
                    ? static_cast<uint8_t>(data_crc_ >> 8)
                    : static_cast<uint8_t>(data_crc_ & 0xFF);

                if (!is_double_density()) {
                    write_fm_byte_to_disc(crc_byte);
                } else {
                    write_mfm_byte_to_disc(crc_byte);
                }

                ++gap_counter_;
                if (gap_counter_ >= 2) {
                    if (current_command_ & FLAG_MULTI_SECTOR) {
                        ++sector_;
                        index_count_ = 0;
                        phase_ = Phase::SearchingForIdWrite;
                    } else {
                        complete_command();
                    }
                }
                byte_delay_ = byte_period();
                break;
            }

            default: break;
        }
    }

    // =========================================================================
    // Type III: Read Address (pulse-driven)
    // =========================================================================

    void start_read_address() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }
        index_count_ = 0;
        mfm_sync_count_ = 0;
        phase_ = Phase::SearchingForIdReadAddr;
        byte_delay_ = byte_period();
    }

    void tick_read_address_phase() {
        if (index_count_ >= MAX_INDEX_REVOLUTIONS) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }

        switch (phase_) {
            case Phase::SearchingForIdReadAddr: {
                uint8_t byte = read_byte_from_disc();
                if (!is_double_density()) {
                    if (is_fm_address_mark() && byte == ibm_disc_format::k_id_mark_data_pattern) {
                        id_byte_count_ = 0;
                        phase_ = Phase::ReadingIdFieldReadAddr;
                    }
                } else {
                    if (is_mfm_a1_sync()) {
                        ++mfm_sync_count_;
                    } else if (mfm_sync_count_ >= 3 && byte == ibm_disc_format::k_id_mark_data_pattern) {
                        id_byte_count_ = 0;
                        phase_ = Phase::ReadingIdFieldReadAddr;
                        mfm_sync_count_ = 0;
                    } else {
                        mfm_sync_count_ = 0;
                    }
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::ReadingIdFieldReadAddr: {
                uint8_t byte = read_byte_from_disc();
                id_field_buf_[id_byte_count_++] = byte;
                if (id_byte_count_ == 6) {
                    // Update sector register with the sector from the ID field
                    sector_ = id_field_buf_[2];
                    id_byte_count_ = 0;
                    data_ = id_field_buf_[0];
                    drq_ = true;
                    phase_ = Phase::TransferringIdField;
                }
                byte_delay_ = byte_period();
                break;
            }

            case Phase::TransferringIdField: {
                if (drq_) {
                    byte_delay_ = byte_period();
                    return;
                }
                ++id_byte_count_;
                if (id_byte_count_ >= 6) {
                    complete_command();
                } else {
                    data_ = id_field_buf_[id_byte_count_];
                    drq_ = true;
                }
                byte_delay_ = byte_period();
                break;
            }

            default: break;
        }
    }

    // =========================================================================
    // Type III: Read Track (pulse-driven)
    // =========================================================================

    void start_read_track() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }
        index_count_ = 0;
        // Wait for index pulse to start reading
        reading_track_started_ = false;
        phase_ = Phase::ReadingTrackData;
        byte_delay_ = byte_period();
    }

    void tick_read_track_phase() {
        if (!reading_track_started_) {
            // Read bytes until we see an index pulse
            uint8_t byte = read_byte_from_disc();
            if (index_count_ > 0) {
                // Index pulse seen, start capturing
                reading_track_started_ = true;
                index_count_ = 0;  // Reset so we can detect the end
                data_ = byte;
                drq_ = true;
            }
            byte_delay_ = byte_period();
            return;
        }

        if (drq_) {
            byte_delay_ = byte_period();
            return;
        }

        uint8_t byte = read_byte_from_disc();
        if (index_count_ > 0) {
            // Second index pulse = end of track
            complete_command();
            return;
        }
        data_ = byte;
        drq_ = true;
        byte_delay_ = byte_period();
    }

    // =========================================================================
    // Type III: Write Track (pulse-driven)
    // =========================================================================

    void start_write_track() {
        DiscDrive* drive = get_current_drive();
        if (!drive || !drive->has_disc()) {
            status_ |= STATUS_RNF;
            complete_command();
            return;
        }
        if (drive->is_write_protected()) {
            status_ |= STATUS_WRITE_PROT;
            complete_command();
            return;
        }
        index_count_ = 0;
        writing_track_started_ = false;
        write_last_mfm_bit_ = false;
        drq_ = true;  // Request first byte
        phase_ = Phase::WritingTrackData;
        byte_delay_ = byte_period();
    }

    void tick_write_track_phase() {
        if (!writing_track_started_) {
            // Wait for index pulse to start writing
            read_byte_from_disc();  // Advance disc, check for index
            if (index_count_ > 0) {
                writing_track_started_ = true;
                index_count_ = 0;
            }
            byte_delay_ = byte_period();
            return;
        }

        if (drq_) {
            // Host hasn't written byte yet
            byte_delay_ = byte_period();
            return;
        }

        // Write the byte the host provided
        uint8_t byte = data_;

        // In Write Track, the WD1770 interprets certain byte values specially
        // to generate sync patterns and address marks. This is the format command.
        if (!is_double_density()) {
            // FM Write Track special bytes:
            // 0xF5 -> missing clock address mark (clock=0xC7, data=0xFE or per context)
            // 0xF6 -> missing clock (not standard, but some formatters use it)
            // 0xF7 -> write 2 CRC bytes
            if (byte == 0xF7) {
                // Write CRC
                uint8_t crc_hi = static_cast<uint8_t>(data_crc_ >> 8);
                uint8_t crc_lo = static_cast<uint8_t>(data_crc_ & 0xFF);
                write_fm_byte_to_disc(crc_hi);
                write_fm_byte_to_disc(crc_lo);
            } else if (byte == 0xF5 || byte == 0xF6) {
                // Write address mark with missing clock
                // The actual data byte follows in the next write
                // For simplicity, write as a special clock pattern
                write_fm_byte_to_disc(byte, ibm_disc_format::k_mark_clock_pattern);
                data_crc_ = ibm_disc_format::crc_init(false);
                data_crc_ = ibm_disc_format::crc_add_byte(data_crc_, byte);
            } else {
                write_fm_byte_to_disc(byte);
                data_crc_ = ibm_disc_format::crc_add_byte(data_crc_, byte);
            }
        } else {
            // MFM Write Track special bytes:
            // 0xF5 -> A1 sync with missing clock
            // 0xF6 -> C2 sync with missing clock
            // 0xF7 -> write 2 CRC bytes
            if (byte == 0xF7) {
                uint8_t crc_hi = static_cast<uint8_t>(data_crc_ >> 8);
                uint8_t crc_lo = static_cast<uint8_t>(data_crc_ & 0xFF);
                write_mfm_byte_to_disc(crc_hi);
                write_mfm_byte_to_disc(crc_lo);
            } else if (byte == 0xF5) {
                // A1 sync - write raw sync pattern and init CRC
                // (This should write the special 0x4489 pattern)
                write_mfm_byte_to_disc(0xA1);  // Simplified
                data_crc_ = ibm_disc_format::crc_init(true);
            } else if (byte == 0xF6) {
                write_mfm_byte_to_disc(0xC2);  // Simplified
            } else {
                write_mfm_byte_to_disc(byte);
                data_crc_ = ibm_disc_format::crc_add_byte(data_crc_, byte);
            }
        }

        // Check for index pulse (end of track)
        if (index_count_ > 0) {
            complete_command();
            return;
        }

        drq_ = true;  // Request next byte
        byte_delay_ = byte_period();
    }

    // =========================================================================
    // Helpers
    // =========================================================================

    void complete_command() {
        status_ &= ~STATUS_BUSY;
        phase_ = Phase::Idle;
        intrq_ = true;
    }

    void update_track0_status() {
        DiscDrive* drive = get_current_drive();
        if (drive && drive->at_track_0()) {
            status_ |= STATUS_TRACK0;
        } else {
            status_ &= ~STATUS_TRACK0;
        }
    }

    DiscDrive* get_current_drive() {
        return drives_[selected_drive_];
    }

    // Record that a byte was just written to the disc, arming the write-back
    // inactivity timer (see tick()).
    void note_disc_write() {
        writes_pending_flush_ = true;
        write_idle_ticks_ = 0;
    }

    // Flush dirty tracks on every attached drive to their host images.
    void flush_dirty_drives(bool sync_to_disk) {
        for (DiscDrive* drive : drives_) {
            if (drive) drive->flush_if_dirty(sync_to_disk);
        }
    }

    void spin_up() {
        if (!motor_on_) {
            motor_on_ = true;
            status_ |= STATUS_MOTOR_ON;
            if (spin_up_delay_enabled_) spin_up_delay_ = SPIN_UP_DELAY_TICKS;
            if (auto* drive = get_current_drive()) drive->spin_up();
        }
        idle_ticks_ = 0;
    }

    void spin_down() {
        if (motor_on_) {
            motor_on_ = false;
            status_ &= ~STATUS_MOTOR_ON;
            if (auto* drive = get_current_drive()) drive->spin_down();
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    // Registers
    uint8_t status_ = 0x00;
    uint8_t track_ = 0x00;
    uint8_t sector_ = 0x01;
    uint8_t data_ = 0x00;
    uint8_t current_command_ = 0x00;

    // Phase state machine
    Phase phase_ = Phase::Idle;

    // Interrupt signals
    bool drq_ = false;
    bool intrq_ = false;

    // Drive connections
    std::array<DiscDrive*, 2> drives_{nullptr, nullptr};

    // External control
    uint8_t selected_side_ = 0;
    uint8_t selected_drive_ = 0;
    Density density_ = Density::SingleFM;

    // Type I state
    int step_direction_ = 1;
    int step_delay_ = 0;
    int selected_step_rate_ = 6000;

    // Byte timing
    int byte_delay_ = 0;

    // Motor
    bool motor_on_ = false;
    int idle_ticks_ = 0;
    bool spin_up_delay_enabled_ = true;
    int spin_up_delay_ = 0;

    // Write-back inactivity timer (independent of motor spin-down)
    bool writes_pending_flush_ = false;
    int write_idle_ticks_ = 0;

    // Index pulse counting
    int index_count_ = 0;

    // ID field reading state
    std::array<uint8_t, 6> id_field_buf_{};
    uint8_t id_byte_count_ = 0;
    uint16_t id_crc_ = 0;
    bool id_crc_ok_ = false;

    // Data transfer state
    uint32_t data_size_ = 256;
    uint32_t byte_counter_ = 0;
    uint32_t search_counter_ = 0;
    uint32_t gap_counter_ = 0;
    uint16_t data_crc_ = 0;

    // MFM sync detection
    int mfm_sync_count_ = 0;

    // FM clock tracking (for address mark detection)
    uint8_t last_fm_clocks_ = 0;

    // MFM raw tracking (for A1 sync detection)
    uint16_t last_mfm_raw_ = 0;

    // MFM write state
    bool write_last_mfm_bit_ = false;

    // Read/Write Track state
    bool reading_track_started_ = false;
    bool writing_track_started_ = false;
};

} // namespace beebium
