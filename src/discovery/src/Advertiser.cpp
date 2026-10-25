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

#include <beebium/discovery/Advertiser.hpp>

#include <cstdio>

namespace beebium::discovery {

namespace {

std::string quoted(const std::string& name) {
    return "\"" + name + "\"";
}

}  // namespace

void report_to_stderr(const std::string& line) {
    // One call, so lines from two advertisers do not interleave.
    std::fprintf(stderr, "%s\n", line.c_str());
}

bool Advertiser::start(const ServiceInfo& info) {
    withdraw();
    {
        std::lock_guard lock(mutex_);
        info_ = info;
        if (!registered_name_.empty()) {
            replacing_name_ = registered_name_;
        }
        registered_name_.clear();
    }
    const std::string reason = begin(info);
    if (reason.empty()) {
        return true;
    }
    registration_failed(reason);
    return false;
}

void Advertiser::stop() {
    // Once withdraw() returns no callback can run, so the name is final.
    withdraw();
    std::lock_guard lock(mutex_);
    if (!registered_name_.empty()) {
        report("mdns: no longer advertising " + quoted(registered_name_) + " as " +
               info_.service_type);
    }
    registered_name_.clear();
    replacing_name_.clear();
}

void Advertiser::set_reporter(AdvertiserReporter reporter) {
    std::lock_guard lock(mutex_);
    reporter_ = std::move(reporter);
}

void Advertiser::registered(const std::string& actual_name) {
    std::lock_guard lock(mutex_);
    // The name this registration replaces: one a start() withdrew, or one the
    // responder has just renamed after a clash.
    const std::string previous =
        !replacing_name_.empty() ? replacing_name_ : registered_name_;
    std::string line = "mdns: advertising " + quoted(actual_name) + " as " +
                       info_.service_type + " on port " + std::to_string(info_.port);
    if (!previous.empty() && previous != actual_name) {
        line += ", in place of " + quoted(previous);
    }
    registered_name_ = actual_name;
    replacing_name_.clear();
    report(line);
}

void Advertiser::registration_failed(const std::string& reason) {
    std::lock_guard lock(mutex_);
    // A registration a start() withdrew is not coming back.
    if (!replacing_name_.empty()) {
        report("mdns: no longer advertising " + quoted(replacing_name_) + " as " +
               info_.service_type);
    }
    registered_name_.clear();
    replacing_name_.clear();
    report("mdns: cannot advertise " + quoted(info_.instance_name) + " as " +
           info_.service_type + ": " + reason);
}

void Advertiser::report(const std::string& line) const {
    if (reporter_) {
        reporter_(line);
    }
}

}  // namespace beebium::discovery
