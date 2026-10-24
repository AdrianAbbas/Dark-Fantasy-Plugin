#!/usr/bin/env bash
# Rootless Debian 13 build prerequisites. Uses signed APT metadata and verified
# package downloads, then unpacks packages without changing the host system.
set -euo pipefail
veil_root="${VEIL_TOOLCHAIN:-/workspace/toolchain}"
if [[ "$(dpkg --print-architecture)" != amd64 ]] || ! rg -q '^VERSION_CODENAME=trixie$' /etc/os-release; then
    echo 'This rootless bootstrap targets Debian 13 amd64. See README for other systems.' >&2
    exit 1
fi
mkdir -p "$veil_root"/apt/{lists/partial,cache/archives/partial,config,empty} "$veil_root/sysroot" "$veil_root/packages"
cat > "$veil_root/apt/config/sources.list" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian trixie main
deb [signed-by=/usr/share/keyrings/debian-archive-keyring.gpg] https://deb.debian.org/debian-security trixie-security main
SOURCES
cat > "$veil_root/apt/apt.conf" <<CONFIG
Dir::Etc::main "-";
Dir::Etc::parts "$veil_root/apt/empty";
Dir::Etc::sourcelist "$veil_root/apt/config/sources.list";
Dir::Etc::sourceparts "$veil_root/apt/empty";
Dir::State::lists "$veil_root/apt/lists";
Dir::Cache "$veil_root/apt/cache";
Dir::State::status "/var/lib/dpkg/status";
APT::Get::List-Cleanup "0";
Debug::NoLocking "true";
CONFIG
export APT_CONFIG="$veil_root/apt/apt.conf"
/usr/bin/apt-get update
/usr/bin/apt-get --download-only -y --no-install-recommends install \
    libasound2-dev libfreetype6-dev libfontconfig1-dev libx11-dev \
    libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libgl1-mesa-dev xvfb xauth
(
    cd "$veil_root/packages"
    /usr/bin/apt-get download libasound2t64 libfreetype6 libfontconfig1 \
        libx11-6 libxau6 libxcb1 libxdmcp6 libxcomposite1 libxcursor1 \
        libxext6 libxfixes3 libxinerama1 libxrandr2 libxrender1 libgl1 \
        libglx0 libegl1 libopengl0 libglvnd0 libbz2-1.0 libuuid1 xauth
)
for veil_package in "$veil_root"/apt/cache/archives/*.deb "$veil_root"/packages/*.deb; do
    dpkg-deb -x "$veil_package" "$veil_root/sysroot"
done
python3 -m pip install --upgrade --target "$veil_root/python" cmake==4.4.4 ninja==1.13.2
{
    printf '# Source before configuring, building, or running VEIL in this rootless machine.\n'
    printf 'export VEIL_TOOLCHAIN=%q\n' "$veil_root"
    cat <<'ACTIVATE'
export PYTHONPATH="$VEIL_TOOLCHAIN/python${PYTHONPATH:+:$PYTHONPATH}"
export PATH="$VEIL_TOOLCHAIN/python/bin:$VEIL_TOOLCHAIN/sysroot/usr/bin:$PATH"
export PKG_CONFIG_PATH="$VEIL_TOOLCHAIN/sysroot/usr/lib/x86_64-linux-gnu/pkgconfig:$VEIL_TOOLCHAIN/sysroot/usr/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_SYSROOT_DIR="$VEIL_TOOLCHAIN/sysroot"
export CMAKE_PREFIX_PATH="$VEIL_TOOLCHAIN/sysroot/usr${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export CPATH="$VEIL_TOOLCHAIN/sysroot/usr/include:$VEIL_TOOLCHAIN/sysroot/usr/include/x86_64-linux-gnu${CPATH:+:$CPATH}"
export LIBRARY_PATH="$VEIL_TOOLCHAIN/sysroot/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$VEIL_TOOLCHAIN/sysroot/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
ACTIVATE
} > "$veil_root/activate.sh"
printf 'Ready. Run: source %q\n' "$veil_root/activate.sh"
