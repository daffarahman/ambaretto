# Ambaretto

A C++ city builder and sandbox game. Create islands, roads, buildings, characters, and vehicles, then explore on foot, by car, or by aircraft. Built with [raylib](https://www.raylib.com/) and [Jolt Physics](https://github.com/jrouwe/JoltPhysics).

See the [full guide](docs/GUIDE.md) for editor instructions, controls, save formats, architecture, and tests.

## Requirements

- A C++17 compiler, CMake **3.24 or newer**, Ninja, Git, and pkg-config.
- raylib **6.0** and GLFW **3.4 or newer**, sharing the same GLFW library. The steps below install these.
- A desktop session and an OpenGL **3.3** capable graphics driver.
- Internet access for the first setup: CMake downloads and builds Jolt **5.4.0** automatically. No separate Jolt installation is needed.

Build commands use two parallel jobs to limit memory use. Use `--parallel 1` if your computer runs out of RAM.

## Windows

Install [MSYS2](https://www.msys2.org/), then open **MSYS2 UCRT64** from the Start menu. Update it:

```bash
pacman -Syu
```

If asked to close the terminal, reopen UCRT64 and run the update again. Install the compiler and libraries, including the [MSYS2 raylib package](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-raylib):

```bash
pacman -S --needed git mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-raylib \
  mingw-w64-ucrt-x86_64-glfw
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

Install the tools and graphics/audio development libraries for your distribution, then follow [Compile on Linux or macOS](#compile-on-linux-or-macos). These commands cover common Linux families and their derivatives; use a supported release with CMake 3.24 or newer. The graphics/audio packages follow the [raylib Linux dependency guide](https://github.com/raysan5/raylib/wiki/Working-on-GNU-Linux).

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

Run the Ubuntu dependency command and the compilation steps inside the container, and launch the game there. The Linux build below uses X11; a Wayland desktop needs XWayland installed through its distribution.

## macOS

Install Apple's command-line developer tools:

```bash
xcode-select --install
```

Install [Homebrew](https://brew.sh/) using its website's instructions, including the `brew shellenv` step, then install the build tools:

```bash
brew install git cmake ninja pkgconf
```

Continue with the shared compilation steps below in Terminal. Use the native toolchain on either Intel or Apple Silicon.

## Compile on Linux or macOS

Download the project:

```bash
git clone https://github.com/daffarahman/ambaretto.git
cd ambaretto
```

Build [GLFW 3.4](https://www.glfw.org/docs/3.4/compile_guide.html) and [raylib 6.0](https://github.com/raysan5/raylib/blob/6.0/CMakeOptions.txt) once. Both libraries stay inside `build/deps/install`, with no system-wide installation. Building them together ensures the game and raylib use the same GLFW for controller input.

```bash
mkdir -p build/deps
git clone --depth 1 --branch 3.4 https://github.com/glfw/glfw.git build/deps/glfw
cmake -S build/deps/glfw -B build/deps/glfw-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
  -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
  -DGLFW_BUILD_WAYLAND=OFF -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/deps/install"
cmake --build build/deps/glfw-build --parallel 2
cmake --install build/deps/glfw-build

git clone --depth 1 --branch 6.0 https://github.com/raysan5/raylib.git build/deps/raylib
cmake -S build/deps/raylib -B build/deps/raylib-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DBUILD_EXAMPLES=OFF \
  -DUSE_EXTERNAL_GLFW=ON -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/install" \
  -DCMAKE_INSTALL_PREFIX="$PWD/build/deps/install" \
  -DCMAKE_INSTALL_RPATH="$PWD/build/deps/install/lib"
cmake --build build/deps/raylib-build --parallel 2
cmake --install build/deps/raylib-build
```

Compile and run the game from the project directory:

```bash
export PKG_CONFIG_PATH="$PWD/build/deps/install/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build/release --target Ambaretto --parallel 2
./build/release/Ambaretto
```

After changing game code, repeat only the game build command. When configuring from a new terminal, set `PKG_CONFIG_PATH` again. Keep the dependency installation in place; moving the checkout requires reconfiguring its library paths.

## First launch

Choose **Create / edit** to make a city, building, car, or character. A playable city needs a player spawn and a valid saved Player character. The [full guide](docs/GUIDE.md#city-builder) explains the workflow.

CMake copies `assets/` beside the executable automatically. Saved cities and designs also live beside the executable, so keep those folders when moving or backing up a build.

Windows is the locally tested build environment. Linux and macOS instructions follow the upstream build documentation; native builds on those systems still need verification. For Debug builds, tests, and GDB, see [Checks and debugging](docs/GUIDE.md#checks-and-debugging).
