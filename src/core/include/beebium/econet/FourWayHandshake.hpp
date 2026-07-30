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

#include "NetworkBackend.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

namespace beebium {

// Bridges AUN's two-way protocol (data + ack) to the four-way handshake
// (scout + scout-ack + data + final-ack) that Econet NFS ROMs expect.
//
// Implements NetworkBackend as a decorator: the ADLC talks to this class
// using raw Econet frames, and this class translates to/from typed AUN
// packets on the wrapped backend.  Scout phases are generated locally
// using timeouts; only data and ack packets cross the real network.
//
// Timer tick() must be called once per 2MHz rising edge, before the ADLC
// tick, so that timeout-generated frames are available when the ADLC's
// byte trickle calls receive_frame().
class FourWayHandshake : public NetworkBackend {
public:

    // Handshake states
    enum class Stage : uint8_t {
        Idle,               // No transaction
        ScoutSent,          // TX: scout held, waiting for timeout to fake ack
        ScoutAckReceived,   // TX: fake scout ack delivered, waiting for data from Beeb
        DataSent,           // TX: AUN Unicast sent, waiting for remote AUN Ack
        WaitForIdle,        // Transaction finishing, draining buffers
        ScoutReceived,      // RX: scout delivered to Beeb, waiting for Beeb's ack
        ScoutAckSent,       // RX: Beeb's ack swallowed, waiting for timeout to deliver data
        DataReceived,       // RX: data delivered to Beeb, waiting for Beeb's final ack
        ImmediateSent,      // TX: immediate sent, waiting for reply from remote
        ImmediateReceived,  // RX: immediate reply delivered, waiting for Beeb's response
    };

    // Handshake timeouts (in ticks — one per 2MHz rising edge)
    static constexpr int SCOUT_ACK_TIMEOUT = 5000;     // ~2.5ms — delay before faking scout ack
    static constexpr int FINAL_ACK_TIMEOUT = 10000;    // ~5ms — delay before faking final ack
    static constexpr int WATCHDOG_TIMEOUT  = 500000;    // ~250ms — force-reset hung transactions
    static constexpr int FLAG_FILL_TIMEOUT = 500000;    // ~250ms — assume peer finished
    static constexpr int IDLE_COOLDOWN     = 5000;      // ~2.5ms — gap between handshakes

    // How long a frame that did not fit the current stage may wait to be
    // re-offered before it is discarded. Generous next to a handshake (which
    // the watchdog abandons after ~250ms) but far short of the multi-second
    // timeouts NFS applies to an OSWORD, so a held frame is either redelivered
    // while it still means something or dropped before it can surface wildly
    // out of context.
    static constexpr int HELD_FRAME_TTL = 2000000;      // ~1s

    // Upper bound on frames held at once. A peer that floods us -- broken,
    // hostile, or merely very busy -- must not be able to grow our memory
    // without bound. Sized well above any legitimate burst: a handshake
    // involves a handful of frames, not dozens.
    static constexpr int MAX_HELD_FRAMES = 32;

    // Econet frame byte offsets
    static constexpr int FRAME_DEST_STN = 0;
    static constexpr int FRAME_DEST_NET = 1;
    static constexpr int FRAME_SRC_STN  = 2;
    static constexpr int FRAME_SRC_NET  = 3;
    static constexpr int FRAME_CONTROL  = 4;
    static constexpr int FRAME_PORT     = 5;
    static constexpr int FRAME_DATA     = 6;

    // Econet control byte values — classify port-0 operations.
    // Control bytes 0x02-0x05 carry extra scout payload and use the
    // full four-way handshake (Unicast). Other port-0 control bytes
    // are Immediate operations.
    static constexpr uint8_t CTRL_POKE     = 0x02;
    static constexpr uint8_t CTRL_JSR      = 0x03;
    static constexpr uint8_t CTRL_USERPROC = 0x04;
    static constexpr uint8_t CTRL_OSPROC   = 0x05;

    // Control byte encoding: bit 7 distinguishes inbound scouts from
    // outbound in Econet framing. The lower 7 bits carry the function.
    static constexpr uint8_t CTRL_FUNCTION_MASK = 0x7F;
    static constexpr uint8_t CTRL_HIGH_BIT      = 0x80;

    explicit FourWayHandshake(NetworkBackend& backend)
        : backend_(backend) {}

