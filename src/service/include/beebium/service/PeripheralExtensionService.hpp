// Copyright © 2026 Robert Smallshire <robert@smallshire.org.uk>
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

#ifndef BEEBIUM_SERVICE_PERIPHERAL_EXTENSION_SERVICE_HPP
#define BEEBIUM_SERVICE_PERIPHERAL_EXTENSION_SERVICE_HPP

#include "peripheral_extension.grpc.pb.h"
#include "beebium/extension/AttachmentPointCatalogue.hpp"
#include "beebium/extension/ExtensionArgParser.hpp"
#include "beebium/extension/ExtensionRegistry.hpp"
#include "beebium/extension/ExtensionStorage.hpp"

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace beebium::service {

// gRPC service for discovering loaded peripheral extensions.
// This is a core service (always present), not templated on MachineType.
// It queries the ExtensionRegistry to enumerate loaded extensions, and gives
// with them everything a frontend shows about them: the attachment points by
// name, where each extension is attached, and each parameter's full schema.
class PeripheralExtensionServiceImpl final : public PeripheralExtensionService::Service {
public:
    // The value an extension uses for a parameter when none is given, on this
    // host (describe-extension's effective_default). Unset: the default.
    using EffectiveDefault = std::function<std::string(const ParameterSchema&)>;

    // Runs a function with emulation paused, serialised with every other
    // caller that pauses it (Machine::with_emulation_paused).
    using Quiescer = std::function<void(const std::function<void()>&)>;

    explicit PeripheralExtensionServiceImpl(const ExtensionRegistry& registry,
                                            EffectiveDefault effective_default = {})
        : registry_(registry), effective_default_(std::move(effective_default)) {}

    // Read storage devices with emulation paused, so a device the server
    // re-points (a Save moving the machine's media) is never read half
    // changed. Unset: read as they are.
    void set_quiescer(Quiescer quiescer) { quiescer_ = std::move(quiescer); }

    grpc::Status ListExtensions(
            grpc::ServerContext*,
            const ListExtensionsRequest*,
            ListExtensionsResponse* response) override {
        const auto extensions = registry_.extensions();
        for (std::size_t i = 0; i < extensions.size(); ++i) {
            auto* ext = extensions[i];
            auto* info = response->add_extensions();
            info->set_name(std::string(ext->name()));
            info->set_id(std::string(ext->id()));
            info->set_label(std::string(ext->label()));
            info->set_display_name(ext->manifest().display_name);
            info->set_description(std::string(ext->description()));
            info->set_has_ui(ext->ui() != nullptr);

            // Storage devices: extensions opt in by implementing
            // ExtensionStorage and overriding Extension::storage()
            // to return `this`. Empty for the typical extension.
            if (auto* storage = ext->storage()) {
                std::vector<StorageDeviceInfo> devices;
                const auto read_devices = [&] { devices = storage->devices(); };
                if (quiescer_) {
                    quiescer_(read_devices);
                } else {
                    read_devices();
                }
                for (const auto& dev : devices) {
                    auto* proto_dev = info->add_storage_devices();
                    proto_dev->set_id(dev.id);
                    proto_dev->set_name(dev.name);
                    proto_dev->set_kind(
                        dev.kind == StorageDeviceInfo::Kind::Removable
                            ? StorageDevice::REMOVABLE
                            : StorageDevice::FIXED);
                    proto_dev->set_media_type(dev.media_type);
                    proto_dev->set_backing_path(dev.backing_path);
                    proto_dev->set_activity_indicator_name(
                        dev.activity_indicator_name);
                }
            }

            for (auto point : ext->attaches_to()) {
                info->add_attaches_to(std::string(point));
                auto* attachment = info->add_attachments();
                attachment->set_point(std::string(point));
                attachment->set_provider_id(provider_id(extensions, i, point));
            }
            for (auto provided : ext->provides()) {
                info->add_provides(std::string(provided));
            }

            // Current instance config
            for (const auto& [key, value] : ext->config()) {
                (*info->mutable_config())[key] = value;
            }

            // Parameter schema from manifest
            for (const auto& param : ext->manifest().parameters) {
                fill_parameter(*info->add_parameters(), param);
            }
            for (const auto& group : ext->manifest().groups) {
                auto* g = info->add_groups();
                g->set_key(group.key);
                g->set_label(group.label);
                g->set_description(group.description);
                for (const auto& key : group.parameters) g->add_parameters(key);
                if (group.none) {
                    g->mutable_none()->set_label(group.none->label);
                    g->mutable_none()->set_description(group.none->description);
                }
            }
        }

        // The attachment points: the machine's own and those extensions
        // provide, in the catalogue's order, any it does not know last.
        std::vector<std::string> points(registry_.extension_points().begin(),
                                        registry_.extension_points().end());
        for (auto* ext : extensions) {
            for (auto provided : ext->provides()) {
                if (std::find(points.begin(), points.end(), provided) == points.end()) {
                    points.emplace_back(provided);
                }
            }
        }
        const auto& catalogue = attachment_point_catalogue();
        auto rank = [&](const std::string& id) {
            auto it = std::find_if(catalogue.begin(), catalogue.end(),
                                   [&](const AttachmentPoint& p) { return p.id == id; });
            return static_cast<std::size_t>(it - catalogue.begin());
        };
        std::stable_sort(points.begin(), points.end(),
                         [&](const std::string& a, const std::string& b) {
                             return rank(a) < rank(b);
                         });
        for (const auto& id : points) {
            auto* point = response->add_attachment_points();
            point->set_id(id);
            point->set_display_name(std::string(attachment_point_display_name(id)));
            if (const auto* catalogued = find_attachment_point(id)) {
                point->set_description(catalogued->description);
                point->set_min_occupancy(catalogued->min_occupancy);
                if (catalogued->max_occupancy) {
                    point->set_max_occupancy(*catalogued->max_occupancy);
                }
            }
            for (auto* ext : extensions) {
                const auto provided = ext->provides();
                if (std::find(provided.begin(), provided.end(), id) != provided.end()) {
                    point->add_provided_by(std::string(ext->id()));
                }
            }
        }
        return grpc::Status::OK;
    }

private:
    // The id of the instance whose `point` extensions[index] is attached to,
    // as ExtensionContext chose it at init: the one its provider_of parameter
    // names, else the last provider initialised before it; empty for the
    // machine's own point.
    static std::string provider_id(std::span<PeripheralExtension* const> extensions,
                                   std::size_t index, std::string_view point) {
        const auto* ext = extensions[index];
        for (const auto& param : ext->manifest().parameters) {
            if (param.provider_of != point) continue;
            const auto& config = ext->config();
            auto it = config.find(param.key);
            const std::string wanted = it != config.end() ? it->second : param.default_value;
            if (!wanted.empty()) return wanted;
            break;
        }
        for (std::size_t j = index; j-- > 0;) {
            const auto provided = extensions[j]->provides();
            if (std::find(provided.begin(), provided.end(), point) != provided.end()) {
                return std::string(extensions[j]->id());
            }
        }
        return {};
    }

    void fill_parameter(ParameterSchemaInfo& p, const ParameterSchema& param) const {
        p.set_key(param.key);
        p.set_type(param.type);
        p.set_description(param.description);
        p.set_position(param.position);
        p.set_required(param.required);
        p.set_default_value(param.default_value);
        p.set_label(param.label);
        p.set_is_list(param.is_list);
        p.set_default_from(param.default_from);
        p.set_effective_default(effective_default_ ? effective_default_(param)
                                                   : param.default_value);
        if (param.min) p.set_min(*param.min);
        if (param.max) p.set_max(*param.max);
        for (const auto& c : param.choices) fill_choice(*p.add_choices(), c);
        p.set_choices_from(param.choices_from);
        for (const auto& key : param.excludes) p.add_excludes(key);
        p.set_bundled(param.bundled);
        p.set_unique(param.unique);
        p.set_provider_of(param.provider_of);
        p.set_item_format(param.item_format);
        if (is_path_type(param.type)) p.set_access(param.access);
        for (const auto& field : param.fields) {
            auto* f = p.add_fields();
            f->set_key(field.key);
            f->set_label(field.label);
            f->set_description(field.description);
            f->set_type(field.type);
            f->set_required(field.required);
            f->set_default_value(field.default_value);
            if (field.min) f->set_min(*field.min);
            if (field.max) f->set_max(*field.max);
            for (const auto& c : field.choices) fill_choice(*f->add_choices(), c);
        }
    }

    static void fill_choice(ParameterChoiceInfo& out, const ParameterChoice& c) {
        out.set_value(c.value);
        out.set_label(c.label);
        out.set_description(c.description);
    }

    const ExtensionRegistry& registry_;
    EffectiveDefault effective_default_;
    Quiescer quiescer_;
};

}  // namespace beebium::service

#endif  // BEEBIUM_SERVICE_PERIPHERAL_EXTENSION_SERVICE_HPP
