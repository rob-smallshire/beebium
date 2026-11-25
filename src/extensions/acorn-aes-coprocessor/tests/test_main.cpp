// Copyright 2026 James Sargent <acheton1984@letterboxes.org>
//
// This file is part of AESulator, a plugin for Beebium.
//
// AESulator is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. AESulator is distributed in the hope that it will
// be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
// You should have received a copy of the GNU General Public License along with AESulator.
// If not, see <https://www.gnu.org/licenses/>.

// AESulator: the Acorn ARM Evaluation System, a Beebium plugin.
// test_main.cpp, the test runner
// version 2.0.0, 20 November 2026.

#include "ArmTest.hpp"

std::vector<TestCase>& test_list() {
    static std::vector<TestCase> list;
    return list;
}
int g_checks = 0;
int g_failures = 0;
std::string g_skip_why;

int main() {
    int failed_tests = 0;
    int skipped_tests = 0;
    const bool strict = getenv("AES_REQUIRE_ALL") != nullptr;
    for (const TestCase& t : test_list()) {
        const int before = g_failures;
        g_skip_why.clear();
        t.run();
        if (g_failures != before) {
            failed_tests++;
            printf("FAILED: %s\n", t.name);
        } else if (!g_skip_why.empty() && strict) {
            failed_tests++;
            printf("FAILED: %s: skipped under AES_REQUIRE_ALL: %s\n", t.name, g_skip_why.c_str());
        } else if (!g_skip_why.empty()) {
            skipped_tests++;
            printf("SKIPPED: %s: %s\n", t.name, g_skip_why.c_str());
        }
    }
    printf("%zu tests, %d failed, %d skipped; %d checks, %d failed\n", test_list().size(), failed_tests,
           skipped_tests, g_checks, g_failures);
    return failed_tests == 0 ? 0 : 1;
}
