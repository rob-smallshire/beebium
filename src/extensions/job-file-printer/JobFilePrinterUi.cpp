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

#include "JobFilePrinterUi.hpp"

#include "JobFilePrinterExtension.hpp"

#include "extension_ui.pb.h"

#include <filesystem>
#include <string>

namespace beebium {

namespace {

constexpr const char* CONTROL_DIRECTORY = "directory";
constexpr const char* CONTROL_JOB       = "job";
constexpr const char* CONTROL_JOBS      = "jobs";

}  // namespace

void JobFilePrinterUi::build_view(View* out) const {
    auto* root = out->mutable_root();
    root->set_id("root");
    auto* group = root->mutable_group();
    // No group label: the sidebar already names the extension ("Job File Printer").

    // build_view runs on the gRPC SubscribeView thread; the counters are
    // written by the emulation and sink threads. Read one snapshot.
    printer::JobFilePrinterSink::Status status;
    if (auto* sink = ext_.sink()) {
        status = sink->status();
    }

    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_DIRECTORY);
        control->mutable_label()->set_text(status.dirpath.empty() ? std::string("(unset)")
                                                                  : status.dirpath);
    }
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_JOB);
        control->mutable_label()->set_text(
            status.current_job_filepath.empty()
                ? std::string("No job printing")
                : std::filesystem::path(status.current_job_filepath).filename().string() +
                      ", " + std::to_string(status.bytes_in_job) + " bytes");
    }
    {
        auto* control = group->add_controls();
        control->set_id(CONTROL_JOBS);
        control->mutable_label()->set_text(std::to_string(status.jobs_completed) +
                                           (status.jobs_completed == 1 ? " job" : " jobs") +
                                           " completed, " + std::to_string(status.total_bytes) +
                                           " bytes written");
    }
}

void JobFilePrinterUi::handle_event(const DispatchRequest& /*request*/) {
    // The panel has no controls.
}

}  // namespace beebium
