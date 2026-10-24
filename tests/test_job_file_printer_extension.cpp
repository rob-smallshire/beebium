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

// job-file-printer (#217): one file per print job, jobs split by quiet in
// emulated time (VIA ticks), file naming and collisions, shutdown, offline,
// the panel and the typed RPC.

#include <catch2/catch_test_macros.hpp>

#include "JobFilePrinterExtension.hpp"
#include "JobFilePrinterSink.hpp"
#ifdef BEEBIUM_BUILD_SERVICE
#include "JobFilePrinterDispatcher.hpp"
#include "job_file_printer.pb.h"
#endif

#include "test_temp_paths.hpp"

#include <beebium/UserViaPeripheral.hpp>
#include <beebium/extension/ExtensionContext.hpp>
#include <beebium/extension/PrinterPort.hpp>

#include "extension_ui.pb.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace beebium;
using beebium::test::ScopedTempDir;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

constexpr uint64_t IDLE_TICKS = 1000;

std::string read_file(const fs::path& filepath) {
    std::ifstream in(filepath, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The job files in `dirpath`, by name.
std::vector<std::string> job_filenames(const fs::path& dirpath) {
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dirpath)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

// Advance the port by `ticks` VIA ticks with /STROBE idle: emulated time
// passing with nothing printed.
void tick(UserViaPeripheral& peripheral, uint64_t ticks) {
    uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
    for (uint64_t i = 0; i < ticks; ++i) {
        peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    }
}

void strobe_byte(UserViaPeripheral& peripheral, uint8_t byte) {
    uint8_t ca1 = 1, ca2 = 1, cb1 = 1, cb2 = 1;
    peripheral.update_port_a(byte, 0xFF);
    ca2 = 0;
    peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    peripheral.update_control_lines(ca1, ca2, cb1, cb2);
    tick(peripheral, 12);
}

void strobe_string(UserViaPeripheral& peripheral, const std::string& text) {
    for (unsigned char c : text) {
        strobe_byte(peripheral, c);
    }
}

bool wait_until(const std::function<bool()>& condition) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(2ms);
    }
    return false;
}

bool wait_for_jobs(printer::JobFilePrinterSink& sink, uint64_t count) {
    return wait_until([&] { return sink.status().jobs_completed >= count; });
}

bool wait_for_bytes(printer::JobFilePrinterSink& sink, uint64_t count) {
    return wait_until([&] { return sink.status().total_bytes >= count; });
}

// 2026-10-24T15:30:45Z
std::chrono::system_clock::time_point stamp_time() {
    return std::chrono::system_clock::from_time_t(1792855845);
}

// A clock that moves on a second at every reading, from stamp_time().
std::function<std::chrono::system_clock::time_point()> stepping_clock() {
    auto readings = std::make_shared<int>(0);
    return [readings] { return stamp_time() + std::chrono::seconds((*readings)++); };
}

printer::JobFilePrinterSink::Options sink_options(const fs::path& dirpath) {
    printer::JobFilePrinterSink::Options options;
    options.dirpath = dirpath;
    options.idle_ticks = IDLE_TICKS;
    options.now = stepping_clock();
    return options;
}

#ifdef BEEBIUM_BUILD_SERVICE
class TestRpcContext final : public RpcContext {
public:
    bool is_cancelled() const override { return false; }
    const std::map<std::string, std::string>& metadata() const override { return metadata_; }
private:
    std::map<std::string, std::string> metadata_;
};
#endif

}  // namespace

TEST_CASE("Job files are named by the UTC second their job began", "[printer-port][job-file-printer]") {
    CHECK(printer::JobFilePrinterSink::job_filename(stamp_time()) == "job-20261024T153045Z.prn");
    CHECK(printer::JobFilePrinterSink::job_filename(stamp_time(), 2) ==
          "job-20261024T153045Z-2.prn");
}

