
## Contributing

Thank you for your interest in contributing to swblocks-baselib!

This page describes the details for the most recent versions of compiler toolchains and library dependencies (devenv7). You can see [CONTRIBUTING.DEVENV4.md](CONTRIBUTING.DEVENV4.md) file for details on the previous version of the supported development environments (devenv4).

swblocks-baselib is built on and intended for open source and we fully intend to accept public contributions in the near future. Until then, feel free to file issues and open pull requests, but note that we won't be merging them until the necessary processes are in place.

If you are currently J.P. Morgan Chase employee and interested in contributing you can contact [Lazar Ivanov](https://github.com/lazar-ivanov) about more details on the process and governance of contributions from current J.P. Morgan Chase employees.

The swblocks-baselib library comes with a comprehensive unit tests suite and virtually all parts of it are very extensively tested. It also comes currently with a couple of application binaries - 'bl-tool' and 'bl-messaging-broker'.

In order to be able to make changes to the library code and contribute these back you will need to be able to modify and add to the tests code suite and also of course be able to build and run the unit tests suite, etc. The way you can do this is by using the swblocks-baselib development environment and its own build system. The library comes also with its own flexible make files and build + test system which allows you to build these binaries and unit tests and to run the unit tests. To learn how to use the swblocks-baselib library development environment and build system look at the next section.

## Development environment and build system

The library development environment is based on GNU make, but it has somewhat sophisticated structure and implementation that is [declarative and build-by-convention](https://docs.gradle.org/current/userguide/overview.html) and of course also avoids the [pitfalls of recursive make](https://www.google.com/#q=recursive+make+considered+harmful). The development environment and the build system assumes certain structure of dependencies and how they are built. This structure and its assumptions are embedded in the make files and the steps to build and prepare the external dependencies are described in the various notes files (under notes folder in the root). As mentioned in the [README.md](README.md) file the swblocks-baselib library and its development environment and build system has very limited dependencies, namely a C++ compliant compiler and toolchain (e.g. recent versions of Clang, GCC or VC++), Boost (including Boost.JSON which is the default JSON library) and OpenSSL. Optionally, JSON Spirit 4.08 can be used instead of Boost.JSON by setting `BL_USE_JSON_SPIRIT=1` (see the JSON library selection section below). Preparing the dependencies for the development environment is relatively easy, but it is not automated (you have to follow the instructions in the notes files).

The development environment and build system has a minimal notion of versioning to facilitate evolution without having to break the world, but it generally requires "forced upgrade" model where older versions are quickly depreciated and the users / clients of the library are expected to upgrade and move forward. That applies to both the development environment and the code itself. The code of course is changed carefully with backward compatibility in mind and older versions of the development environment or dependencies are not explicitly broken or not supported, but they might be, so if you are a user of the library and you can't upgrade easily at some point you might not be able to pick up the latest version of swblocks-baselib library and might have to stay on older version - btw, the model is very similar to the way Boost is versioned and how backward compatibility is maintained there.

A development environment version is a notion of collection of specific versions of compiler toolchains + a collection of compatible versions of the 3rd party dependencies. Currently swblocks-baselib library officially supports two development environment versions - devenv7 (latest) and [devenv4](CONTRIBUTING.DEVENV4.md) (older) with the devenv4 environment of course to be eventually depreciated in the future. Here are the collections of the compiler toolchains versions and the 3rd party dependencies versions in the most recent development environment (for older environment see [CONTRIBUTING.DEVENV4.md](CONTRIBUTING.DEVENV4.md) file):

* **devenv7**
  * Operating Systems
    * Darwin / macOS platforms (ARM64 only)
      * Darwin 24 / macOS Sequoia
      * Darwin 25 / macOS Tahoe
    * Linux platforms
      * RHEL 9 (a64, x64)
      * RHEL 10 (a64)
      * Ubuntu 24.04 LTS (a64, x64)
      * Linux Mint 22.x based on Ubuntu 24.04 LTS (a64, x64)
    * Windows platforms
      * Windows 10+ (a64, x64, x86) — ARM64 (a64) is a notable new capability
  * Compilers
    * GCC 15.2.0 for all supported Linux platforms
    * Clang 20.1.0 for all supported Linux platforms
    * Apple Clang 17.0.0 for Darwin / macOS platforms
    * Microsoft vc143 Visual C++ 2022 (VC 17.08) for Windows platforms
    * Clang-CL ccl16 for Windows platforms (new — see Clang-CL section below)
  * C++ standard library implementations
    * libc++ with Clang for Darwin / macOS and Linux platforms
    * libstdc++ with GCC for Linux platforms
    * msvcrt for vc143 for Windows platforms
  * Boost 1.90.0 (with Boost.JSON as the default JSON library)
  * OpenSSL 3.5.4 (optionally OpenSSL 1.1.1w via BL_USE_OPENSSL_1X=1)
  * JSON Spirit 4.08 (optional, not enabled by default — see JSON library selection below)

### Clang-CL (ccl16) toolchain on Windows

devenv7 introduces Clang-CL support on Windows as an alternative compiler. Clang-CL (ccl16) uses the clang-cl.exe front-end with the MSVC standard library and linker, providing Clang diagnostics and optimizations while maintaining full ABI compatibility with MSVC-built code. Clang-CL is included in the devenv7 Windows distributions and no additional download is needed.

To build with Clang-CL, pass the TOOLCHAIN parameter to make:

```make
make -k -j4 TOOLCHAIN=ccl16
make -k -j4 TOOLCHAIN=ccl16 VARIANT=release
make -k -j4 TOOLCHAIN=ccl16 ARCH=x64
```

Clang-CL supports all target architectures: a64 (ARM64), x64 and x86. It uses the same MSVC installation, Windows SDK and pre-built libraries (Boost, OpenSSL) as the vc143 toolchain.

### JSON library selection

devenv7 uses Boost.JSON as the default JSON library. Boost.JSON is included in Boost 1.90.0 and is linked automatically when building with devenv7.

To use JSON Spirit instead of Boost.JSON (e.g. for backward compatibility with older code), pass the BL_USE_JSON_SPIRIT flag:

```make
make -k -j4 BL_USE_JSON_SPIRIT=1
```

The JSON library abstraction layer in the codebase allows transparent switching between Boost.JSON and JSON Spirit. Existing code that uses the bl::json types works unchanged with either library.

### Cross-compilation

Each devenv7 Windows distribution includes support for all target architectures (a64, x64, x86) via cross-compilation. By default, the build targets the host architecture. To cross-compile for a different architecture, use the ARCH parameter:

```make
make -k -j4 ARCH=x64
make -k -j4 ARCH=a64
make -k -j4 ARCH=x86
```

## Development environment distributions and links

In addition to the code dependencies of the library itself (compiler toolchain, Boost, OpenSSL) the development environment also has few additional dependencies such as GNU make (e.g. via MSYS2 on Windows), Git, Python, etc. The "binary blob" that contains the pre-built versions of the development environment with all code dependencies plus the additional tools is called "devenv distribution". Pre-built devenv distributions are available for download from storage.swblocks.net and are the recommended way to get started with the swblocks-baselib library. You can also browse the full list of available distributions at the [distribution index page](http://storage.swblocks.net/index.html). Here is the list of the currently supported devenv distributions:

### macOS distributions

* **devenv7** for macOS Darwin 24 (Sequoia), ARM64 can be downloaded from [here](https://storage.swblocks.net/devenv/7/macos/darwin24/a64/dist-devenv7-darwin-24-a64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/macos/darwin24/a64/dist-devenv7-darwin-24-a64.tar.gz
* **devenv7** for macOS Darwin 25, ARM64 can be downloaded from [here](https://storage.swblocks.net/devenv/7/macos/darwin25/a64/dist-devenv7-darwin-25-a64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/macos/darwin25/a64/dist-devenv7-darwin-25-a64.tar.gz

### Linux distributions

The combined distributions (with both GCC and Clang) are recommended as they include both compilers and allow building with either toolchain. Single-compiler variants (GCC-only or Clang-only) are also available for a smaller footprint when only one compiler is needed.

**RHEL 10 (a64):**

* **devenv7** for RHEL 10 a64 with GCC 15.2.0 and Clang 20.1.0 (recommended) can be downloaded from [here](https://storage.swblocks.net/devenv/7/rhel/rhel10/a64/dist-devenv7-rhel10-gcc1520-clang2010-a64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/rhel/rhel10/a64/dist-devenv7-rhel10-gcc1520-clang2010-a64.tar.gz
* GCC-only variant: [dist-devenv7-rhel10-gcc1520-a64.tar.gz](https://storage.swblocks.net/devenv/7/rhel/rhel10/a64/dist-devenv7-rhel10-gcc1520-a64.tar.gz)
* Clang-only variant: [dist-devenv7-rhel10-clang2010-a64.tar.gz](https://storage.swblocks.net/devenv/7/rhel/rhel10/a64/dist-devenv7-rhel10-clang2010-a64.tar.gz)

**RHEL 9 (a64):**

* **devenv7** for RHEL 9 a64 with GCC 15.2.0 and Clang 20.1.0 (recommended) can be downloaded from [here](https://storage.swblocks.net/devenv/7/rhel/rhel9/a64/dist-devenv7-rhel9-gcc1520-clang2010-a64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/rhel/rhel9/a64/dist-devenv7-rhel9-gcc1520-clang2010-a64.tar.gz
* GCC-only variant: [dist-devenv7-rhel9-gcc1520-a64.tar.gz](https://storage.swblocks.net/devenv/7/rhel/rhel9/a64/dist-devenv7-rhel9-gcc1520-a64.tar.gz)
* Clang-only variant: [dist-devenv7-rhel9-clang2010-a64.tar.gz](https://storage.swblocks.net/devenv/7/rhel/rhel9/a64/dist-devenv7-rhel9-clang2010-a64.tar.gz)

**RHEL 9 (x64):**

* **devenv7** for RHEL 9 x64 with GCC 15.2.0 and Clang 20.1.0 (recommended) can be downloaded from [here](https://storage.swblocks.net/devenv/7/rhel/rhel9/x64/dist-devenv7-rhel9-gcc1520-clang2010-x64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/rhel/rhel9/x64/dist-devenv7-rhel9-gcc1520-clang2010-x64.tar.gz
* GCC-only variant: [dist-devenv7-rhel9-gcc1520-x64.tar.gz](https://storage.swblocks.net/devenv/7/rhel/rhel9/x64/dist-devenv7-rhel9-gcc1520-x64.tar.gz)
* Clang-only variant: [dist-devenv7-rhel9-clang2010-x64.tar.gz](https://storage.swblocks.net/devenv/7/rhel/rhel9/x64/dist-devenv7-rhel9-clang2010-x64.tar.gz)

**Ubuntu 24.04 / Linux Mint 22.x (a64):**

* **devenv7** for Ubuntu 24.04 a64 (also used for Linux Mint 22.x) with GCC 15.2.0 and Clang 20.1.0 (recommended) can be downloaded from [here](https://storage.swblocks.net/devenv/7/ubuntu/ub24/a64/dist-devenv7-ub24-gcc1520-clang2010-a64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/ubuntu/ub24/a64/dist-devenv7-ub24-gcc1520-clang2010-a64.tar.gz
* GCC-only variant: [dist-devenv7-ub24-gcc1520-a64.tar.gz](https://storage.swblocks.net/devenv/7/ubuntu/ub24/a64/dist-devenv7-ub24-gcc1520-a64.tar.gz)
* Clang-only variant: [dist-devenv7-ub24-clang2010-a64.tar.gz](https://storage.swblocks.net/devenv/7/ubuntu/ub24/a64/dist-devenv7-ub24-clang2010-a64.tar.gz)

**Ubuntu 24.04 / Linux Mint 22.x (x64):**

* **devenv7** for Ubuntu 24.04 x64 (also used for Linux Mint 22.x) with GCC 15.2.0 and Clang 20.1.0 (recommended) can be downloaded from [here](https://storage.swblocks.net/devenv/7/ubuntu/ub24/x64/dist-devenv7-ub24-gcc1520-clang2010-x64.tar.gz); or from command line:
wget https://storage.swblocks.net/devenv/7/ubuntu/ub24/x64/dist-devenv7-ub24-gcc1520-clang2010-x64.tar.gz
* GCC-only variant: [dist-devenv7-ub24-gcc1520-x64.tar.gz](https://storage.swblocks.net/devenv/7/ubuntu/ub24/x64/dist-devenv7-ub24-gcc1520-x64.tar.gz)
* Clang-only variant: [dist-devenv7-ub24-clang2010-x64.tar.gz](https://storage.swblocks.net/devenv/7/ubuntu/ub24/x64/dist-devenv7-ub24-clang2010-x64.tar.gz)

### Windows distributions

Windows distributions use the VS2022 vc143 (VC 17.08) compiler toolchain and Windows SDK 10. Each distribution is organized by host architecture and includes support for all target architectures (a64, x64, x86) via cross-compilation. ARM64 (a64) is a notable new capability for Windows in devenv7.

* **devenv7** for Windows, host ARM64 (a64), targets a64+x64+x86, vc143 (VS 2022) can be downloaded from [here](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86.zip); or from command line:
wget https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86.zip
* **devenv7** for Windows, host x64, targets a64+x64+x86, vc143 (VS 2022) can be downloaded from [here](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86.zip); or from command line:
wget https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86.zip
* **devenv7** for Windows, host x86, targets a64+x64+x86, vc143 (VS 2022) can be downloaded from [here](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-x86-targets-a64-x64-x86.zip); or from command line:
wget https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-x86-targets-a64-x64-x86.zip

The following Windows downloads are optional and only needed for rebuilding or updating the development environment:

* **Downloads cache** archives contain the downloaded source and binary packages and are distributed separately for convenience, so the user can rebuild or update the environment without having to download the packages again:
  * Host a64: [dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86-downloads-cache.zip](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-a64-targets-a64-x64-x86-downloads-cache.zip)
  * Host x64: [dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86-downloads-cache.zip](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86-downloads-cache.zip)
  * Host x86: [dist-devenv7-windows-hostarch-x86-targets-a64-x64-x86-downloads-cache.zip](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/dist-devenv7-windows-hostarch-x86-targets-a64-x64-x86-downloads-cache.zip)
* **MSVC compiler toolchain package** contains the VS2022 vc143 VC 17.08 compiler toolchain and Windows SDK and is also optional and only available for convenience when one needs to rebuild or update the devenv7 Windows environment: [msvc-toolchain-17.08-and-sdk.zip](https://storage.swblocks.net/devenv/7/windows/vs2022-vc143-17.08/msvc-toolchain-17.08-and-sdk.zip)

## Setting up the development environment

All links above are either .zip file (for Windows) or a .gz tar file for Darwin / macOS and Linux. The paths in the archives are structured so that they can be extracted directly with the home directory as root on all platforms. Once extracted, the distribution will be located under **$(HOME)/swblocks** on Darwin / macOS and Linux, or **c:\\Users\\username\\swblocks** on Windows respectively. Each distribution archive includes a pre-generated **projects/make/ci-init-env.mk** file with the correct paths. If you extract the distribution with the home directory as root, you can simply copy this generated **ci-init-env.mk** file directly into your **projects/make** folder in the swblocks-baselib repository clone and it will work without any modifications. If you extract the distribution to a different location, you will need to adjust the paths in **ci-init-env.mk** accordingly.

The **ci-init-env.mk** file points the 3 _DIST_ roots to the place where you have extracted the development environment distribution.

E.g. if you have extracted the development environment into **%USERPROFILE%\\swblocks** then the **projects/make/ci-init-env.mk** can look the following way for Windows:

```make
# initialize the important env roots

DIST_ROOT_DEPS1 = /c/Users/$(USERNAME)/swblocks/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86
DIST_ROOT_DEPS2 = /c/Users/$(USERNAME)/swblocks/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86
DIST_ROOT_DEPS3 = /c/Users/$(USERNAME)/swblocks/dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86
```

Note that on Windows you should use UNIX style paths since we use GNU make (via MSYS2)

E.g. **projects/make/ci-init-env.mk** can look the following way for Darwin / macOS:

```make
# initialize the important env roots

DIST_ROOT_DEPS1 = $(HOME)/swblocks/dist-devenv7-darwin-24-a64
DIST_ROOT_DEPS2 = $(HOME)/swblocks/dist-devenv7-darwin-24-a64
DIST_ROOT_DEPS3 = $(HOME)/swblocks/dist-devenv7-darwin-24-a64
```

E.g. **projects/make/ci-init-env.mk** can look the following way for Linux:

```make
# initialize the important env roots

DIST_ROOT_DEPS1 = $(HOME)/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64
DIST_ROOT_DEPS2 = $(HOME)/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64
DIST_ROOT_DEPS3 = $(HOME)/swblocks/dist-devenv7-ub24-gcc1520-clang2010-x64
```

### Windows environment setup

An additional step which applies **only** to Windows is to configure the environment so that the necessary tools (GNU make, Git, Python, etc.) are available. On Linux / UNIX / Darwin these are typically available, or can be made available, in the OS, so this step is not necessary on these platforms.

**Recommended approach: setup-env scripts**

The recommended way to configure the Windows development environment is to use the setup-env scripts that are auto-generated as part of the distribution. Open a Command Prompt and run:

```
call %USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\scripts\ci\setup-env-x64.bat
```

This sets up all necessary PATH entries, compiler paths and environment variables for the specified target architecture. Three variants are available for each architecture (where {arch} is a64, x64, or x86):

* **setup-env-{arch}.bat** — Full environment: MSVC compiler, Windows SDK, Clang-CL, debuggers, Jom, NASM, Git, Python, MSYS2, plus INCLUDE/LIB/LIBPATH for the target architecture.
* **setup-env-nomsvc-{arch}.bat** — No MSVC compiler: debuggers, Jom, NASM (x64/x86 only), Git, Python, MSYS2. No compiler, SDK, or INCLUDE/LIB/LIBPATH paths. Useful when you only need the tools but not the compiler (the makefiles configure compiler paths automatically during builds).
* **setup-env-minimal-{arch}.bat** — Minimal: Git and MSYS2 only. Useful for running make and git commands only.

**Alternative approach: manual PATH entries**

Alternatively, you can manually add the following entries to the Path environment variable associated with your account permanently. These are the essential tools needed to build and run tests. Assuming the distribution is extracted to **%USERPROFILE%\\swblocks** (preferably in this order):

```
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\msys2\20251213\msys64\usr\bin
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\git\2.48.1\default\bin
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\python\3.14.2\default
```

If you are using WinDbg for debugging on Windows you can also optionally add the following entry to the Path associated with your account:

```
%USERPROFILE%\swblocks\dist-devenv7-windows-hostarch-x64-targets-a64-x64-x86\winsdk\10\default\Debuggers\x64
```

Note: The setup-env scripts (above) are the recommended approach as they also configure the MSVC compiler toolchain paths, which the makefiles handle automatically during builds but which are needed for interactive compiler use.

### Building and running tests

Once you unpack the development environment distribution and create the **projects/make/ci-init-env.mk** as per above you can now build the code and run tests by specifying the targets, etc. The unit test targets start with **utf_baselib** prefix (e.g. utf_baselib, utf_baselib_data) and the respective test targets start with **test_utf_baselib**. The other targets are the respective binary names and there are special targets too, just type make help for more information.

Here are some examples e.g.:

```make
make -k -j4
make -k -j4 VARIANT=release
make -k -j4 TOOLCHAIN=ccl16
make -k -j4 ARCH=x64
make utf_baselib
make test_utf_baselib_data
make -k -j4 && make -k -j4 test
make help
make -k -j4 && make -k -j4 test && make install
```

## Using GitHub and creating pull requests

This section of course will not cover general information about how to use Git and GitHub (there is plenty of information on the internet and GitHub site itself), but if you are new to Git it is highly recommended to read the following [link](https://www.sbf5.com/~cduan/technical/git) which is not the typical Git tutorial, but will help you understand Git conceptually. And of course for tutorials on the specifics, the likes of "getting started", "cheat sheets", etc, you can search on Google as there are plenty of those available on the internet.

Here are some details on how to use GitHub to open pull requests and contribute specifically to the swblocks-baselib library. First of course you will need to create an account on GitHub and then create your own private fork (on GitHub) of the repository off the master copy located in the JP Morgan Chase account area [here](https://github.com/jpmorganchase/swblocks-baselib). The way you can create a private fork of the repository is by first going to the [master copy link](https://github.com/jpmorganchase/swblocks-baselib) and then clicking the 'Fork' button in the top right corner. The reason you need to create a private fork is that the [master copy](https://github.com/jpmorganchase/swblocks-baselib) is locked down for write access and you can't directly make changes to it via push Git commands, but only via pull requests (from branches in your own private fork).

How do you want to organize the branches in your own private fork is up to your own preference, but it is recommended to pull directly from the master copy into your private branches in your fork and then when you have changes ready you can create pull requests from branches in your private for to master branch in the master copy fork.

In order to pull easily changes from the master copy repository it is recommended to add it as an additional remote where fetch is allowed, but push is disabled. You can accomplish this with the following Git commands:

```
git remote add --mirror=fetch public_origin https://github.com/jpmorganchase/swblocks-baselib.git
git remote set-url --push public_origin disabled
```

Now every time you want to pull changes from the master repository you can do this with the following Git command:

```
git pull public_origin master
```

If you want to do pull / push for your own private branches in your own private fork you of course can do this in the usual way:

```
git checkout -b mybranch
git push --set-upstream origin mybranch
git push
git pull origin master
```

Note also that it is recommended to not use pull requests in your own private fork (but use Git push / pull directly) as this will create extra 'pollution' with unnecessary commits for the private pull requests and these will eventually leak in the master repository as part of your official / public pull requests.
