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

// Setting environment variables from a test, restored when the scope ends.

#pragma once

#include <beebium/PlatformUtils.hpp>

#include <cstdlib>
#include <optional>
#include <string>

namespace beebium::test {

// Sets an environment variable for the life of the scope, for the code and
// the commands it runs, and restores the previous value afterwards. An empty
// value unsets the variable.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const std::string& value) : name_(name) {
        previous_ = beebium::platform::get_env(name);
        set(value);
    }
    ~ScopedEnv() { set(previous_.value_or("")); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    void set(const std::string& value) {
#ifdef _WIN32
        _putenv_s(name_, value.c_str());
#else
        if (value.empty()) {
            ::unsetenv(name_);
        } else {
            ::setenv(name_, value.c_str(), 1);
        }
#endif
    }
    const char* name_;
    std::optional<std::string> previous_;
};

}  // namespace beebium::test
