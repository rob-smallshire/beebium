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
#include "beebium/extension/AudioSourcePort.hpp"
#include "beebium/devices/Sn76489.hpp"
#include <grpcpp/grpcpp.h>
#include <algorithm>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
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
            // sequence is the produced index of this chunk's first sample
            // (delivered plus dropped so far), so a consumer that sees it jump
            // beyond the previous chunk's sample_count has detected dropped
            // samples. cycle_count is reserved and left zero.
            chunk.set_sequence(audio_buffer.last_read_index());
            chunk.set_sample_count(static_cast<uint32_t>(count));
            chunk.set_cycle_count(0);

            // Pack samples into bytes: each sample is N × 32-bit fields
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

// The proto encoding of a frame field.
inline SourceEncoding to_proto_encoding(AudioFieldEncoding encoding) {
    switch (encoding) {
        case AudioFieldEncoding::Signed4x8:   return ENCODING_4X8BIT_SIGNED;
        case AudioFieldEncoding::Signed2x16:  return ENCODING_2X16BIT_SIGNED;
        case AudioFieldEncoding::Signed1x32:  return ENCODING_1X32BIT_SIGNED;
        case AudioFieldEncoding::Unsigned4x8: return ENCODING_4X8BIT_UNSIGNED;
    }
    return ENCODING_SILENCE;
}

// Describe every frame field from the machine's audio source port: the
// SN76489's two fields, any extension's, and silence for the rest. Groups are
// numbered from 1 in order of first appearance; a source with no group name
// is ungrouped (0).
inline void describe_audio_format(const AudioSourcePort& port, uint32_t sample_rate,
                                  AudioFormat* response) {
    response->set_sample_rate(sample_rate);
    response->set_source_count(static_cast<uint32_t>(AudioSourcePort::kFieldCount));

    std::vector<std::string> group_names;
    for (std::size_t field = 0; field < AudioSourcePort::kFieldCount; ++field) {
        auto* source = response->add_sources();
        source->set_source_index(static_cast<uint32_t>(field));

        const AudioFieldInfo* info = port.field_info(field);
        if (!info) {
            source->set_encoding(ENCODING_SILENCE);
            source->set_group_id(0);
            continue;
        }

        source->set_source_name(info->name);
        source->set_encoding(to_proto_encoding(info->encoding));
        for (const auto& channel_name : info->channel_names) {
            source->add_channel_names(channel_name);
        }
        source->set_bipolar(info->bipolar);

        uint32_t group_id = 0;
        if (!info->group_name.empty()) {
            auto it = std::find(group_names.begin(), group_names.end(), info->group_name);
            if (it == group_names.end()) {
                group_names.push_back(info->group_name);
                auto* group = response->add_groups();
                group->set_group_id(static_cast<uint32_t>(group_names.size()));
                group->set_group_name(info->group_name);
                group->set_description(info->group_description);
                group->set_color(info->group_color);
                group_id = static_cast<uint32_t>(group_names.size());
            } else {
                group_id = static_cast<uint32_t>(it - group_names.begin()) + 1;
            }
        }
        source->set_group_id(group_id);
    }
}

template<typename MachineType>
grpc::Status AudioServiceImpl<MachineType>::GetAudioFormat(
    grpc::ServerContext* /*context*/,
    const GetAudioFormatRequest* /*request*/,
    AudioFormat* response) {

    if constexpr (HasAudioSourcePort<std::remove_reference_t<decltype(machine_.state().memory)>>) {
        describe_audio_format(machine_.state().memory.audio_source_port(), SAMPLE_RATE, response);
    } else {
        describe_audio_format(AudioSourcePort{}, SAMPLE_RATE, response);
    }
    return grpc::Status::OK;
}

} // namespace service
} // namespace beebium

#endif // BEEBIUM_SERVICE_AUDIO_SERVICE_HPP