    // --- NetworkBackend interface ---

    // Called by the ADLC when a complete frame has been assembled.
    // The frame is a RawFrame with data containing Econet frame bytes.
    void send_frame(const NetworkFrame& frame) override {
        ++tx_frames_from_beeb_count_;
        // Record stage at each send_frame call (circular buffer of last 8)
        if (send_stage_log_count_ < 8) {
            send_stage_log_[send_stage_log_count_++] = stage_;
        }
        const auto& data = frame.data;
        switch (stage_) {
            case Stage::Idle:
                handle_tx_from_idle(data);
                break;
            case Stage::ScoutAckReceived:
                handle_tx_data_after_scout_ack(data);
                break;
            case Stage::ScoutReceived:
                handle_tx_scout_ack_from_beeb(data);
                break;
            case Stage::DataReceived:
                handle_tx_final_ack_from_beeb(data);
                break;
            case Stage::ImmediateReceived:
                handle_tx_immediate_reply(data);
                break;
            default:
                // Unexpected TX in this state -- reset
                ++unexpected_tx_reset_count_;
                reset_handshake();
                break;
        }
    }

    // Called by the ADLC's byte trickle to fetch the next frame for RX delivery.
    // Returns a RawFrame with Econet bytes, or nullopt if nothing available.
    std::optional<NetworkFrame> receive_frame() override {
        // 1. Return queued frames first (from timeouts or handshake processing)
        if (!rx_queue_.empty()) {
            auto frame = std::move(rx_queue_.front());
            rx_queue_.pop_front();
            return frame;
        }

        // 2. Don't poll backend during idle cooldown. On real Econet, there is
        // a natural gap between handshakes (wire speed, remote processing time).
        // AUN-over-UDP delivers replies instantly; the cooldown gives the
        // foreground code time to process the previous result and set up the
        // next receive control block before a new scout arrives.
        //
        // TODO: Replace this blunt cooldown with proper decoupling. The
        // FourWayHandshake should buffer incoming AUN packets in a queue with
        // arrival timestamps, and only present them to the ADLC after a
        // minimum inter-frame gap has elapsed — modelling wire-speed delivery
        // rather than blocking all backend polling.
        if (idle_cooldown_ > 0) return std::nullopt;

        // 3. Re-offer anything held from earlier, oldest first, before taking
        // anything new off the backend. Held frames arrived first and must
        // keep their place in the order.
        if (replay_held_frames() && !rx_queue_.empty()) {
            auto frame = std::move(rx_queue_.front());
            rx_queue_.pop_front();
            return frame;
        }

        // 4. Check backend for incoming AUN packets
        auto packet = backend_.receive_frame();
        if (!packet) return std::nullopt;

        // 5. Route through handshake state machine. A packet the current
        // stage cannot use is held rather than destroyed -- it has already
        // been taken off the socket, so dropping it here loses it for good.
        if (!handle_incoming(*packet)) {
            hold_frame(std::move(*packet));
            return std::nullopt;
        }

        // 6. Return first generated frame
        if (!rx_queue_.empty()) {
            auto frame = std::move(rx_queue_.front());
            rx_queue_.pop_front();
            return frame;
        }

        return std::nullopt;
    }

    bool is_connected() const override {
        return backend_.is_connected();
    }

    bool is_receiving_flags() const override {
        if (flag_fill_active_) return true;
        // Simulate clock box: continuous flag fill when idle and connected.
        // On real Econet, the clock box transmits 0x7E flag bytes between frames.
        // In AUN mode, a bound socket implies a functioning network with a clock box.
        return stage_ == Stage::Idle && backend_.is_connected();
    }

    bool is_expecting_frame() const override {
        // During an RX handshake, the gap between scout delivery and data
        // delivery is bridged by flag fill on real Econet. The ADLC should
        // not report INACTIVE during this gap. ScoutAckSent is the state
        // where the Beeb has acknowledged the scout and we're waiting for
        // the timer to deliver the data frame.
        return stage_ == Stage::ScoutAckSent;
    }

    // --- Timer tick (called once per 2MHz rising edge) ---

