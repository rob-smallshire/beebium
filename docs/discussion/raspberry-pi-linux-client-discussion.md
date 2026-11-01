# Raspberry Pi Linux Client

Beebium has a great macOS client app. I'm getting close to the time where I'll want to make a Linux client. I'm particularly interested in targeting the Raspberry Pi 4 and Pi 5, as they are poorly served by emulators, and I'm prepared to make special Pi builds that fit in well with the Pi Desktop and make effective use of the Pi processor and GPU. Still, the almost complete lack of UI standards on Linux is bothersome.

## Response:
31/10/2026, 21:02:08 · gpt-6

I think there's an opportunity here to make **the Raspberry Pi a first-class Beebium platform**, rather than treating Linux as an undifferentiated collection of distributions and desktop environments.

And I sympathise with your frustration. Linux has plenty of standards for the things an emulator needs underneath the UI, but remarkably little agreement about what an application should look and behave like.

The distinction I'd make is between **building a Linux client** and **building a Raspberry Pi desktop client**\. They overlap considerably, but they needn't be the same product.

## The Raspberry Pi is a particularly attractive target

The Pi 4 and Pi 5 are quite capable machines for BBC Micro emulation, especially given Beebium's separation of the emulation core from its graphical client.

Your existing architecture gives you a considerable advantage\:

- The C++20 emulation core and gRPC services can remain largely platform-independent.
- The Linux client can concentrate on presentation, input, audio, window management and local process orchestration.
- The Pi's GPU can handle display scaling, shader effects and potentially CRT simulation without burdening the emulation core.
- The Pi 5 in particular should have ample CPU capacity for sophisticated BBC Micro configurations, including second processors.

I'd resist the temptation to make the Pi client an entirely separate implementation, though. Ideally, the Pi-specific work should be concentrated in the rendering, input and packaging layers.

## The Linux UI dilemma

There are really three credible choices.

