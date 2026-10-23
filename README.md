# Ambaretto

A C++ city builder and sandbox game. Draw islands and water with Bezier pens, build curved roads, place buildings freely, create characters and vehicles, then explore on foot, by car, or by aircraft. Built with [SDL3](https://libsdl.org/), [raylib](https://www.raylib.com/), and [Jolt Physics](https://github.com/jrouwe/JoltPhysics).

See the [full guide](docs/GUIDE.md) for editor instructions, controls, save formats, architecture, and tests.

## Requirements

- C and C++17 compilers, CMake **3.25 or newer**, Ninja, and Git.
- A desktop session and an OpenGL **3.3** capable graphics driver.
- On x86-64, the default game/physics build uses SSE2; AVX, FMA, F16C, and SSE4 are not required.
- Internet access for the first setup: CMake downloads and builds pinned static SDL **3.2.30**, raylib **6.0**, and Jolt **5.4.0** libraries automatically. No preinstalled SDL, raylib, GLFW, or Jolt libraries are needed.

SDL3 manages the window, OpenGL context, events, and input through raylib's SDL backend. Raylib/rlgl still draws the 2D interface and 3D scene with OpenGL 3.3; the game and raylib use the same SDL3 instance. See [portability](docs/GUIDE.md#code-and-physics) for the remaining Android work.

Build commands use two parallel jobs to limit memory use. Use `--parallel 1` if your computer runs out of RAM.

## Windows

Install [MSYS2](https://www.msys2.org/), then open **MSYS2 UCRT64** from the Start menu. Update it:

```bash
pacman -Syu
```

If asked to close the terminal, reopen UCRT64 and run the update again. Install the compiler and build tools:

```bash
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
```

Download, compile, and run:

```bash
git clone https://github.com/daffarahman/ambaretto.git
cd ambaretto
cmake --preset ucrt64-release -DBUILD_TESTING=OFF
cmake --build --preset ucrt64-release --target Ambaretto --parallel 2
./build/ucrt64-release/Ambaretto.exe
```

Run from UCRT64 so the required DLLs are on `PATH`. For PowerShell, add your MSYS2 installation's `ucrt64\bin` directory to `PATH` first; the default is `C:\msys64\ucrt64\bin`.

## Linux

Install the tools and graphics/audio development libraries for your distribution, then follow [Compile on Linux or macOS](#compile-on-linux-or-macos). These commands cover common Linux families and their derivatives; use a supported release with CMake 3.25 or newer. SDL detects available system features at configure time; pkg-config helps discover the development libraries. See the [SDL Linux dependency guide](https://github.com/libsdl-org/SDL/blob/release-3.2.30/docs/README-linux.md) for optional audio, controller, and Wayland support.

| Distributions | Install dependencies |
| --- | --- |
| **Debian / Ubuntu**: Linux Mint, Pop!_OS, Zorin OS, elementary OS, MX Linux, Kali, Parrot, KDE neon, Kubuntu, Xubuntu, Lubuntu, Ubuntu MATE, Deepin | `sudo apt update`, then `sudo apt install build-essential git cmake ninja-build pkg-config libasound2-dev libgl1-mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev` |
| **Fedora / RHEL**: Nobara, CentOS Stream, Rocky Linux, AlmaLinux, Oracle Linux | `sudo dnf install gcc-c++ git cmake ninja-build pkgconf-pkg-config alsa-lib-devel mesa-libGL-devel libX11-devel libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel` |
| **Arch Linux**: Manjaro, EndeavourOS, Garuda Linux, CachyOS | `sudo pacman -Syu --needed base-devel git cmake ninja pkgconf alsa-lib mesa libx11 libxrandr libxi libxcursor libxinerama` |
| **openSUSE**: Leap, Tumbleweed, Slowroll | `sudo zypper install gcc-c++ git cmake ninja pkg-config alsa-devel Mesa-libGL-devel libX11-devel libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel` |
| **Alpine Linux** | `sudo apk add build-base git cmake ninja pkgconf alsa-lib-dev mesa-dev libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev` |
| **Void Linux** | `sudo xbps-install -S base-devel git cmake ninja pkg-config alsa-lib-devel MesaLib-devel libX11-devel libXrandr-devel libXi-devel libXcursor-devel libXinerama-devel` |

On RHEL and its derivatives, enable the development repository for your release (CodeReady Builder or CRB) if development packages are unavailable. Older distributions may also need a newer [CMake](https://cmake.org/download/).

<details>
<summary>Gentoo, Solus, and NixOS</summary>

**Gentoo** (with its normal compiler toolchain installed):

```bash
sudo emerge --ask dev-vcs/git dev-build/cmake dev-build/ninja virtual/pkgconfig \
  media-libs/alsa-lib media-libs/mesa x11-libs/libX11 x11-libs/libXrandr \
  x11-libs/libXi x11-libs/libXcursor x11-libs/libXinerama
```

**Solus**: install its [development component](https://help.getsol.us/docs/user/software/development/) and libraries:

```bash
sudo eopkg install -c system.devel
sudo eopkg install git cmake ninja pkg-config alsa-lib-devel mesalib-devel \
  libx11-devel libxrandr-devel libxi-devel libxcursor-devel libxinerama-devel
```

**NixOS**: enter a development shell, and keep it open for compilation and launching the game:

```bash
nix-shell -p gcc git cmake ninja pkg-config alsa-lib libGL \
  xorg.libX11 xorg.libXrandr xorg.libXi xorg.libXcursor xorg.libXinerama
export LD_LIBRARY_PATH="$(nix-build '<nixpkgs>' -A alsa-lib --no-out-link)/lib:$(nix-build '<nixpkgs>' -A libGL --no-out-link)/lib:${LD_LIBRARY_PATH:-}"
```

</details>

**Immutable distributions** (Fedora Silverblue/Kinoite, Bazzite, SteamOS, openSUSE Aeon/Kalpa): use a development container such as [Distrobox](https://distrobox.it/) with Podman installed:

```bash
distrobox create --name ambaretto-dev --image ubuntu:24.04
distrobox enter ambaretto-dev
```

Run the Ubuntu dependency command and the compilation steps inside the container, and launch the game there. The packages above cover X11; a Wayland desktop can use XWayland or the additional development packages in SDL's guide.

## macOS

Install Apple's command-line developer tools:

```bash
xcode-select --install
```

Install [Homebrew](https://brew.sh/) using its website's instructions, including the `brew shellenv` step, then install the build tools:

```bash
brew install git cmake ninja
```

Continue with the shared compilation steps below in Terminal. Use the native toolchain on either Intel or Apple Silicon.

## Compile on Linux or macOS

Download the project:

```bash
git clone https://github.com/daffarahman/ambaretto.git
cd ambaretto
```

CMake builds the pinned dependencies inside the build directory. Compile and run the game from the project directory:

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --target Ambaretto --parallel 2
./build/release/Ambaretto
```

After changing game code, repeat only the game build command. No separate dependency installation or library path setup is needed.

### Illegal instruction on Linux

Older builds left some of Jolt's CPU extensions enabled even with AVX disabled. A CPU without those extensions can exit with `Illegal instruction (core dumped)`, as described in [Jolt's build guide](https://github.com/jrouwe/JoltPhysics/blob/v5.4.0/Build/README.md#illegal-instruction-error). The current CMake configuration disables all optional x86 extensions. Reconfigure and rebuild from the project directory; these explicit options also fix older checkouts:

```bash
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DUSE_SSE4_1=OFF -DUSE_SSE4_2=OFF \
  -DUSE_AVX=OFF -DUSE_AVX2=OFF -DUSE_AVX512=OFF \
  -DUSE_LZCNT=OFF -DUSE_TZCNT=OFF -DUSE_F16C=OFF -DUSE_FMADD=OFF
cmake --build build/release --target Ambaretto --parallel 2
./build/release/Ambaretto
```

Reconfiguration updates existing cached settings and rebuilds the affected objects; keep saved cities and designs in place. Avoid manually adding `-march=native` or CPU-specific flags when targeting older machines. If the crash persists, collect the CPU flags with `lscpu` and a stack trace with `gdb ./build/release/Ambaretto` (`run`, then `bt` and `x/i $pc`). On Fedora, install GDB with `sudo dnf install gdb`.

## First launch

Choose **Create / edit** to make a city, building, car, or character. **Play** lets you choose a city and a mode: **Free roam** needs a spawn and valid Player character; **Car only** needs a spawn and a saved car, keeps you in that car, and respawns it after destruction. The [full guide](docs/GUIDE.md#city-builder) explains the workflow.

CMake copies `assets/` beside the executable automatically. Saved cities and designs also live beside the executable, so keep those folders when moving or backing up a build.

Windows is the locally tested build environment. Linux and macOS instructions follow the upstream build documentation; native builds on those systems still need verification. For Debug builds, tests, and GDB, see [Checks and debugging](docs/GUIDE.md#checks-and-debugging).
