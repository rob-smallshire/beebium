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
// test_a_defaults.cpp, checks every switch default first
// version 2.0.0, 20 November 2026.

// Runs first, by its file name: a default outside its limits, such as time.refresh_period 0,
// hangs AesTiming in the first board test, so a later test would never get to report it.

#include "../AesOptions.hpp"
#include "ArmTest.hpp"

#include <cstdlib>

// Beebium does not check an edited default (SRC, ExtensionArgParser.cpp: only the manifest's
// three settings reach the plugin), so this is the only check on one.
TEST(options_defaults_are_in_range, "Every row's built-in default passes back through set_option, so an edit outside a row's limits or choices fails here (USER, 1 Nov 2026; review finding 2)") {
    const AesOptions plain;
    long refused = 0;
    for (const OptionRow& row : option_table()) {
        AesOptions o;
        if (set_option(o, row.key, row.get(plain))) continue;
        printf("  %s: the default %s is refused\n", row.key.c_str(), row.get(plain).c_str());
        refused++;
    }
    CHECK_EQ(refused, 0);
    if (refused) exit(1);  // The board tests after this would hang, not fail.
}