[![Form and Function – Allan Day's GNOME Blog](https://images.openai.com/static-rsc-4/ZQnqP4PM9p1BFYJgQbl-2VCthCU_mUj3uLsFLBsN5yMVvJ7IKQPmD9y10HqhkLOWIYSQRWZD9-JF5phU45h1lc6ti0onivK2fupb5rcjj0CZyq2vpJspOcu34OQ88gjRyHpI9xN6i0r0ycGxxxkhKdcU-vrP1UGmvzFlxet5nVbqfII8nsc2w1jIWQwI5ynx?purpose=fullsize)](https://blogs.gnome.org/aday/)

**GTK 4 / libadwaita**

The most natural choice for a GNOME-oriented Linux application. Good accessibility, modern controls and Wayland integration. But libadwaita brings fairly strong GNOME design opinions, which aren't necessarily what you want for Raspberry Pi OS.

---

[![Widgets style is not the same as my system's style | Qt Forum](https://images.openai.com/static-rsc-4/0S0uCjv7YKlF0G2QKmYP1ZTRSCfE7e_svyoLqO6oOayJSBiGkWiA_928gbg-fp9k9Z9jz30PakCWmhKtp8UACaFKk7R-LnM5UiO0WeWAZ1t2IYUQ3C-bfjNXq-Jm4-0RAT_ry90alghUeAbV0WAkm-QlvO-rZa6vBOco_YRdKQOtV4YrYbmkJXJx-8G84LUc?purpose=fullsize)](https://forum.qt.io/topic/131473/widgets-style-is-not-the-same-as-my-system-s-style)

**Qt 6 / Qt Widgets**

Probably my preferred option for Beebium. Mature menus, dialogs, keyboard handling, dockable panels and settings interfaces. Qt can work well across Linux desktops, including Raspberry Pi OS, without requiring you to adopt a particular desktop's design philosophy.

---

[![Bookworm — the new version of Raspberry Pi OS - Raspberry Pi](https://images.openai.com/static-rsc-4/urzzHWnw2LEcaDljtUf37Tir3XZD5T_EQmcMwaYsn4WkORAnHc-365OrjmcLDus6KkGUzIHWFpuLEFxn-rl8f6RhhxT0OJYuDUxZKiOdWAMTmOU3rAUHyh63U-PLSCFkhTADgb5aT6HJTBe6F5TBjYLWpLQaRjqqPQ6iiYhShRedRefkjlwC7MPJI2bndNiZ?purpose=fullsize)](https://www.raspberrypi.com/news/bookworm-the-new-version-of-raspberry-pi-os/)

**Raspberry Pi OS native integration**

Target the Raspberry Pi OS desktop specifically, using its conventions for menus, window decorations, file dialogs and packaging. This is more a distribution and integration strategy than a separate UI toolkit choice.

One important wrinkle is that Raspberry Pi OS has historically favoured GTK-based applications, but its desktop environment and compositor have evolved considerably. I wouldn't equate Raspberry Pi OS with GNOME or assume that libadwaita is automatically the best fit.

## Graphics: I would separate the UI toolkit from the emulator display

This is where your existing Metal-based macOS client offers a useful architectural model.

On Linux, I would seriously consider **Vulkan as the long-term graphics backend**, with OpenGL ES as a pragmatic alternative.

|  | Raspberry Pi 4 | Raspberry Pi 5 |
|--------|--------|--------|
| GPU | VideoCore VI | VideoCore VII |
| OpenGL ES | 3.1 | 3.1 |
| Vulkan | 1.0-class support | 1.2-class support |
| Display ambition | Accurate scaling and CRT effects | More elaborate shader pipelines |

Actual Vulkan capabilities depend on the Mesa driver and OS release.

The important design decision is that the graphics API shouldn't determine the UI toolkit. A Qt application can host a Vulkan or OpenGL rendering surface, just as your macOS client hosts Metal rendering.

I'd keep the BBC display renderer conceptually separate from the window chrome and debugger interfaces.

## A proposed Beebium Linux architecture

**Beebium Linux Client**

Qt 6 · menus · settings · debugger · keyboard mapping

**Display renderer**

Vulkan / OpenGL ES

**Platform services**

Audio · input · filesystem

gRPC

**libBeebium**

C++20 emulation core · host CPU · Tube · peripherals

One additional consideration: since you're willing to optimise specifically for the Pi, you could support a low-overhead, full-screen presentation mode, potentially using DRM/KMS directly. That could be appealing for dedicated retro-computing installations, although I would regard it as a later enhancement rather than the initial desktop implementation.

## Distribution matters almost as much as the UI

I'd suggest three levels of support, in priority order\:

1. **Raspberry Pi OS, 64-bit, Pi 4 and Pi 5.** Your reference platform, with thoroughly tested builds and desktop integration.
2. **Generic Linux ARM64 and x86-64.** A portable Qt client, built from substantially the same source, supporting common Wayland and X11 environments.
3. **Other distributions and packaging formats.** Flatpak, distro packages and perhaps AppImage, depending on demand.

For Raspberry Pi OS specifically, I'd favour a proper `.deb` package and APT repository over making users download an executable and figure out where to put it. It should install a desktop entry, icons, MIME associations and everything needed to launch Beebium from the application menu.

And, given Beebium's client/server architecture, I would preserve the option of connecting the Pi client to a Beebium server running on another machine. A Pi 4 could make an excellent dedicated BBC Micro terminal.

## One design decision I'd make early

Your macOS client has the benefit of Apple's Human Interface Guidelines and a coherent menu system. You've already developed concepts such as Immersive Mode and graphical keyboard mapping.

I would write a small **platform-independent Beebium UI specification**, describing behaviours rather than particular widgets\:

- Application menus and commands, including keyboard shortcuts.
- Windowed, full-screen and immersive presentation modes.
- Emulator lifecycle and machine configuration.
- Keyboard and controller mapping.
- Debugger presentation and interaction.
- File and disc image management.

Then implement that specification using native macOS conventions on the Mac and Qt conventions on Linux.

That way, the Linux client needn't look like a Mac application, but both clients will behave like Beebium.

My inclination would be **Qt 6 Widgets + Vulkan/OpenGL ES + Raspberry Pi OS as the reference Linux platform**\. Qt Quick/QML is worth considering if you want a more heavily customised interface, but for a traditional desktop emulator with menus, dialogs and debugger windows, Qt Widgets is an excellent fit.

There is one architectural question I'd be interested in exploring before committing to Qt, however: **how much of your existing Swift/macOS client is a thin platform-specific UI, and how much contains substantial application logic?** That distinction will determine whether Linux development is mainly a matter of implementing a new frontend or whether it's worth extracting a shared C++ client-side application layer first.

## Prompt:
31/10/2026, 21:06:51

Much of the front end is built from declarative information provided by Beebium servers, and then interpreted by the macOS client in macOS specific ways. I've pushed as much the platform indepedent details to the server as possible. I agree that a GUI client spec built from the capabilities of the macOS client, and then realised for Linux and later Windows independently, is the way to go.

## Response:
31/10/2026, 21:06:53 · gpt-6

That sounds like a very sound architectural foundation. In fact, I think you've already made the most important decision: **the server describes what Beebium can do, while each client decides how those capabilities should be presented on its platform.**

That is a stronger approach than trying to share UI implementation code between macOS, Linux and Windows.

I'd be inclined to formalise this as three distinct contracts\:

1. **Server capability protocol** — the declarative descriptions, state, operations and notifications exposed by Beebium servers. This is already substantially implemented.
2. **Client behavioural specification** — what the user can accomplish, how operations behave, what states and transitions exist, and which interactions must be supported. This would be extracted from the existing macOS client.
3. **Platform-specific presentation** — how those behaviours are realised using Swift/AppKit or SwiftUI on macOS, Qt on Linux, and potentially WinUI on Windows.

The second is the interesting missing piece.

## Specify behaviour, not appearance

For example, consider your Immersive Mode.

The specification shouldn't say anything about hiding an `NSWindow` title bar or invoking a particular Qt API. It should establish that\:

- The emulator display occupies the available client area without application chrome.
- The display maintains its intended aspect ratio.
- Unused space is filled with the configured background.
- The user has a reliable way to leave Immersive Mode.
- Immersive Mode is distinct from operating-system full-screen mode.

The macOS implementation might use AppKit window styles; Linux might use Qt window flags and compositor-specific behaviour; Windows might use WinUI or native Win32 APIs.

The same principle applies to keyboard mapping, disc management, machine selection and debugging.

## I'd also distinguish requirements from conventions

This is particularly important given your frustration with Linux desktop fragmentation.

| Aspect | Specification authority |
|--------|--------|
| Available machines and peripherals | Beebium server |
| Emulator operations and state | Beebium protocol |
| Meaning of Immersive Mode | Beebium client specification |
| Menu organisation | Client specification, with platform latitude |
| Keyboard shortcut conventions | Platform implementation |
| File picker appearance | Operating system |
| Window decorations | Desktop environment |
| Emulator display fidelity | Shared rendering specification |

That last distinction is particularly valuable. You can insist on identical emulated display behaviour across platforms without insisting on identical application interfaces.

## The macOS client becomes the reference implementation

I wouldn't necessarily describe the macOS client as the *canonical* implementation, because that could accidentally turn Apple-specific design choices into universal requirements.

Instead, it is the **first implementation from which the behavioural specification is derived**\.

That also gives you a useful opportunity to review existing design decisions. Some features might turn out to be intrinsic to Beebium, while others are simply convenient macOS affordances.

And I would make the specification testable wherever possible. For example, a common suite of client conformance scenarios could verify that changing machine configurations, inserting discs, reconnecting to servers and handling unavailable capabilities produces the expected observable behaviour, irrespective of the UI toolkit.

One further thought: your declarative server architecture might eventually support **clients with substantially different interaction models**, not merely different desktop operating systems. A browser client, a dedicated Raspberry Pi appliance, or even a command-line control client could implement different subsets of the same capabilities without compromising the underlying design.

I'd therefore resist specifying that every client must implement every feature. Instead, define a core behavioural profile and optional feature profiles such as debugging, configuration editing, peripheral management and advanced display controls.

That gives you something more useful than a cross-platform GUI specification: **a platform-independent Beebium client contract, with desktop GUI conformance as one profile.**

And crucially, it doesn't require introducing another abstraction layer into your running software merely for architectural tidiness. Much of the contract already exists in your server protocol; the work is primarily to document and formalise the client-side semantics.

## Prompt:
31/10/2026, 21:08:15

Bessegen is up and running with Sequoia 15.8.2 :-)

## Response:
31/10/2026, 21:08:17 · gpt-6

Excellent\! 🎉

From a seemingly broken 2018 Intel Mac mini, through a T2 DFU recovery, Mojave, transferring the 15.7 GB Sequoia installer over SSH from Rhea, and finally to **macOS Sequoia 15.8.2**\.

Quite a resurrection for Bessegen\!

And another useful machine in your Beebium development and testing fleet.

## Prompt:
31/10/2026, 21:08:32

Absolutely.

## Response:
31/10/2026, 21:08:33 · gpt-6

A satisfying result. And it's particularly nice to have another real Intel Mac available for Beebium testing, rather than relying entirely on Apple Silicon.

## Prompt:
31/10/2026, 22:57:14

I agree that Qt and Vulkan are a good path, but what about UI/UX norms on that platform?

## Response:
31/10/2026, 22:57:16 · gpt-6

This is where Linux becomes rather more complicated than macOS. But I think there's a useful distinction between **the absence of a universal Linux human interface standard** and the absence of established conventions. Linux certainly has the latter.

For Beebium, I'd aim for an application that feels at home on Raspberry Pi OS, KDE Plasma and GNOME, without attempting to imitate all three.

## There are several overlapping sets of conventions

The two most influential design systems are\:

[![KDE Plasma 5.24 LTS Releases with Updated Breeze Theme and New Overview Effect](https://images.openai.com/static-rsc-4/-bTvrYjbodH0M0l94qwhChxmAVF2xTEQqlnCKVDJUfS3ZMPZJzTSYPOj3tFmOSwRT4B_AgVwO5ve6WltSFlKBWSiAms8jATvfOK3sniIYwubNSbi4utgPhm8TR6jMs9-MahyeVlujB3nvRPBijAWAp2BieK31dOIw4S4UtfeOThE22zu1TKcOOPJ6_IS_icq?purpose=fullsize)](https://news.itsfoss.com/kde-plasma-5-24-lts-release/)

**KDE Plasma / Breeze**

Traditional desktop applications with menus, toolbars, status bars, keyboard shortcuts, configurable interfaces and conventional dialogs.

[![Files – GNOME 应用](https://images.openai.com/static-rsc-4/o-Gwe74Bz5AgJXaDBvAXADVJF5UhYbuGJNKHHTDRC0Te-6HhyIKhQnXrCRV3ByjFgxojHTEQ_Q5A6S9N7VvzNKkMRWyuxybHD3jxVwZcA7CPo9iha45bUD7DlTPNjnYUa_3ndhd0T6xbQCqzLQarhTI_cIsjN9Cw9s89XewDTOQnwIv4sHblykzRk3EXnPVR?purpose=fullsize)](https://apps.gnome.org/zh-CN/Nautilus/)

**GNOME / Adwaita**

Header bars, simplified menus, contextual controls, fewer persistent UI elements, and a stronger preference for opinionated application layouts.

For an emulator with machine configuration, media management, debugging, peripheral control and display settings, **the KDE/Qt tradition is a substantially better conceptual fit**\.

That doesn't mean Beebium needs to look like a KDE application. It means the conventional desktop application model is already well understood by Linux users.

Raspberry Pi OS is interesting because its desktop is comparatively traditional, despite its GTK heritage. It isn't GNOME, and its users are accustomed to conventional menus and windows.

## What I'd recommend for Beebium

I'd adopt a small set of explicit Linux UX rules.

| Area | Recommended convention |
|--------|--------|
| Main menu | Traditional menu bar: File, Machine, View, Tools, Help |
| Settings | Dedicated Preferences/Settings dialog |
| Keyboard shortcuts | Ctrl-based rather than macOS Command-based |
| Context menus | Right-click, where appropriate |
| File selection | Native desktop file dialogs |
| Window decorations | Let the window manager own them |
| Notifications | Desktop notifications for background events; in-app status for routine events |
| Full-screen | F11, with an obvious exit mechanism |
| Help | F1 and conventional Help menu |
| Dialog buttons | Platform/theme conventions rather than hardcoded macOS ordering |
| Theme | Follow system light/dark theme where practical |
| Accessibility | Keyboard navigation, focus indicators and accessible control names |

These aren't all universal Linux standards. They're sensible defaults for a traditional Linux desktop application.

## Qt provides more integration than you might expect

One benefit of Qt Widgets is that it doesn't require you to implement every convention yourself.

Qt's platform integration can provide native or desktop-integrated file dialogs, clipboard handling, drag-and-drop, system colours, font selection, accessibility and input methods.

Its styling system also allows applications to adopt the appearance of the surrounding desktop, although the results vary by distribution and theme.

I would **avoid shipping a custom Beebium widget theme**\. Let the operating system choose how buttons, checkboxes, scrollbars and dialogs look.

Your distinctive Beebium visual identity belongs in the emulator display, application icon, About dialog and perhaps some carefully chosen graphical controls-not in a wholesale replacement for native widgets.

## A particularly important issue: keyboard input

For an emulator, Linux keyboard handling deserves more attention than it would in an ordinary desktop application.

You have at least three distinct concepts\:

1. Physical key position and scancode.
2. Host keyboard layout and interpreted symbols.
3. Emulated BBC Micro key position.

On Linux, X11 and Wayland have different mechanisms for delivering keyboard events, and Wayland deliberately restricts some forms of global input capture.

Qt provides a useful abstraction, but it doesn't eliminate the distinction between physical and logical keyboard input.

I'd make the Linux keyboard mapping implementation explicitly aware of XKB layouts and physical key identity, and test it on both Wayland and X11.

The graphical keyboard mapping interface you've developed for macOS should translate well conceptually, even if the host keyboard detection and event handling are substantially different.

## Where I would deliberately depart from Linux conventions

There are a few places where consistency *within Beebium* matters more than consistency with a desktop environment.

**Immersive Mode** is one. You've already established a meaningful distinction between immersive and full-screen presentation. Preserve it on Linux, even if neither KDE nor GNOME has an equivalent concept.

**Machine configuration** is another. Since much of the interface is driven by declarative information supplied by Beebium servers, the arrangement and meaning of controls should remain consistent across clients, while their visual appearance follows the host platform.

**Debugger interfaces** are a third. If you eventually offer memory inspectors, disassembly views and breakpoint management, established debugger and IDE conventions are more useful than general desktop conventions. Qt's dockable windows could be particularly appropriate here.

## The standards I'd actually follow

There are several relevant specifications worth knowing about\:

- [freedesktop.org specifications](https://specifications.freedesktop.org/?utm_source=chatgpt.com) - desktop entries, MIME associations, notifications, icon naming and interoperability.
- [KDE Human Interface Guidelines](https://develop.kde.org/hig/?utm_source=chatgpt.com) - particularly relevant to a Qt Widgets application.
- [GNOME Human Interface Guidelines](https://developer.gnome.org/hig/?utm_source=chatgpt.com) - useful as a contrasting design philosophy.
- [Qt 6 documentation](https://doc.qt.io/qt-6/?utm_source=chatgpt.com) - platform integration, widget behaviour, shortcuts and accessibility.

I'd treat the KDE HIG as the **primary reference for interaction conventions**, not as a requirement to adopt KDE styling.

## One subtle but important recommendation

I would separate three kinds of consistency in your client specification\:

- **Semantic consistency\:** the same operations mean the same things everywhere. Mandatory.
- **Interaction consistency\:** workflows are recognisably similar, but shortcuts, dialogs and menu placement can follow platform conventions.
- **Visual consistency\:** branding, emulator display and specialist visualisations are shared; ordinary controls remain native.

This avoids both extremes: a Linux client that feels like a badly ported Mac application, and a Linux client that behaves so differently that existing Beebium users must relearn it.

My preference would be to make **Raspberry Pi OS the reference environment for Linux UX**, with Qt Widgets following conventional KDE-style desktop interaction patterns. That seems particularly appropriate for the audience Beebium is likely to attract: people who appreciate traditional desktop applications, precise control and discoverable functionality.

One question worth resolving early is whether you want the Linux client to feel primarily like **a conventional desktop application that happens to emulate a BBC Micro**, or like **a dedicated virtual BBC Micro appliance with desktop controls available when needed**\. Your existing Immersive Mode suggests you may eventually want both, and Qt is quite capable of accommodating that distinction.



---
Powered by [ChatGPT Exporter](https://www.chatgptexporter.com)