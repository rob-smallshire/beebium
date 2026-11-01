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

#ifndef BEEBIUM_LINUX_APP_LED_COLOUR_HPP
#define BEEBIUM_LINUX_APP_LED_COLOUR_HPP

#include <QColor>
#include <QString>

namespace beebium::linux_client {

// An LED's colour from its metadata (client specification 10), as the macOS
// client reads it: "#rrggbb" or "#rgb", "rgb(r g b)", "hsl(h s% l%)", or a
// wavelength such as "625nm". A wavelength is brought to the lightness the
// macOS client tuned for its hue (compensating the Helmholtz-Kohlrausch
// effect), so LEDs of different colours look equally bright. Grey for what
// does not parse.
QColor led_colour(const QString& metadata);

// The ring drawn round an LED: its colour darkened by a fixed perceptual
// step (20 L in CIE LAB), the same for every hue.
QColor led_outline(const QColor& colour);

}  // namespace beebium::linux_client

#endif  // BEEBIUM_LINUX_APP_LED_COLOUR_HPP
