<img src="desktop/icons/sc-apps-koncrepe.svg" alt="Koncrepe logo" width="96" align="right">

# Koncrepe - KDE's Terminal Emulator, on the shitty engine

Koncrepe is a terminal program for KDE: a fork of Konsole whose terminal
emulation is done by [shitty](https://github.com/pg83/shitty) instead of
Konsole's own VT102 emulation.

As well as being a standalone program, it provides a terminal KPart
(`koncrepepart`) that other programs can embed, as the Kwilt drop-down
terminal does. Koncrepe's features and usage are explained and illustrated in
the Koncrepe handbook, which can be accessed by browsing to `help:/koncrepe`
in Konqueror.


## The terminal engine

Everything a terminal emulator decides is shitty's: parsing the byte stream,
the screen and cursor state, modes, the replies to queries, and the encoding
of keyboard, mouse and focus input according to what the application
negotiated (cursor and keypad modes, modifyOtherKeys, the kitty keyboard
protocol, every mouse protocol).

Koncrepe keeps what belongs to the application around it: rendering, fonts,
selection, search, scrollback storage, tabs and splits, and its profiles.
`src/ShittyEmulation.cpp` connects the two through shitty's C embedding API
(`libshitty_vt`), built from the `3rdparty/shitty` submodule:

- The screens Koncrepe draws are models of shitty's state, refreshed before
  every repaint. Cells keep the colors the application asked for (default,
  palette entry, RGB), so Koncrepe's color schemes draw them.
- Lines leaving the top of the screen are copied into Koncrepe's history, so
  every history mode of a profile (none, fixed size, unlimited, file backed)
  works as before.
- The profile's color scheme and cursor color are pushed into shitty, which
  answers color queries (OSC 4/10/11/12) and light/dark reports with them.
- Key bindings (keytabs) stay in charge: their scrolling commands and any
  binding that differs from the stock one are Koncrepe's, the rest is left
  for shitty to encode.
- The profile's "kitty keyboard protocol" switch decides whether applications
  can turn the protocol on; "links in escape sequences" (OSC 8), shell
  integration (OSC 133: semantic hints, prompt navigation, error bars,
  semantic Up/Down, click to move the cursor), the working directory,
  notifications, progress and tab titles all keep working.

## HOWTO Build

Fetch the engine along with the sources:

```
git clone --recurse-submodules <this repository>
# or, in an existing clone:
git submodule update --init
```

Besides Konsole's usual KDE Frameworks and Qt dependencies, building the
engine needs clang with `-std=c++26` support (clang 19 or newer), Python 3,
Ragel, pkg-config, and the Vulkan and Wayland client development files
(shitty's build describes its own renderer even when only its embedding
library is built). On Debian:

```
apt install clang python3 ragel pkg-config libvulkan-dev libwayland-dev libxkbcommon-dev
```

Then build as usual:

```
cmake -B build -S . -DCMAKE_INSTALL_PREFIX=/where/to/install
cmake --build build
cmake --install build
```

## Directory Structure

| Directory          | Description                                                                                                                                                                        |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `/doc/user`        | README files, primarily for advanced users, explaining various aspects of Koncrepe such as fonts and keyboard handling in-depth.                                                    |
| `/doc/developer`   | README files and resources for developers of Koncrepe. This includes information on the design of Koncrepe's internals and the VT100 terminal on which Koncrepe's emulation is based. |
| `/src`             | Source code for Koncrepe, including the embedded versions of Koncrepe which are used in Kate, KDevelop and others.                                                                   |
| `/desktop`         | .desktop files for Koncrepe, used to launch the program from KDE's various menus and other application launchers.                                                                   |
| `/data`            | Data files for use with Koncrepe as well as the keyboard setup and color schemes provided with Koncrepe.                                                                             |
| `/3rdparty/shitty` | The shitty terminal engine (git submodule of the upstream repository).                                                                                                             |
| `/cmake`           | Builds shitty's embedding library as part of the CMake build.                                                                                                                      |

## License

Koncrepe's own sources keep Konsole's licenses (mostly GPL-2.0-or-later; see
`LICENSES/` and the SPDX headers). Koncrepe links the shitty terminal engine:

> Copyright (c) 2026 Shitty team
>
> shitty's original contributions are dual-licensed under the GNU General
> Public License, version 3 or any later version, or the MIT License; the
> code it imported in 2023 remains under the GPL version 3 or later. See
> `3rdparty/shitty/LICENSE`, `LICENSE.GPL3` and `LICENSE.MIT`.

Koncrepe binaries, which contain the engine, are therefore distributed under
the GNU General Public License version 3 only.

## Upstream

Koncrepe is based on Konsole (https://konsole.kde.org,
https://invent.kde.org/utilities/konsole). Please don't report Koncrepe bugs
to KDE. The terminal engine is developed by Anton Samokhvalov (pg83) at
https://github.com/pg83/shitty.
