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

// The one server executable's dispatch (beebium/server/MultiCall.hpp): a model
// run by its name or by the first argument, seeing the argv it would see run
// by its own name, and SystemInfo's path the name invoked.

#include "test_env.hpp"
#include "test_temp_paths.hpp"

#include <beebium/PlatformUtils.hpp>
#include <beebium/server/MultiCall.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using beebium::server::dispatch;
using beebium::server::invoked_name;
using beebium::server::invoked_path;
using beebium::server::multi_call_main;
using beebium::server::ServerModel;

namespace {

// What the last model run saw.
std::string ran;
std::vector<std::string> ran_argv;

template <int N>
int record(int argc, char* argv[]) {
    ran = N == 0 ? "model-b" : "master-128";
    ran_argv.assign(argv, argv + argc);
    return 40 + N;
}

constexpr std::array<ServerModel, 2> kModels{{{"model-b", record<0>}, {"master-128", record<1>}}};

// An argv as main receives it.
struct Argv {
    explicit Argv(std::vector<std::string> arguments) : strings(std::move(arguments)) {
        for (auto& s : strings) pointers.push_back(s.data());
        pointers.push_back(nullptr);
    }
    int argc() const { return static_cast<int>(strings.size()); }
    char** argv() { return pointers.data(); }

    std::vector<std::string> strings;
    std::vector<char*> pointers;
};

int run(std::vector<std::string> arguments, std::string* out_text = nullptr, std::string* err_text = nullptr) {
    ran.clear();
    ran_argv.clear();
    Argv argv(std::move(arguments));
    std::ostringstream out;
    std::ostringstream err;
    const int code = multi_call_main(kModels, argv.argc(), argv.argv(), "9.9.9", out, err);
    if (out_text) *out_text = out.str();
    if (err_text) *err_text = err.str();
    return code;
}

}  // namespace

TEST_CASE("The invoked name is the last path component", "[multi_call]") {
    CHECK(invoked_name("beebium-model-b") == "beebium-model-b");
    CHECK(invoked_name("/opt/beebium/bin/beebium-master-128") == "beebium-master-128");
    CHECK(invoked_name("./beebium-server") == "beebium-server");
    CHECK(invoked_name("") == "");
#ifdef _WIN32
    CHECK(invoked_name("C:\\Beebium\\bin\\Beebium-Master-128.EXE") == "beebium-master-128");
    CHECK(invoked_name("beebium-model-b.exe") == "beebium-model-b");
#endif
}

TEST_CASE("A model invoked by its name runs with argv unchanged", "[multi_call]") {
    CHECK(run({"/opt/beebium/bin/beebium-master-128", "start", "--port", "0"}) == 41);
    CHECK(ran == "master-128");
    CHECK(ran_argv == std::vector<std::string>{"/opt/beebium/bin/beebium-master-128", "start", "--port", "0"});

    CHECK(run({"beebium-model-b"}) == 40);
    CHECK(ran_argv == std::vector<std::string>{"beebium-model-b"});
}

TEST_CASE("A model named by the first argument sees the argv its own name would give", "[multi_call]") {
    CHECK(run({"/opt/beebium/bin/beebium-server", "master-128", "start", "--port", "0"}) == 41);
    CHECK(ran == "master-128");
    CHECK(ran_argv == std::vector<std::string>{"/opt/beebium/bin/beebium-master-128", "start", "--port", "0"});

    SECTION("alone") {
        CHECK(run({"beebium-server", "model-b"}) == 40);
        CHECK(ran_argv == std::vector<std::string>{"beebium-model-b"});
    }
    SECTION("a .exe keeps its suffix") {
        CHECK(run({"bin/beebium-server.exe", "model-b", "list-presets"}) == 40);
        CHECK(ran_argv == std::vector<std::string>{"bin/beebium-model-b.exe", "list-presets"});
    }
    SECTION("from an executable of any other name") {
        CHECK(run({"/tmp/server-copy", "model-b", "--help"}) == 40);
        CHECK(ran_argv == std::vector<std::string>{"/tmp/beebium-model-b", "--help"});
    }
}

