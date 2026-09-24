# Koncrepe - KDE's Terminal Emulator

Koncrepe is a terminal program for KDE.


## HOWTO Build

1. Install dependencies. On neon:
```
apt install git cmake make g++ extra-cmake-modules libkf6config-dev libkf6auth-dev libkf6package-dev libkf6declarative-dev libkf6coreaddons-dev libkf6kcmutils-dev libkf6i18n-dev libkf6crash-dev libkf6newstuff-dev libkf6textwidgets-dev libkf6iconthemes-dev libkf6dbusaddons-dev libkf6notifyconfig-dev libkf6pty-dev libkf6notifications-dev libkf6parts-dev qt6-base-dev libqt6core6t64 libqt6widgets6 libqt6gui6 libqt6qml6 libicu-dev
```
   The terminal engine additionally needs clang (19 or newer), python3,
   ragel, pkg-config, libvulkan-dev, libwayland-dev and libxkbcommon-dev.
2. Clone with `git clone --recurse-submodules <this repository> koncrepe`
   (or run `git submodule update --init` in an existing clone)
3. Make _build_ directory: `mkdir koncrepe/build`
4. Change into _build_ directory: `cd koncrepe/build`
5. Configure: `cmake ..` (or `cmake .. -DCMAKE_INSTALL_PREFIX=/where/your/want/to/install`)
6. Build: `make`
7. Install: `make install`