    void tick() {
        if (handshake_timer_ > max_handshake_timer_seen_) {
            max_handshake_timer_seen_ = handshake_timer_;
        }
        if (handshake_timer_ > 0) {
            ++ticks_with_timer_active_;
            if (--handshake_timer_ == 0) {
                on_handshake_timeout();
            }
        }

        if (watchdog_timer_ > 0) {
            if (--watchdog_timer_ == 0) {
                on_watchdog_timeout();
            }
        }

        if (flag_fill_timer_ > 0) {
            if (--flag_fill_timer_ == 0) {
                flag_fill_active_ = false;
            }
        }

        age_held_frames();

        // WaitForIdle → Idle when all queued frames have been consumed.
        // Start a cooldown to prevent the next incoming AUN packet from
        // arriving before the foreground code has processed the completion.
        if (stage_ == Stage::WaitForIdle && rx_queue_.empty()) {
            stage_ = Stage::Idle;
            idle_cooldown_ = IDLE_COOLDOWN;
        }

        if (idle_cooldown_ > 0) --idle_cooldown_;
    }

    // Reset all handshake state (called on system reset).
    void reset() {
        reset_handshake();
    }

    // --- State inspection ---

    Stage stage() const { return stage_; }
    bool flag_fill_active() const { return flag_fill_active_; }

    // Diagnostic: counts scout ack timer fires (fake ack enqueued)
    uint32_t scout_ack_generated_count() const { return scout_ack_generated_count_; }

    // Diagnostic: counts TX frames received from the Beeb (send_frame calls)
    uint32_t tx_frames_from_beeb_count() const { return tx_frames_from_beeb_count_; }

    // Diagnostic: counts handshake resets from unexpected TX in wrong state
    uint32_t unexpected_tx_reset_count() const { return unexpected_tx_reset_count_; }

    // Diagnostic: counts send_frame calls that reached handle_tx_from_idle
    uint32_t tx_from_idle_count() const { return tx_from_idle_count_; }

    // Diagnostic: highest handshake_timer_ value observed (should be 5000 if armed)
    int max_handshake_timer_seen() const { return max_handshake_timer_seen_; }
    uint32_t watchdog_timeout_count() const { return watchdog_timeout_count_; }
    uint64_t ticks_with_timer_active() const { return ticks_with_timer_active_; }

    // Diagnostic: stages at each send_frame call (up to 8)
    const Stage* send_stage_log() const { return send_stage_log_; }
    uint32_t send_stage_log_count() const { return send_stage_log_count_; }

    // --- Holding queue inspection ---
    //
    // Exposed so a test can assert on the mechanism rather than only on the
    // outcome: "the reply arrived" can pass by luck, "the reply arrived and
    // was redelivered from the holding queue" cannot.

    // Frames currently held awaiting a stage that can accept them.
    size_t held_frame_count() const { return held_frames_.size(); }

    // Frames that were held and later successfully redelivered.
    uint32_t held_frames_redelivered_count() const {
        return held_frames_redelivered_count_;
    }

    // Frames discarded because no stage accepted them within HELD_FRAME_TTL.
    uint32_t held_frames_expired_count() const {
        return held_frames_expired_count_;
    }

    // Frames discarded because the holding queue was full. Non-zero means a
    // peer is offering traffic faster than the handshake can consume it.
    uint32_t held_frames_dropped_count() const {
        return held_frames_dropped_count_;
    }

    // Number of extra scout payload bytes for a given control byte value.
    static int scout_payload_size(uint8_t ctrl) {
        switch (ctrl & CTRL_FUNCTION_MASK) {
            case CTRL_POKE: return 8;
            case CTRL_JSR: case CTRL_USERPROC: case CTRL_OSPROC: return 4;
            default: return 0;
        }
    }

private:

    // --- TX frame routing ---

