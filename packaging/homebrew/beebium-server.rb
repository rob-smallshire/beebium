# Homebrew formula for the headless Beebium emulator servers.
#
# This is the canonical copy, kept in the monorepo for development, review and
# CI. It is mirrored into the tap (rob-smallshire/homebrew-beebium) at release
# time by packaging/homebrew/sync-tap.sh.
#
# The servers are built from source against Homebrew's own gRPC/protobuf/abseil.
# Since the ExtensionRpc channel landed, extension plugins no longer embed gRPC
# (they link only libbeebium_extension_api + the shared libprotobuf), so there
# is no duplicate-runtime hazard and no need to static-link gRPC the way the
# Linux bundle does.
class BeebiumServer < Formula
  desc "Headless BBC Micro emulator servers"
  homepage "https://github.com/rob-smallshire/beebium"
  url "https://github.com/rob-smallshire/beebium/archive/refs/tags/v0.1.0.tar.gz"
  sha256 "0000000000000000000000000000000000000000000000000000000000000000" # placeholder; pinned by sync-tap.sh
  license "GPL-3.0-or-later"
  head "https://github.com/rob-smallshire/beebium.git", branch: "master"

  depends_on "cmake" => :build
  depends_on "nlohmann-json" => :build
  depends_on "abseil"
  depends_on "c-ares"
  depends_on "grpc"
  depends_on "openssl@3"
  depends_on "protobuf"
  depends_on "re2"

  def install
    # Build only the server executables (and their extension plugins); the test
    # suite, Python/TS clients and the macOS app are out of scope for the
    # server package.
    system "cmake", "-S", ".", "-B", "build", *std_cmake_args,
           "-DBEEBIUM_BUILD_TESTS=OFF"
    system "cmake", "--build", "build", "--target", "beebium-servers"

    # Install the whole relocatable tree under libexec: one server binary per
    # machine variant (libexec/bin/beebium-model-*), the extension ABI dylibs
    # (libexec/lib), the dlopened plugins (libexec/bin/extensions/<name>) and
    # the ROMs, presets and bundled discs (libexec/share/beebium). The binaries
    # resolve all of these relative to their own on-disk location, so the tree
    # relocates intact.
    system "cmake", "--install", "build", "--prefix", libexec

    # Put every server on the user's PATH without dragging the rest of the tree
    # (notably bin/extensions) onto it. Every resource lookup (extensions, ROMs,
    # presets, bundled discs) is relative to the binary's real on-disk location,
    # resolved through this symlink, so they all find the libexec tree. The
    # test block exercises each lookup through the symlink.
    bin.install_symlink (libexec/"bin").glob("beebium-model-*")
  end

  test do
    # Every check runs through the bin symlink, as a real invocation does. The
    # servers find their resources relative to their own on-disk location, so
    # each lookup below only succeeds if the binary resolves the symlink to its
    # libexec target first; a lookup relative to bin/ itself finds nothing.

    # list-extensions loads the extension ABI dylib via RPATH and enumerates
    # both the built-in and the dlopened plugins.
    output = shell_output("#{bin}/beebium-model-b list-extensions")

    # The built-in extensions are compiled into the binary.
    assert_match "host-serial", output
    assert_match "aun", output
    # A representative dlopened plugin must be discovered from the keg.
    assert_match "scsi-hdd", output
    # The plugins must resolve out of the installed tree, not a build dir.
    assert_match (libexec/"bin/extensions").to_s, output

    # Every machine variant is on PATH and resolves its plugins out of the keg
    # through its own symlink.
    servers = bin.glob("beebium-model-*")
    refute_empty servers
    assert_includes servers.map { |s| s.basename.to_s }, "beebium-model-b"
    servers.each do |server|
      assert_predicate server, :symlink?
      assert_match (libexec/"bin/extensions").to_s,
                   shell_output("#{server} list-extensions")
    end

    # Preset discovery: the built-in presets live in libexec/share/beebium/
    # presets. A binary that anchors on the symlink's own directory lists
    # "(none)" here.
    presets = shell_output("#{bin}/beebium-model-b list-presets")
    assert_match(/^\s+model-b\s/, presets)
    assert_match "model-b-disc", presets

    # ROM discovery: a headless screenshot loads the MOS from libexec/share/
    # beebium/roms, runs briefly and exits on its own, so no server has to be
    # backgrounded. A binary that anchors on the symlink's own directory fails
    # with "Cannot find ROM directory" and produces no image.
    system bin/"beebium-model-b", "capture-screenshot",
           "--output", testpath/"shot.png", "--duration", "0.5"
    assert_path_exists testpath/"shot.png"
  end
end
