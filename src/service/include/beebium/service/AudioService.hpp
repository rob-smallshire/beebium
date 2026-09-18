// Copyright © 2025 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_AUDIO_SERVICE_HPP
#define BEEBIUM_SERVICE_AUDIO_SERVICE_HPP

#include "audio.grpc.pb.h"
#include "beebium/AudioBuffer.hpp"
#include "beebium/devices/Sn76489.hpp"
#include <grpcpp/grpcpp.h>
#include <thread>
#include <chrono>

namespace beebium {
namespace service {

/// gRPC service implementation for audio streaming and introspection
template<typename MachineType>
class AudioServiceImpl final : public AudioService::Service {
public:
    explicit AudioServiceImpl(MachineType& machine);
    ~AudioServiceImpl() override = default;

    // Non-copyable
    AudioServiceImpl(const AudioServiceImpl&) = delete;
    AudioServiceImpl& operator=(const AudioServiceImpl&) = delete;

    grpc::Status SubscribeAudio(
        grpc::ServerContext* context,
        const SubscribeAudioRequest* request,
        grpc::ServerWriter<AudioChunk>* writer) override;

    grpc::Status GetAudioFormat(
        grpc::ServerContext* context,
        const GetAudioFormatRequest* request,
        AudioFormat* response) override;

private:
    MachineType& machine_;

    static constexpr size_t DEFAULT_CHUNK_SIZE = 1024;
    static constexpr uint32_t SAMPLE_RATE = 48000;
};

// Template implementation

template<typename MachineType>
AudioServiceImpl<MachineType>::AudioServiceImpl(MachineType& machine)
    : machine_(machine) {
}

template<typename MachineType>
grpc::Status AudioServiceImpl<MachineType>::SubscribeAudio(
    grpc::ServerContext* context,
    const SubscribeAudioRequest* request,
    grpc::ServerWriter<AudioChunk>* writer) {

    size_t chunk_size = request->chunk_size() > 0 ? request->chunk_size() : DEFAULT_CHUNK_SIZE;
    std::vector<AudioSample> samples(chunk_size);
    uint64_t sequence = 0;

    while (!context->IsCancelled()) {
        // Check if audio buffer is available
        if (!machine_.state().memory.audio_buffer) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto& audio_buffer = machine_.state().memory.audio_buffer.value();

        if (audio_buffer.available() >= chunk_size) {
            size_t count = audio_buffer.read(samples.data(), chunk_size);

            AudioChunk chunk;
            chunk.set_sequence(sequence++);
            chunk.set_sample_count(static_cast<uint32_t>(count));
            chunk.set_cycle_count(0);  // Reserved for future use

            // Pack samples into bytes: each sample is N × 32-bit fields
            // For now, just source 0 (SN76489) is active
            std::string sample_data;
            sample_data.reserve(count * AudioSample::MAX_SOURCES * sizeof(uint32_t));

            for (size_t i = 0; i < count; ++i) {
                for (size_t j = 0; j < AudioSample::MAX_SOURCES; ++j) {
                    uint32_t value = samples[i].sources[j];
                    // Pack as little-endian bytes
                    sample_data.push_back(static_cast<char>(value & 0xFF));
                    sample_data.push_back(static_cast<char>((value >> 8) & 0xFF));
                    sample_data.push_back(static_cast<char>((value >> 16) & 0xFF));
                    sample_data.push_back(static_cast<char>((value >> 24) & 0xFF));
                }
            }

            chunk.set_samples(std::move(sample_data));

            if (!writer->Write(chunk)) {
                // Client disconnected
                break;
            }
        } else {
            // Not enough samples, wait briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    return grpc::Status::OK;
}

template<typename MachineType>
grpc::Status AudioServiceImpl<MachineType>::GetAudioFormat(
    grpc::ServerContext* /*context*/,
    const GetAudioFormatRequest* /*request*/,
    AudioFormat* response) {

    response->set_sample_rate(SAMPLE_RATE);
    response->set_source_count(AudioSample::MAX_SOURCES);

    // Define channel group for internal sound
    auto* internal_group = response->add_groups();
    internal_group->set_group_id(1);
    internal_group->set_group_name("Internal Sound");
    internal_group->set_description("BBC Micro built-in SN76489 sound chip");
    internal_group->set_color("#FF6B35");  // Orange

    // SN76489 occupies two source fields, each two signed 16-bit channels.
    // pack_2x16bit stores the first (left) channel in the low half and the
    // second (right) in the high half. Channel names use MOS SOUND numbering.
    // Field 0 = (tone0, tone1).
    auto* sn_lo = response->add_sources();
    sn_lo->set_source_index(0);
    sn_lo->set_source_name("SN76489");
    sn_lo->set_encoding(ENCODING_2X16BIT_SIGNED);
    sn_lo->add_channel_names("1");  // Tone0 = MOS channel 1
    sn_lo->add_channel_names("2");  // Tone1 = MOS channel 2
    sn_lo->set_group_id(1);

    // Field 1 = (tone2, noise).
    auto* sn_hi = response->add_sources();
    sn_hi->set_source_index(1);
    sn_hi->set_source_name("SN76489");
    sn_hi->set_encoding(ENCODING_2X16BIT_SIGNED);
    sn_hi->add_channel_names("3");  // Tone2 = MOS channel 3
    sn_hi->add_channel_names("0");  // Noise = MOS channel 0
    sn_hi->set_group_id(1);

    // Reserved sources (silent for now)
    for (size_t i = 2; i < AudioSample::MAX_SOURCES; ++i) {
        auto* reserved = response->add_sources();
        reserved->set_source_index(static_cast<uint32_t>(i));
        reserved->set_source_name("Reserved");
        reserved->set_encoding(ENCODING_SILENCE);
        reserved->set_group_id(0);  // Ungrouped
    }

    return grpc::Status::OK;
}

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_AUDIO_SERVICE_HPP