    // First TX frame from Idle: classify as scout, broadcast, or immediate.
    void handle_tx_from_idle(const std::vector<uint8_t>& data) {
        ++tx_from_idle_count_;
        if (data.size() < 4) return;  // Malformed — too short for addressing

        uint8_t dest_stn = data[FRAME_DEST_STN];
        uint8_t dest_net = data[FRAME_DEST_NET];
        uint8_t src_stn  = data[FRAME_SRC_STN];
        uint8_t src_net  = data[FRAME_SRC_NET];

        // Broadcast: destination station 0xFF
        if (dest_stn == 0xFF) {
            NetworkFrame nf;
            nf.type = FrameType::Broadcast;
            nf.dest_stn = dest_stn;
            nf.dest_net = dest_net;
            nf.src_stn = src_stn;
            nf.src_net = src_net;
            if (data.size() >= 6) {
                nf.control_byte = data[FRAME_CONTROL] & CTRL_FUNCTION_MASK;
                nf.port = data[FRAME_PORT];
                if (data.size() > 6) {
                    nf.data.assign(data.begin() + FRAME_DATA, data.end());
                }
            }
            backend_.send_frame(nf);
            set_flag_fill();
            stage_ = Stage::WaitForIdle;
            return;
        }

        if (data.size() < 6) return;  // Too short for scout header

        uint8_t ctrl = data[FRAME_CONTROL];
        uint8_t port = data[FRAME_PORT];

        // Port 0 classification: control bytes 0x02-0x05 (POKE, JSR,
        // UserProc, OSProc) carry extra scout payload and use the full
        // four-way handshake — they are Unicast scouts, not Immediates.
        // All other port-0 operations (Machine Peek, Halt, etc.) are
        // Immediate. Matches BeebEm's classification.
        uint8_t masked_ctrl = ctrl & CTRL_FUNCTION_MASK;
        bool is_port0_unicast = (masked_ctrl >= CTRL_POKE && masked_ctrl <= CTRL_OSPROC);
        if (port == 0x00 && !is_port0_unicast) {
            NetworkFrame nf;
            nf.type = FrameType::Immediate;
            nf.port = 0;
            nf.control_byte = ctrl & CTRL_FUNCTION_MASK;
            nf.dest_stn = dest_stn;
            nf.dest_net = dest_net;
            nf.src_stn = src_stn;
            nf.src_net = src_net;
            if (data.size() > FRAME_DATA) {
                nf.data.assign(data.begin() + FRAME_DATA, data.end());
            }
            backend_.send_frame(nf);
            save_addressing(dest_stn, dest_net, src_stn, src_net, ctrl, port);
            arm_watchdog();
            stage_ = Stage::ImmediateSent;
            return;
        }

        // Unicast scout: save frame, arm timeout, do NOT send to network
        saved_scout_ = data;
        save_addressing(dest_stn, dest_net, src_stn, src_net, ctrl, port);
        arm_scout_timer();
        arm_watchdog();
        set_flag_fill();
        stage_ = Stage::ScoutSent;
    }

    // Beeb sends data frame after receiving (fake) scout ack.
    void handle_tx_data_after_scout_ack(const std::vector<uint8_t>& data) {
        NetworkFrame nf;
        nf.type = FrameType::Unicast;
        nf.port = saved_port_;
        nf.control_byte = saved_ctrl_ & CTRL_FUNCTION_MASK;
        nf.dest_stn = saved_dest_stn_;
        nf.dest_net = saved_dest_net_;
        nf.src_stn = saved_src_stn_;
        nf.src_net = saved_src_net_;

        // AUN payload = scout extra bytes + data frame payload (after 4 address bytes)
        int extra = scout_payload_size(saved_ctrl_);
        if (extra > 0 && saved_scout_.size() > static_cast<size_t>(FRAME_DATA)) {
            int available = std::min(extra,
                static_cast<int>(saved_scout_.size() - FRAME_DATA));
            nf.data.insert(nf.data.end(),
                saved_scout_.begin() + FRAME_DATA,
                saved_scout_.begin() + FRAME_DATA + available);
        }
        if (data.size() > 4) {
            nf.data.insert(nf.data.end(), data.begin() + 4, data.end());
        }

        backend_.send_frame(nf);
        saved_scout_.clear();
        arm_final_ack_timer();
        arm_watchdog();
        stage_ = Stage::DataSent;
    }

    // Beeb sends scout ack after receiving a locally-constructed scout.
    void handle_tx_scout_ack_from_beeb(const std::vector<uint8_t>& /*data*/) {
        // Don't send to network — the scout was generated locally
        arm_scout_timer();
        stage_ = Stage::ScoutAckSent;
    }

    // Beeb sends final ack after receiving data.
    void handle_tx_final_ack_from_beeb(const std::vector<uint8_t>& /*data*/) {
        NetworkFrame nf;
        nf.type = FrameType::Ack;
        nf.port = saved_port_;
        nf.control_byte = saved_ctrl_ & CTRL_FUNCTION_MASK;
        nf.dest_stn = saved_src_stn_;   // Ack goes TO the original source
        nf.dest_net = saved_src_net_;
        nf.src_stn = saved_dest_stn_;   // FROM us (we were the original destination)
        nf.src_net = saved_dest_net_;
        backend_.send_frame(nf);
        clear_flag_fill();
        watchdog_timer_ = 0;  // Cancel watchdog -- handshake completed normally
        stage_ = Stage::WaitForIdle;
    }