TEST_CASE("A name that is no model's leaves the first argument to name one", "[multi_call]") {
    CHECK(run({"beebium-model-q", "model-b", "start"}) == 40);
    CHECK(ran_argv == std::vector<std::string>{"beebium-model-b", "start"});
}

TEST_CASE("No model named is a usage error listing the models", "[multi_call]") {
    std::string err;
    SECTION("nothing named") {
        CHECK(run({"beebium-server"}, nullptr, &err) == beebium::server::ExitCode::USAGE);
        CHECK(err.find("name the machine model") != std::string::npos);
    }
    SECTION("a model there is not") {
        CHECK(run({"beebium-server", "model-z", "start"}, nullptr, &err) == beebium::server::ExitCode::USAGE);
        CHECK(err.find("no machine model 'model-z'") != std::string::npos);
    }
    SECTION("a subcommand in the model's place") {
        CHECK(run({"beebium-server", "start"}, nullptr, &err) == beebium::server::ExitCode::USAGE);
    }
    CHECK(ran.empty());
    CHECK(err.find("  model-b ") != std::string::npos);
    CHECK(err.find("  master-128 ") != std::string::npos);
}

TEST_CASE("beebium-server answers --help and --version itself", "[multi_call]") {
    std::string out;
    CHECK(run({"beebium-server", "--help"}, &out) == 0);
    CHECK(out.find("Usage: beebium-server <model>") != std::string::npos);
    CHECK(out.find("  model-b ") != std::string::npos);
    CHECK(out.find("BBC Master 128") != std::string::npos);
    CHECK(run({"beebium-server", "-h"}, &out) == 0);
    CHECK(run({"beebium-server", "help"}, &out) == 0);
    CHECK(run({"beebium-server", "--version"}, &out) == 0);
    CHECK(out == "beebium-server 9.9.9\n");
    CHECK(ran.empty());
}

TEST_CASE("The path invoked is argv[0] made absolute, else found on PATH", "[multi_call]") {
    beebium::test::ScopedTempDir dir("beebium_multi_call_");
    SECTION("a path with a directory, links not followed") {
        const auto path = invoked_path("bin/../bin/beebium-master-128");
        REQUIRE(path);
        CHECK(path->is_absolute());
        CHECK(*path == (std::filesystem::current_path() / "bin" / "beebium-master-128").lexically_normal());
    }
    SECTION("a bare name, on PATH") {
#ifdef _WIN32
        const std::string filename = "beebium-model-b.exe";
#else
        const std::string filename = "beebium-model-b";
#endif
        std::ofstream(dir.path() / filename) << "";
        beebium::test::ScopedEnv path_env("PATH", (dir.path() / "missing").string() +
#ifdef _WIN32
                                                      ";" +
#else
                                                      ":" +
#endif
                                                      dir.path().string());
        const auto path = invoked_path("beebium-model-b");
        REQUIRE(path);
        CHECK(std::filesystem::equivalent(*path, dir.path() / filename));
    }
    SECTION("a bare name on no PATH directory") {
        beebium::test::ScopedEnv path_env("PATH", dir.path().string());
        CHECK_FALSE(invoked_path("beebium-model-b"));
    }
}

TEST_CASE("SystemInfo's path is the name a model was invoked by", "[multi_call]") {
    beebium::platform::detail::invoked_executable_path_storage().reset();
    const auto own = beebium::platform::executable_path();
    run({"/opt/beebium/bin/beebium-server", "master-128"});
    // Named by argument, the executable is what was invoked: nothing is set.
    CHECK(beebium::platform::invoked_executable_path() == own);
    run({"/opt/beebium/bin/beebium-master-128", "start"});
    CHECK(beebium::platform::invoked_executable_path() ==
          std::filesystem::absolute("/opt/beebium/bin/beebium-master-128").lexically_normal());
}
