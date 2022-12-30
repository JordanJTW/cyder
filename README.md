<div align="center"><img src="assets/logo.png?raw=true" height="40px"/></div>

# About

Cyder lets you run [Classic Mac OS](https://en.wikipedia.org/wiki/Classic_Mac_OS) applications on modern operating systems (like macOS and Linux) -- you can think of it as functionally equivalent to [WINE](https://www.winehq.org/) but for Classic Mas OS.

Cyder employs a form of [high-level emulation (HLE)](https://emulation.gametechwiki.com/index.php/High/Low_level_emulation). It uses the [Musashi](https://github.com/kstenerud/Musashi) Motorola 68k emulator to run original m68k binaries but all calls to the [Macintosh Toolbox](https://en.wikipedia.org/wiki/Macintosh_Toolbox) are intercepted and implemented in native code to run on the host. Cyder is currently targeting compatiblity with [Mac OS 6.0.8](https://en.wikipedia.org/wiki/Classic_Mac_OS#System_Software_6) but may target higher versions as time goes on.

A major goal of the project is to be easy to understand and learn from (it was started as a learning exercise by the author) and promote a better understanding of retro computers.

# Build

Ensure submodule is sync'd:

```console
git submodule update --init --recursive
```

<details><summary>Using cyder.py</summary>

```console
# To build all targets
./cyder build

# To build the emulator
./cyder build emu
```

</details>

<details><summary>Using Make</summary>

```console
mkdir build && cd build
cmake ..

# To build all targets
make
```

</details>

<details><summary>Using Ninja</summary>

```console
mkdir -p build/out
cmake -GNinja -Bbuild/out

# Only this command is needed to build from now on
ninja -C build/out
```

</details>