    // Beeb sends reply to an immediate operation we received.
    void handle_tx_immediate_reply(const std::vector<uint8_t>& data) {
        if (data.size() < 4) {
            reset_handshake();
            return;
        }
        NetworkFrame nf;
        nf.type = FrameType::ImmReply;
        nf.port = 0;
        nf.control_byte = saved_ctrl_ & CTRL_FUNCTION_MASK;
        nf.dest_stn = saved_src_stn_;   // Reply goes TO the requester
        nf.dest_net = saved_src_net_;
        nf.src_stn = saved_dest_stn_;   // FROM us
        nf.src_net = saved_dest_net_;
        if (data.size() > 4) {
            nf.data.assign(data.begin() + 4, data.end());
        }
        backend_.send_frame(nf);
        stage_ = Stage::WaitForIdle;
    }

    // --- RX packet routing ---

    // Route an incoming AUN packet based on current handshake stage.
    bool handle_incoming(const NetworkFrame& packet) {
        switch (stage_) {
            case Stage::Idle:
                return handle_rx_in_idle(packet);

            case Stage::WaitForIdle:
                // The previous transaction is finishing. If the rx_queue_ has
                // been fully consumed by the ADLC, accept new incoming packets
                // as if idle. This handles the common case where the remote
                // server's reply Unicast arrives between the ADLC consuming
                // the last frame from the queue and the next tick() advancing
                // the stage to Idle.
                if (rx_queue_.empty()) {
                    return handle_rx_in_idle(packet);
                }
                return false;

            case Stage::DataSent:
                if (packet.type == FrameType::Ack) {
                    // Remote acknowledged our data — generate fake final ack for Beeb
                    enqueue_rx_frame({
                        saved_src_stn_, saved_src_net_,     // dest of ack = us
                        saved_dest_stn_, saved_dest_net_    // src of ack = them
                    });
                    clear_flag_fill();
                    watchdog_timer_ = 0;  // Cancel watchdog -- handshake completing
                    stage_ = Stage::WaitForIdle;
                    return true;
                }
                return false;

            case Stage::ImmediateSent:
                if (packet.type == FrameType::ImmReply) {
                    // Construct Econet frame for the Beeb
                    std::vector<uint8_t> frame;
                    frame.push_back(saved_src_stn_);   // dest = us
                    frame.push_back(saved_src_net_);
                    frame.push_back(saved_dest_stn_);  // src = them
                    frame.push_back(saved_dest_net_);
                    frame.insert(frame.end(),
                        packet.data.begin(), packet.data.end());
                    enqueue_rx_frame(std::move(frame));
                    stage_ = Stage::WaitForIdle;
                    return true;
                }
                return false;

            default:
                return false;
        }
    }