TEST_CASE("Two bursts separated by more than idle are two job files",
          "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    printer::JobFilePrinterSink sink(sink_options(temp_dir.path()));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    strobe_string(peripheral, "FIRST\r");
    tick(peripheral, IDLE_TICKS + 10);
    strobe_string(peripheral, "SECOND\r");
    tick(peripheral, IDLE_TICKS + 10);

    REQUIRE(wait_for_jobs(sink, 2));
    CHECK(job_filenames(temp_dir.path()) ==
          std::vector<std::string>{"job-20261024T153045Z.prn", "job-20261024T153046Z.prn"});
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "FIRST\r");
    CHECK(read_file(temp_dir / "job-20261024T153046Z.prn") == "SECOND\r");
    const auto status = sink.status();
    CHECK(status.jobs_completed == 2);
    CHECK(status.total_bytes == 13);
    CHECK(status.current_job_filepath.empty());
    CHECK(status.bytes_in_job == 0);
}

TEST_CASE("A gap shorter than idle stays in one job", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    printer::JobFilePrinterSink sink(sink_options(temp_dir.path()));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    strobe_string(peripheral, "AB");
    tick(peripheral, IDLE_TICKS / 2);
    strobe_string(peripheral, "CD");

    REQUIRE(wait_for_bytes(sink, 4));
    {
        const auto status = sink.status();
        CHECK(status.jobs_completed == 0);
        CHECK(fs::path(status.current_job_filepath).filename() == "job-20261024T153045Z.prn");
        CHECK(status.bytes_in_job == 4);
    }

    tick(peripheral, IDLE_TICKS + 10);
    REQUIRE(wait_for_jobs(sink, 1));
    CHECK(job_filenames(temp_dir.path()) ==
          std::vector<std::string>{"job-20261024T153045Z.prn"});
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "ABCD");
}

TEST_CASE("A paused machine does not split a job", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    printer::JobFilePrinterSink sink(sink_options(temp_dir.path()));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    strobe_string(peripheral, "AB");
    REQUIRE(wait_for_bytes(sink, 2));
    // Paused: host time passes, many times the idle period, but the port is
    // not ticked, so no emulated time passes.
    std::this_thread::sleep_for(50ms);
    strobe_string(peripheral, "CD");
    tick(peripheral, IDLE_TICKS + 10);

    REQUIRE(wait_for_jobs(sink, 1));
    CHECK(job_filenames(temp_dir.path()) ==
          std::vector<std::string>{"job-20261024T153045Z.prn"});
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "ABCD");
}

TEST_CASE("Form feed is not a job boundary", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    printer::JobFilePrinterSink sink(sink_options(temp_dir.path()));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    strobe_string(peripheral, "PAGE 1\f");
    strobe_string(peripheral, "PAGE 2\f");
    tick(peripheral, IDLE_TICKS + 10);

    REQUIRE(wait_for_jobs(sink, 1));
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "PAGE 1\fPAGE 2\f");
}

TEST_CASE("Jobs begun in the same second take -2, -3 suffixes",
          "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    auto options = sink_options(temp_dir.path());
    options.now = [] { return stamp_time(); };
    printer::JobFilePrinterSink sink(options);
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    for (const char* text : {"ONE", "TWO", "THREE"}) {
        strobe_string(peripheral, text);
        tick(peripheral, IDLE_TICKS + 10);
    }

    REQUIRE(wait_for_jobs(sink, 3));
    CHECK(job_filenames(temp_dir.path()) ==
          std::vector<std::string>{"job-20261024T153045Z-2.prn", "job-20261024T153045Z-3.prn",
                                   "job-20261024T153045Z.prn"});
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "ONE");
    CHECK(read_file(temp_dir / "job-20261024T153045Z-2.prn") == "TWO");
    CHECK(read_file(temp_dir / "job-20261024T153045Z-3.prn") == "THREE");
}

TEST_CASE("Bytes are written to the job file as they arrive", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    printer::JobFilePrinterSink sink(sink_options(temp_dir.path()));
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    strobe_string(peripheral, "PART");
    REQUIRE(wait_for_bytes(sink, 4));
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "PART");
}

