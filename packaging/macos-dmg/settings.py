# Copyright 2026 Robert Smallshire <robert@smallshire.org.uk>
#
# This file is part of Beebium.
#
# Beebium is free software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. Beebium is distributed in the hope that it will
# be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
# You should have received a copy of the GNU General Public License along with Beebium.
# If not, see <https://www.gnu.org/licenses/>.

# dmgbuild settings for the Beebium macOS app DMG.
#
# Read by `dmgbuild -s settings.py <volume-name> <output.dmg>` with these
# -D defines (all supplied by make-dmg.sh):
#   app         absolute path to Beebium.app
#   background  path to the background image (a multi-resolution TIFF)
#
# The Finder window layout -- geometry, icon size, the two icon slots and the
# background -- is fixed here so the DMG is generated headlessly and identically
# in the local preview loop and in CI (no Finder scripting, no .DS_Store
# template). The volume name (e.g. "Beebium 0.1.9") is the CLI positional; the
# DMG *file* name is chosen by make-dmg.sh.

import os.path

application = defines["app"]
appname = os.path.basename(application)

# --- What goes in the image -------------------------------------------------
format = "UDZO"                       # compressed, matches the old hdiutil path
files = [application]
symlinks = {"Applications": "/Applications"}

# --- Window and icons -------------------------------------------------------
# 660x420 pt content, icon size 128. Coordinates are points with the origin at
# the top-left, y increasing downward. The app sits in the left third and the
# Applications symlink in the right third, on a high baseline (y=100) so the
# icons sit on the sky of the desaturated artwork and leave the geometric solids
# revealed below. The background's arrow shares this baseline; keep the two in
# step (see gen-backgrounds.py SLOT_Y). Labels render just below the icons
# (~y=170).
background = defines["background"]
icon_size = 128
text_size = 13

window_rect = ((160, 160), (660, 420))
default_view = "icon-view"
include_icon_view_settings = True
include_list_view_settings = False
arrange_by = None
label_pos = "bottom"
show_icon_preview = False

icon_locations = {
    appname: (176, 100),
    "Applications": (484, 100),
}