    // Handle a received frame when the handshake is idle.
    bool handle_rx_in_idle(const NetworkFrame& packet) {
        switch (packet.type) {
            case FrameType::Unicast: {
                // Construct scout frame for the Beeb
                int extra = scout_payload_size(packet.control_byte);
                int available = std::min(extra, static_cast<int>(packet.data.size()));

                std::vector<uint8_t> scout;
                scout.push_back(packet.dest_stn);
                scout.push_back(packet.dest_net);
                scout.push_back(packet.src_stn);
                scout.push_back(packet.src_net);
                scout.push_back(packet.control_byte | CTRL_HIGH_BIT);
                scout.push_back(packet.port);
                if (available > 0) {
                    scout.insert(scout.end(),
                        packet.data.begin(), packet.data.begin() + available);
                }

                // Cache remaining data for after scout ack
                cached_rx_data_.clear();
                if (static_cast<int>(packet.data.size()) > extra) {
                    cached_rx_data_.assign(
                        packet.data.begin() + std::max(extra, 0),
                        packet.data.end());
                }

                save_addressing(packet.dest_stn, packet.dest_net,
                                packet.src_stn, packet.src_net,
                                packet.control_byte, packet.port);
                enqueue_rx_frame(std::move(scout));
                set_flag_fill();
                arm_watchdog();
                stage_ = Stage::ScoutReceived;
                return true;
            }

            case FrameType::Broadcast: {
                // Deliver broadcast directly as an Econet frame
                std::vector<uint8_t> frame;
                frame.push_back(packet.dest_stn);
                frame.push_back(packet.dest_net);
                frame.push_back(packet.src_stn);
                frame.push_back(packet.src_net);
                frame.push_back(packet.control_byte | CTRL_HIGH_BIT);
                frame.push_back(packet.port);
                frame.insert(frame.end(),
                    packet.data.begin(), packet.data.end());
                enqueue_rx_frame(std::move(frame));
                // Broadcasts are fire-and-forget — stay in Idle
                return true;
            }

            case FrameType::Immediate: {
                // Deliver immediate operation as an Econet frame
                std::vector<uint8_t> frame;
                frame.push_back(packet.dest_stn);
                frame.push_back(packet.dest_net);
                frame.push_back(packet.src_stn);
                frame.push_back(packet.src_net);
                frame.push_back(packet.control_byte | CTRL_HIGH_BIT);
                frame.push_back(packet.port);
                frame.insert(frame.end(),
                    packet.data.begin(), packet.data.end());
                save_addressing(packet.dest_stn, packet.dest_net,
                                packet.src_stn, packet.src_net,
                                packet.control_byte, packet.port);
                enqueue_rx_frame(std::move(frame));
                stage_ = Stage::ImmediateReceived;
                return true;
            }

            default:
                return false;
        }
    }

    // --- Timeout handlers ---

    void on_handshake_timeout() {
        switch (stage_) {
            case Stage::ScoutSent: {
                // Generate fake scout ack: swap src/dest from saved scout
                enqueue_rx_frame({
                    saved_src_stn_, saved_src_net_,
                    saved_dest_stn_, saved_dest_net_
                });
                ++scout_ack_generated_count_;
                clear_flag_fill();
                stage_ = Stage::ScoutAckReceived;
                break;
            }
            case Stage::ScoutAckSent: {
                // Deliver cached data as an Econet data frame
                std::vector<uint8_t> frame;
                frame.push_back(saved_dest_stn_);  // dest = us
                frame.push_back(saved_dest_net_);
                frame.push_back(saved_src_stn_);   // src = them
                frame.push_back(saved_src_net_);
                frame.insert(frame.end(),
                    cached_rx_data_.begin(), cached_rx_data_.end());
                cached_rx_data_.clear();
                enqueue_rx_frame(std::move(frame));
                stage_ = Stage::DataReceived;
                break;
            }
            case Stage::DataSent: {
                // Generate fake final ack when no real AUN Ack arrived.
                // On real Econet, the final ack comes within microseconds if
                // the scout succeeded. In AUN mode, the scout always succeeds
                // (locally faked), so we must also provide the final ack.
                // The NFS ROM has no software timeout for this phase — it
                // relies entirely on the ADLC NMI.
                // If a real AUN Ack arrived first (handle_incoming), the stage
                // already transitioned to WaitForIdle and this case won't fire.
                enqueue_rx_frame({
                    saved_src_stn_, saved_src_net_,     // dest of ack = us
                    saved_dest_stn_, saved_dest_net_    // src of ack = them
                });
                clear_flag_fill();
                watchdog_timer_ = 0;  // Cancel watchdog — handshake completing
                stage_ = Stage::WaitForIdle;
                break;
            }
            default:
                break;
        }
    }

    void on_watchdog_timeout() {
        ++watchdog_timeout_count_;
        reset_handshake();
    }

    // --- Helpers ---

    // --- Holding queue ---

    // Park a packet the current stage could not use.
    void hold_frame(NetworkFrame packet) {
        if (held_frames_.size() >= static_cast<size_t>(MAX_HELD_FRAMES)) {
            // Drop the oldest rather than refusing the newest: the oldest is
            // the one most likely to have outlived its usefulness, and a
            // fresh arrival is the more informative of the two.
            held_frames_.pop_front();
            ++held_frames_dropped_count_;
        }
        held_frames_.push_back(HeldFrame{std::move(packet), 0});
    }