TEST_CASE("Shutting down closes the open job", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    JobFilePrinterExtension ext;
    ext.set_config({{"directory", temp_dir.path().string()}});
    ext.init(ctx);

    strobe_string(peripheral, "UNFINISHED\r");
    REQUIRE(wait_for_bytes(*ext.sink(), 11));
    CHECK(ext.sink()->status().jobs_completed == 0);

    ext.shutdown();
    CHECK(ext.sink() == nullptr);
    const auto names = job_filenames(temp_dir.path());
    REQUIRE(names.size() == 1);
    CHECK(read_file(temp_dir / names[0]) == "UNFINISHED\r");
}

TEST_CASE("Offline holds the byte until set online", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    auto options = sink_options(temp_dir.path());
    options.online = false;
    printer::JobFilePrinterSink sink(options);
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    CHECK_FALSE(sink.status().online);
    strobe_string(peripheral, "Z");
    // Offline time is not idle time: no job has begun.
    tick(peripheral, IDLE_TICKS + 10);
    CHECK(sink.receiver().bytes_received() == 0);
    CHECK(job_filenames(temp_dir.path()).empty());

    sink.set_online(true);
    CHECK(sink.status().online);
    tick(peripheral, 12);
    REQUIRE(wait_for_bytes(sink, 1));
    tick(peripheral, IDLE_TICKS + 10);
    REQUIRE(wait_for_jobs(sink, 1));
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "Z");
}

TEST_CASE("Line ends are translated within each job", "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    auto options = sink_options(temp_dir.path());
    options.line_ending = printer::LineEnding::Lf;
    printer::JobFilePrinterSink sink(options);
    UserViaPeripheral peripheral;
    peripheral.attach_printer_port_device(sink.device());

    strobe_string(peripheral, "A\rB\r\n");
    tick(peripheral, IDLE_TICKS + 10);
    REQUIRE(wait_for_jobs(sink, 1));
    CHECK(read_file(temp_dir / "job-20261024T153045Z.prn") == "A\nB\n");
}

TEST_CASE("JobFilePrinterExtension creates its directory and attaches to the printer port",
          "[printer-port][job-file-printer]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    const auto jobs_dirpath = temp_dir / "jobs" / "printer";

    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);

    JobFilePrinterExtension ext;
    ext.set_config({{"directory", jobs_dirpath.string()}, {"idle", "2"}});
    REQUIRE(ext.attaches_to().size() == 1);
    CHECK(ext.attaches_to()[0] == "printer-port");

    ext.init(ctx);
    REQUIRE(port.is_occupied());
    CHECK(fs::is_directory(jobs_dirpath));
    REQUIRE(ext.sink() != nullptr);
    CHECK(ext.sink()->options().idle_ticks == 2'000'000);
    CHECK(job_filenames(jobs_dirpath).empty());  // no job until the first byte
    ext.shutdown();
}

