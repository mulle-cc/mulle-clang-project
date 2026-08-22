Create mulle-objc x86_64 compiler for **trixie, bookworm, and forky**. Just run the script.

> #### Harmless warning
> /home/nat/mono/mulle-clang-project/clang/lib/Sema/SemaDeclObjC.cpp:2916:10: warning: add explicit braces to avoid dangling else [-Wdangling-else]
> 2916 |          else
>

## Forky build (no sanitizers)

The forky deb must be built **without sanitizers** because `linux/scc.h` was
removed from kernel UAPI headers in Debian forky/sid. Pass:

```
CMAKEFLAGS="-DLLVM_ENABLE_RUNTIMES="
```

This disables compiler-rt entirely. The resulting deb has no ASan/TSan/etc.
This should be noted in release notes.

### Docker-based build (preferred for forky)

No need for a VM — use a `debian:testing` container:

```bash
mkdir -p /tmp/forky-output
docker run --rm --privileged --memory=32g --cpus=32 \
  -v /path/to/mulle-clang-cpack:/cpack:ro \
  -v /tmp/forky-output:/output \
  debian:testing bash -c '
ln -s /usr/bin/env /usr/local/bin/sudo
apt-get update -qq
apt-get install -y -qq git cmake ninja-build clang wget python3 lsb-release file linux-libc-dev
mkdir -p /build && cd /build
cp -r /cpack mulle-clang-cpack
VERSION="22.1.2.6" RC="" NINJAFLAGS="" CMAKEFLAGS="-DLLVM_ENABLE_RUNTIMES=" \
  ./mulle-clang-cpack/package-build clean download build verpack
cp *.deb /output/
'
```

Requirements: ~32 CPUs, 16GB+ RAM, ~25GB disk. Build takes ~12 minutes.

### ABI note (discovered 2026-08-01)

The Debian era boundary for `.deb` packages is determined by **shared library
sonames**, NOT by release dates. Ubuntu 25.10 (questing) onward imports from
Debian forky/sid and has `libxml2.so.16` instead of `.so.2`. The mapping in
`github-ci/install.sh` must reflect this. See the `update_muller_de` MRC task
instruction for the full verification procedure.

## Manual Usage

### Unix

#### Get git happening and clone cpack-mulle-clang:

``` bash
sudo apt-get install git sudo
git clone https://github.com/mulle-cc/mulle-clang-cpack.git
```

#### Build mulle-clang into a local opt folder:

Set `VERSION` appropriately:

``` bash
VERSION="17.0.6.0"
RC="" # e.g. -RC1
mkdir mono
cd mono
wget -O - "https://github.com/mulle-cc/mulle-clang-project/archive/${VERSION}${RC}.tar.gz" | tar xfz -
mv "mulle-clang-project-${VERSION}${RC}" mulle-clang-project
mkdir opt/mulle-clang-project
sudo ln -s "$PWD/opt/mulle-clang-project" "/opt/mulle-clang-project"
```

####  Build normally

``` bash
PREFIX="/opt" NAME="${VERSION}" ./mulle-clang-project/clang/bin/cmake-ninja.linux
```


#### Create .deb package and upload:

``` bash
cp ../cpack-mulle-clang/* .
chmod 755 generate-package
./generate-package
```