    // Re-offer held frames to the state machine, oldest first. Returns true
    // when one was accepted. Stops at the first acceptance: a single accepted
    // packet can change the stage, and the rest must be re-evaluated against
    // the new one rather than against the stage that has just been left.
    bool replay_held_frames() {
        for (auto it = held_frames_.begin(); it != held_frames_.end(); ++it) {
            if (handle_incoming(it->packet)) {
                held_frames_.erase(it);
                ++held_frames_redelivered_count_;
                return true;
            }
        }
        return false;
    }

    // Age held frames and discard those that have waited too long.
    void age_held_frames() {
        for (auto it = held_frames_.begin(); it != held_frames_.end();) {
            if (++it->age_ticks > HELD_FRAME_TTL) {
                it = held_frames_.erase(it);
                ++held_frames_expired_count_;
            } else {
                ++it;
            }
        }
    }

    void enqueue_rx_frame(std::vector<uint8_t> data) {
        NetworkFrame frame;
        frame.type = FrameType::RawFrame;
        frame.data = std::move(data);
        rx_queue_.push_back(std::move(frame));
    }

    void save_addressing(uint8_t dest_stn, uint8_t dest_net,
                         uint8_t src_stn, uint8_t src_net,
                         uint8_t ctrl, uint8_t port) {
        saved_dest_stn_ = dest_stn;
        saved_dest_net_ = dest_net;
        saved_src_stn_ = src_stn;
        saved_src_net_ = src_net;
        saved_ctrl_ = ctrl;
        saved_port_ = port;
    }

    void arm_scout_timer() {
        handshake_timer_ = SCOUT_ACK_TIMEOUT;
    }
    void arm_final_ack_timer() { handshake_timer_ = FINAL_ACK_TIMEOUT; }
    void arm_watchdog() { watchdog_timer_ = WATCHDOG_TIMEOUT; }

    void set_flag_fill() {
        flag_fill_active_ = true;
        flag_fill_timer_ = FLAG_FILL_TIMEOUT;
    }

    void clear_flag_fill() {
        flag_fill_active_ = false;
        flag_fill_timer_ = 0;
    }

    void reset_handshake() {
        stage_ = Stage::Idle;
        handshake_timer_ = 0;
        watchdog_timer_ = 0;
        idle_cooldown_ = 0;
        clear_flag_fill();
        saved_scout_.clear();
        cached_rx_data_.clear();
        rx_queue_.clear();
        held_frames_.clear();
    }

    // --- State ---

    NetworkBackend& backend_;
    Stage stage_ = Stage::Idle;

    // Handshake timers (count ticks, decremented once per tick() call)
    int handshake_timer_ = 0;
    int watchdog_timer_ = 0;
    int flag_fill_timer_ = 0;
    int idle_cooldown_ = 0;
    bool flag_fill_active_ = false;

    // Diagnostic counters
    uint32_t scout_ack_generated_count_ = 0;
    uint32_t tx_frames_from_beeb_count_ = 0;
    uint32_t unexpected_tx_reset_count_ = 0;
    uint32_t tx_from_idle_count_ = 0;
    int max_handshake_timer_seen_ = 0;
    uint32_t watchdog_timeout_count_ = 0;
    Stage send_stage_log_[8] = {};
    uint32_t send_stage_log_count_ = 0;
    uint64_t ticks_with_timer_active_ = 0;

    // Saved state for the current handshake transaction
    std::vector<uint8_t> saved_scout_;       // TX path: saved scout frame
    std::vector<uint8_t> cached_rx_data_;    // RX path: cached AUN payload for data delivery
    std::deque<NetworkFrame> rx_queue_;       // Frames waiting for ADLC to receive

    // Packets that arrived while the handshake was in a stage that could not
    // use them, held for re-offering rather than destroyed. UDP does not
    // guarantee ordering and a peer may address us at any time, so this is
    // ordinary traffic, not an error case.
    struct HeldFrame {
        NetworkFrame packet;
        int age_ticks;
    };
    std::deque<HeldFrame> held_frames_;
    uint32_t held_frames_redelivered_count_ = 0;
    uint32_t held_frames_expired_count_ = 0;
    uint32_t held_frames_dropped_count_ = 0;

    uint8_t saved_dest_stn_ = 0;
    uint8_t saved_dest_net_ = 0;
    uint8_t saved_src_stn_ = 0;
    uint8_t saved_src_net_ = 0;
    uint8_t saved_ctrl_ = 0;
    uint8_t saved_port_ = 0;
};

}  // namespace beebium
