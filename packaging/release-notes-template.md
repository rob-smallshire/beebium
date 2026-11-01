## Get Beebium

The full BBC Micro emulator, ready to run. The app, the emulator servers, the ROMs, presets and extensions are all in one download.

### macOS

Download, open the DMG, drag **Beebium** into your Applications folder, and launch it.

- **Apple Silicon** (M1/M2/M3 and later): [Beebium-@@V@@-macos-arm64.dmg](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/Beebium-@@V@@-macos-arm64.dmg)
- **Intel**: [Beebium-@@V@@-macos-x86_64.dmg](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/Beebium-@@V@@-macos-x86_64.dmg)

Or install with Homebrew:

```
brew install --cask rob-smallshire/beebium/beebium-gui
```

Requires macOS 13 (Ventura) or newer.

### Linux

For Debian trixie and Raspberry Pi OS trixie (`amd64` or `arm64`): download the app and the servers for your architecture, then install both with apt, which fetches the app's Qt and gRPC libraries from the distribution.

- **amd64**: [beebium-client_@@V@@_amd64.deb](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-client_@@V@@_amd64.deb) and [beebium-server_@@V@@_amd64.deb](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server_@@V@@_amd64.deb)
- **arm64**: [beebium-client_@@V@@_arm64.deb](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-client_@@V@@_arm64.deb) and [beebium-server_@@V@@_arm64.deb](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server_@@V@@_arm64.deb)

```
sudo apt install ./beebium-client_@@V@@_arm64.deb ./beebium-server_@@V@@_arm64.deb
```

*A Windows application is planned. For now, Windows runs Beebium through the headless servers below.*

---

## Headless emulator servers

For developers and advanced users: drive Beebium from the **Python** or **TypeScript** client, from **CI**, from your own **gRPC** client - or from the **macOS application** above, which can connect over the network to a server running on another machine (it discovers them on your LAN automatically via mDNS/Bonjour). Each package puts every server (Model B, B+, B+ 128K, and Model B with the ROM/RAM, ATPL Sidewise, Watford ROM/RAM and Integra-B boards) on your `PATH`, with the ROMs and extensions bundled.

### Python — any platform

```
pip install beebium beebium-server
```

`beebium` is the client library; `beebium-server` ships the servers. Start here: the [Python client README](https://github.com/rob-smallshire/beebium/tree/v@@V@@/clients/beebium-python-client).

### Linux

Self-contained for `amd64` and `arm64` — no gRPC/protobuf to install.

- **Debian / Ubuntu / Raspberry Pi OS** — `sudo apt install ./<file>.deb`: [amd64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server_@@V@@_amd64.deb) · [arm64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server_@@V@@_arm64.deb)
- **Fedora / RHEL / openSUSE** — `sudo dnf install ./<file>.rpm`: [x86_64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-1.x86_64.rpm) · [aarch64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-1.aarch64.rpm)
- **Other distros** — extract anywhere and add `bin/` to your `PATH`: [amd64 .tar.gz](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-linux-amd64.tar.gz) · [arm64 .tar.gz](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-linux-arm64.tar.gz)

### macOS

```
brew install rob-smallshire/beebium/beebium-server
```

Or a self-contained tarball: [arm64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-macos-arm64.tar.gz) · [x86_64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-macos-x86_64.tar.gz).

### Windows

```
scoop bucket add beebium https://github.com/rob-smallshire/scoop-beebium
scoop install beebium-server
```

Or a self-contained zip: [x64](https://github.com/rob-smallshire/beebium/releases/download/v@@V@@/beebium-server-@@V@@-windows-x64.zip) (needs the Microsoft Visual C++ Redistributable).
