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


#include "FilePrinterUi.hpp"

#include "FilePrinterExtension.hpp"

#include "extension_ui.pb.h"

#include <string>

namespace beebium {

namespace {

constexpr const char* CONTROL_PATH   = "path";
constexpr const char* CONTROL_BYTES  = "bytes";

}  // namespace

void FilePrinterUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();
    // No group label: the sidebar already names the extension ("File Printer").

    // build_view runs on the gRPC SubscribeView thread; the counters are
    // written by the emulation and sink threads. Read one snapshot.
    printer::FilePrinterSink::Status status;
    if (auto* sink = ext_.sink()) {
        status = sink->status();
    }

    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_PATH);
        control->mutable_label()->set_text(status.path.empty() ? std::string("(unset)")
                                                               : status.path);
    }
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_BYTES);
        control->mutable_label()->set_text(std::to_string(status.bytes_received) +
                                           " bytes received, " +
                                           std::to_string(status.bytes_written) + " written");
    }
}

void FilePrinterUi::handle_event(const DispatchRequest& /*request*/) {
    // The panel has no controls.
}

}  // namespace beebium
