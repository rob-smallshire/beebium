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

#ifndef BEEBIUM_LINUX_TEXT_NATURAL_ORDER_HPP
#define BEEBIUM_LINUX_TEXT_NATURAL_ORDER_HPP

#include <QString>

namespace beebium::linux_client {

// Names as a person reads them, whatever the locale: letters without
// regard to case, a run of digits by its value (then the shorter run, so
// "2" before "02"), anything else by code point. Negative, zero or
// positive as a sorts before, with or after b. For lists the user reads
// by name, which must not change order with LANG.
int natural_compare(const QString& a, const QString& b);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_TEXT_NATURAL_ORDER_HPP
