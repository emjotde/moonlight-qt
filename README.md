# Moonlight PC

[Moonlight PC](https://moonlight-stream.org) is an open source PC client for NVIDIA GameStream and [Sunshine](https://github.com/LizardByte/Sunshine).

Moonlight also has mobile versions for [Android](https://github.com/moonlight-stream/moonlight-android) and [iOS](https://github.com/moonlight-stream/moonlight-ios).

This is an unofficial fork based on Moonlight Qt v6.1.0, modified on September 25,
2026 (UTC), with per-application streaming profiles and Windows desktop controls.
The upstream downloads linked below do not include these changes. See
**Per-application stream settings** and **Windows stream controls** below for
the fork's features. See [Moonlight URI launching on Windows](docs/URI-LAUNCHING.md)
for secure dashboard and shortcut integration.

You can follow development on our [Discord server](https://moonlight-stream.org/discord) and help translate Moonlight into your language on [Weblate](https://hosted.weblate.org/projects/moonlight/moonlight-qt/).

 [![AppVeyor Build Status](https://ci.appveyor.com/api/projects/status/glj5cxqwy2w3bglv/branch/master?svg=true)](https://ci.appveyor.com/project/cgutman/moonlight-qt/branch/master)
 [![Downloads](https://img.shields.io/github/downloads/moonlight-stream/moonlight-qt/total)](https://github.com/moonlight-stream/moonlight-qt/releases)
 [![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-qt/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-qt/)

## Features
 - Hardware accelerated video decoding on Windows, Mac, and Linux
 - H.264, HEVC, and AV1 codec support (AV1 requires Sunshine and a supported host GPU)
 - HDR streaming support
 - 7.1 surround sound audio support
 - 10-point multitouch support (Sunshine only)
 - Gamepad support with force feedback and motion controls for up to 16 players
 - Support for both pointer capture (for games) and direct mouse control (for remote desktop)
 - Support for passing system-wide keyboard shortcuts like Alt+Tab to the host
 
## Downloads
- [Windows, macOS, and Steam Link](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Snap (for Ubuntu-based Linux distros)](https://snapcraft.io/moonlight)
- [Flatpak (for other Linux distros)](https://flathub.org/apps/details/com.moonlight_stream.Moonlight)
- [AppImage](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Raspberry Pi 4 and 5](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Raspberry-Pi-4)
- [Generic ARM 32-bit and 64-bit Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-ARM%E2%80%90based-Single-Board-Computers) (not for Raspberry Pi)
- [Experimental RISC-V Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-RISC%E2%80%90V-Single-Board-Computers)
- [NVIDIA Jetson and Nintendo Switch (Ubuntu L4T)](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Linux4Tegra-(L4T)-Ubuntu)

#### Special Thanks

[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&style=flat-square)](https://cloudsmith.com)

Hosting for Moonlight's Debian and L4T package repositories is graciously provided for free by [Cloudsmith](https://cloudsmith.com).

## Building

### Per-application stream settings

Right-click an application or desktop tile and choose **Stream settings...**.
Tiles with artwork also display the application's actual name below the image,
so desktops with identical artwork remain distinguishable.
Uncheck **Use global settings** to save a resolution and frame rate for that
application on that host. Portrait and custom resolutions are supported.
Optionally enable a custom bitrate; otherwise the global bitrate is inherited.
Profiles can also select a launch mode (windowed, borderless fullscreen, or
exclusive fullscreen), a preferred monitor, and system shortcut routing.
Each of these can inherit the global/default behavior. Other settings,
including codec, HDR, audio, and mouse behavior, remain global.

**Reset to global**, or saving with **Use global settings** checked, removes the
profile. Changes apply to the next launch or resume, not an active stream.
The host must already support/configure the requested display orientation.

Profiles are stored in the existing QSettings configuration under
`appstreamingprofiles/<UTF-8 hex-encoded host UUID>/<application ID>`, with
`enabled`, `width`, `height`, `fps`, and optional `bitrate` (Kbps), `windowmode`,
`capturesyskeys`, and `display` values.
Names and network addresses are not part of the key. Existing settings are not
rewritten or migrated. If the host changes an application's ID, it needs a new
profile. Invalid saved profiles are logged and ignored.

For both GUI and CLI launches, explicit CLI resolution, FPS, bitrate,
`--display-mode`, and `--capture-system-keys` options
take precedence over the profile, then global settings. With a profile, an
omitted bitrate inherits the global bitrate even when CLI resolution/FPS options
are supplied. Without a profile, the existing CLI automatic bitrate behavior is
unchanged. Effective preferences are session-owned; neither profiles nor CLI
options modify the global preferences.

The profile dialog uses the existing custom-resolution/frame-rate limits:
256-8192 pixels per dimension and 10-9999 FPS. Custom bitrate is 500-500000 Kbps.
These are input limits, not a guarantee of hardware support; very high FPS and
bitrate values are experimental.

#### Windows stream controls

Hover briefly at the top center of the stream's client area, or press
**Ctrl+Alt+Shift+B**, to reveal the dock. It disappears completely after the
pointer leaves; no collapsed tab or rectangle remains over the video.
The dock provides:

- **Full screen / Windowed**: toggle on the window's current monitor.
  A session launched windowed toggles to borderless fullscreen.
  **Ctrl+Alt+Shift+X** continues to work; the maximize button keeps its usual
  windowed behavior.
- **Monitor**: move the current stream to another connected monitor without
  reconnecting, retaining its fullscreen/windowed state.
- **Keys: Auto / Local / Remote**: choose where system shortcuts go.
  Auto keeps them local while windowed (including maximized), and captures
  them for the remote host while fullscreen. Local keeps them on the client;
  Remote captures them for the host while the stream has input focus.
  Normal typing still goes to the remote desktop in all three modes.
- **Disconnect**: disconnect without quitting the remote app, even if the
  session's "Quit app after ending stream" setting is enabled.

Dock changes are session-only. Use the tile's **Stream settings...** dialog to
save launch mode, monitor, or shortcut defaults. Mouse and keyboard input used
to interact with the dock is not forwarded to the host. Local Windows-key
chords, including their key releases, are filtered out of remote keyboard input.

On Windows, preferred monitors use their device interface identifiers rather
than enumeration order. Screen metadata is a logged fallback if the system
cannot provide an identifier. If a saved monitor is disconnected, Moonlight
warns and launches on the monitor containing its main window. The saved choice
is retained, including when editing a profile while the monitor is absent.

The dock uses a native Windows tool window, because streaming intentionally
suspends Qt's UI event loop. It does not resume the main UI or change the stream
protocol.

#### Windows URI launches

Windows dashboards and shortcuts can request a typed stream launch through
`moonlight://stream`. External requests are confirmed by default, trusted hosts
are keyed by paired UUID, and a second URI process forwards to the existing
Moonlight instance. The installer registers the protocol per-user; portable
builds provide `--register-uri` and `--unregister-uri`.

See [docs/URI-LAUNCHING.md](docs/URI-LAUNCHING.md) for the grammar, supported
parameters, precedence, security model, registration details, and CLI fallback.

#### Profile tests

The focused Qt Test suite uses temporary INI files, never your Moonlight
configuration. It covers preference isolation, landscape/portrait/custom
resolutions, bitrate inheritance, CLI precedence and legacy behavior, reset,
host/app isolation, a separate-process persistence check, existing settings,
invalid input, write failures, desktop-preference inheritance, monitor identity
under reordering/disconnection, shortcut capture policy, whole-chord input
filtering, the scrollable QML dialog, URI validation and precedence, external
confirmation/trust, startup queueing, cross-process forwarding, duplicate
suppression, and Windows protocol registration/removal.

From a Qt/MSVC command prompt, with submodules initialized:

```bat
mkdir build\profile-tests
cd build\profile-tests
qmake ..\..\tests\appstreamingsettings.pro CONFIG+=release
..\..\scripts\jom.exe
release\tst_appstreamingsettings.exe
```

### Windows Build Requirements
* Qt 5.15 SDK or later. Qt 6 is also supported for x64 and ARM64 builds.
* [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community edition is fine)
* Select **MSVC** option during Qt installation. MinGW is not supported.
* [7-Zip](https://www.7-zip.org/) (only if building installers for non-development PCs)
* Graphics Tools (only if running debug builds)
  * Install "Graphics Tools" in the Optional Features page of the Windows Settings app.
  * Alternatively, run `dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0` and reboot.

### macOS Build Requirements
* Qt 6.4 SDK or later
* Xcode 13 or later
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only if building DMGs for use on non-development Macs)

### Linux/Unix Build Requirements
* Qt 6 is recommended, but Qt 5.9 or later is also supported (replace `qmake6` with `qmake` when using Qt 5).
* GCC or Clang
* FFmpeg 4.0 or later
* Install the required packages:
  * Debian/Ubuntu:
    * Base Requirements: `libegl1-mesa-dev libgl1-mesa-dev libopus-dev libsdl2-dev libsdl2-ttf-dev libssl-dev libavcodec-dev libavformat-dev libswscale-dev libva-dev libvdpau-dev libxkbcommon-dev wayland-protocols libdrm-dev`
    * Qt 6 (Recommended): `qt6-base-dev qt6-declarative-dev libqt6svg6-dev qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick`
    * Qt 5: `qtbase5-dev qt5-qmake qtdeclarative5-dev qtquickcontrols2-5-dev qml-module-qtquick-controls2 qml-module-qtquick-layouts qml-module-qtquick-window2 qml-module-qtquick2 qtwayland5`
  * RedHat/Fedora (RPM Fusion repo required):
    * Base Requirements: `openssl-devel SDL2-devel SDL2_ttf-devel ffmpeg-devel libva-devel libvdpau-devel opus-devel pulseaudio-libs-devel alsa-lib-devel libdrm-devel`
    * Qt 6 (Recommended): `qt6-qtsvg-devel qt6-qtdeclarative-devel`
    * Qt 5: `qt5-qtsvg-devel qt5-qtquickcontrols2-devel`
* Building the Vulkan renderer requires a `libplacebo-dev`/`libplacebo-devel` version of at least v7.349.0 and FFmpeg 6.1 or later.

### Steam Link Build Requirements
* [Steam Link SDK](https://github.com/ValveSoftware/steamlink-sdk) cloned on your build system
* STEAMLINK_SDK_PATH environment variable set to the Steam Link SDK path

### Build Setup Steps
1. Install the latest Qt SDK (and optionally, the Qt Creator IDE) from https://www.qt.io/download
    * You can install Qt via Homebrew on macOS, but you will need to use `brew install qt --with-debug` to be able to create debug builds of Moonlight.
    * You may also use your Linux distro's package manager for the Qt SDK as long as the packages are Qt 5.9 or later.
    * This step is not required for building on Steam Link, because the Steam Link SDK includes Qt 5.14.
2. Run `git submodule update --init --recursive` from within `moonlight-qt/`
3. Open the project in Qt Creator or build from qmake on the command line.
    * To build a binary for use on non-development machines, use the scripts in the `scripts` folder.
        * For Windows builds, use `scripts\build-arch.bat` and `scripts\generate-bundle.bat`. Execute these scripts from the root of the repository within a Qt command prompt. Ensure  7-Zip binary directory is on your `%PATH%`.
        * For macOS builds, use `scripts/generate-dmg.sh`. Execute this script from the root of the repository and ensure Qt's `bin` folder is in your `$PATH`.
        * For Steam Link builds, run `scripts/build-steamlink-app.sh` from the root of the repository.
    * To build from the command line for development use on macOS or Linux, run `qmake6 moonlight-qt.pro` then `make debug` or `make release`
    * To create an embedded build for a single-purpose device, use `qmake6 "CONFIG+=embedded" moonlight-qt.pro` and build normally.
        * This build will lack windowed mode, Discord/Help links, and other features that don't make sense on an embedded device.
        * For platforms with poor GPU performance, add `"CONFIG+=gpuslow"` to prefer direct KMSDRM rendering over GL/Vulkan renderers. Direct KMSDRM rendering can use dedicated YUV/RGB conversion and scaling hardware rather than slower GPU shaders for these operations.

## Contribute
1. Fork us
2. Write code
3. Send Pull Requests

Check out our [website](https://moonlight-stream.org) for project links and information.