TEST_CASE("JobFilePrinterExtension requires a directory and checks its options",
          "[printer-port][job-file-printer]") {
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    ScopedTempDir temp_dir("beebium-job-file-printer-");

    SECTION("No directory") {
        JobFilePrinterExtension ext;
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
        CHECK_FALSE(port.is_occupied());
    }

    for (const char* idle : {"0", "-1", "1.5", "soon"}) {
        DYNAMIC_SECTION("idle=" << idle) {
            JobFilePrinterExtension ext;
            ext.set_config({{"directory", temp_dir.path().string()}, {"idle", idle}});
            REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
            CHECK_FALSE(port.is_occupied());
        }
    }

    SECTION("Unknown line ending") {
        JobFilePrinterExtension ext;
        ext.set_config({{"directory", temp_dir.path().string()}, {"line_ending", "cr"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("Buffer must be a positive integer") {
        JobFilePrinterExtension ext;
        ext.set_config({{"directory", temp_dir.path().string()}, {"buffer", "0"}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
    }

    SECTION("A directory that cannot be created") {
        const auto file_filepath = temp_dir / "a-file";
        std::ofstream(file_filepath) << "x";
        JobFilePrinterExtension ext;
        ext.set_config({{"directory", (file_filepath / "jobs").string()}});
        REQUIRE_THROWS_AS(ext.init(ctx), std::runtime_error);
        CHECK_FALSE(port.is_occupied());
    }
}

TEST_CASE("JobFilePrinterUi shows the directory, the job, the counts and an Online toggle",
          "[printer-port][job-file-printer][ui]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    JobFilePrinterExtension ext;
    REQUIRE(ext.ui() != nullptr);

    SECTION("Before init the panel still builds") {
        View view;
        ext.ui()->build_view(&view);
        REQUIRE(view.root().control_case() == Control::kGroup);
        REQUIRE(view.root().group().controls_size() == 4);
        CHECK(view.root().group().controls(0).label().text() == "(unset)");
    }

    SECTION("After init") {
        ext.set_config({{"directory", temp_dir.path().string()}});
        ext.init(ctx);

        View idle_view;
        ext.ui()->build_view(&idle_view);
        CHECK(idle_view.root().group().controls(1).label().text() == "No job printing");

        strobe_string(peripheral, "AB");
        REQUIRE(wait_for_bytes(*ext.sink(), 2));

        View view;
        ext.ui()->build_view(&view);
        const auto& group = view.root().group();
        REQUIRE(group.controls_size() == 4);
        CHECK(group.controls(0).id() == "directory");
        CHECK(group.controls(0).label().text() == temp_dir.path().string());
        CHECK(group.controls(1).id() == "job");
        const auto job_text = group.controls(1).label().text();
        CHECK(job_text.rfind("job-", 0) == 0);
        CHECK(job_text.find(".prn, 2 bytes") != std::string::npos);
        CHECK(group.controls(2).id() == "jobs");
        CHECK(group.controls(2).label().text() == "0 jobs completed, 2 bytes written");
        CHECK(group.controls(3).id() == "online");
        REQUIRE(group.controls(3).control_case() == Control::kToggle);
        CHECK(group.controls(3).toggle().label() == "Online");
        CHECK(group.controls(3).toggle().value());

        DispatchRequest request;
        request.set_control_id("online");
        request.set_bool_value(false);
        ext.ui()->handle_event(request);
        CHECK_FALSE(ext.sink()->status().online);

        View after;
        ext.ui()->build_view(&after);
        CHECK_FALSE(after.root().group().controls(3).toggle().value());
        ext.shutdown();
    }
}

#ifdef BEEBIUM_BUILD_SERVICE
TEST_CASE("JobFilePrinterDispatcher serves GetStatus and SetOnline",
          "[printer-port][job-file-printer][rpc]") {
    ScopedTempDir temp_dir("beebium-job-file-printer-");
    UserViaPeripheral peripheral;
    PrinterPort port(peripheral);
    ExtensionContext ctx(nullptr, nullptr, nullptr, nullptr, nullptr, &port);
    JobFilePrinterExtension ext;
    ext.set_config({{"directory", temp_dir.path().string()}});
    ext.init(ctx);

    auto dispatchers = ext.rpc_dispatchers();
    REQUIRE(dispatchers.size() == 1);
    auto* dispatcher = dispatchers[0];
    CHECK(dispatcher->service_name() == "JobFilePrinter");

    strobe_string(peripheral, "HI\r");
    REQUIRE(wait_for_bytes(*ext.sink(), 3));

    TestRpcContext rpc_ctx;
    std::string response;
    {
        JobFilePrinterGetStatusRequest req;
        auto status = dispatcher->invoke("GetStatus", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        JobFilePrinterStatus reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK(reply.directory() == temp_dir.path().string());
        CHECK(reply.online());
        CHECK(fs::path(reply.current_job_file()).parent_path() == temp_dir.path());
        CHECK(reply.bytes_in_job() == 3);
        CHECK(reply.jobs_completed() == 0);
        CHECK(reply.total_bytes() == 3);
    }
    {
        JobFilePrinterSetOnlineRequest req;
        req.set_online(false);
        auto status = dispatcher->invoke("SetOnline", req.SerializeAsString(), response, rpc_ctx);
        REQUIRE(status.is_ok());
        JobFilePrinterStatus reply;
        REQUIRE(reply.ParseFromString(response));
        CHECK_FALSE(reply.online());
        CHECK_FALSE(ext.sink()->status().online);
    }
    {
        auto status = dispatcher->invoke("Nope", "", response, rpc_ctx);
        CHECK_FALSE(status.is_ok());
    }
    ext.shutdown();
}
#endif
