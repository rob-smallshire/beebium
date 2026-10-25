# typed: strict
# frozen_string_literal: true

# Homebrew cask for the Beebium macOS GUI app.
#
# This is the canonical copy, kept in the monorepo for development, review and
# CI (brew audit/style run against it). It is mirrored into the tap
# (rob-smallshire/homebrew-beebium, Casks/beebium-gui.rb) at release time by
# packaging/homebrew/sync-gui-cask.sh, which pins the DMG url and its sha256 to
# a published release. It is arm64-only while Intel macOS is suspended until a
# self-hosted Intel runner exists (#223); restoring brings back the arch stanza
# and the per-architecture sha256 pair.
#
# The DMGs are Developer ID signed, notarized and stapled, so Gatekeeper accepts
# them on first launch. Unlike the beebium-server FORMULA (a source build of the
# headless servers), this CASK installs the prebuilt .app -- which embeds its own
# servers, ROMs, presets and extensions -- so the two do not overlap.
cask "beebium-gui" do
  version "0.1.0"
  # Placeholder; the real hash is pinned by sync-gui-cask.sh at release time.
  sha256 "0000000000000000000000000000000000000000000000000000000000000000"

  url "https://github.com/rob-smallshire/beebium/releases/download/v#{version}/Beebium-#{version}-macos-arm64.dmg"
  name "Beebium"
  desc "BBC Micro emulator with a graphical interface"
  homepage "https://github.com/rob-smallshire/beebium"

  auto_updates false
  # arm64 only while Intel macOS is suspended (#223).
  depends_on arch: :arm64
  # The app's deployment target is macOS 13 (Ventura) -- higher than the
  # beebium-server wheels' floor (Big Sur), because the SwiftUI/Metal front end
  # needs it. The bare symbol means "Ventura or newer".
  depends_on macos: :ventura

  app "Beebium.app"

  # The app writes its user data under Application Support/Beebium (keyboard
  # mappings, user presets); the rest are the standard AppKit locations for the
  # com.beebium.Beebium bundle id.
  zap trash: [
    "~/Library/Application Support/Beebium",
    "~/Library/Caches/com.beebium.Beebium",
    "~/Library/HTTPStorages/com.beebium.Beebium",
    "~/Library/Preferences/com.beebium.Beebium.plist",
    "~/Library/Saved Application State/com.beebium.Beebium.savedState",
  ]
end
