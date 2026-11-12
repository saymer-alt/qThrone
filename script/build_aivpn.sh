#!/usr/bin/env bash
# aivpn-client (infosave2007/aivpn) for AIVPN profiles, in its SOCKS5 proxy mode. Rawline's
# patch (script/aivpn) adds the private launch file, the per-launch SOCKS5 credentials and a
# manifest without the administrator request, which only the original's TUN mode needs.
set -euo pipefail

revision=80deb78b42edc96684f703dbe1fa0c196181134f
case "$RUNNER_OS:$ARCH" in
    Windows:x64) target=x86_64-pc-windows-msvc ;;
    Windows:arm64) target=aarch64-pc-windows-msvc ;;
    Windows:x86) target=i686-pc-windows-msvc ;;
    macOS:x86_64) target=x86_64-apple-darwin ;;
    macOS:arm64) target=aarch64-apple-darwin ;;
    Linux:x86_64) target=x86_64-unknown-linux-gnu ;;
    Linux:aarch64) target=aarch64-unknown-linux-gnu ;;
    *) echo 'Unsupported AIVPN build target' >&2; exit 1 ;;
esac

python3 script/fetch_pinned_source.py https://github.com/infosave2007/aivpn.git "$revision" aivpn-source
python3 script/aivpn/patch_aivpn.py aivpn-source
rustup toolchain install 1.97.1 --profile minimal
rustup target add --toolchain 1.97.1 "$target"
if [ "$RUNNER_OS" = Windows ]; then
    # Git's own link.exe would shadow MSVC's linker.
    msvc_bin="$(cygpath -u "$VCToolsInstallDir")/bin/Host${VSCMD_ARG_HOST_ARCH}/${VSCMD_ARG_TGT_ARCH}"
    test -f "$msvc_bin/link.exe"
    export PATH="$msvc_bin:$PATH"
fi
if [ "$RUNNER_OS" = macOS ]; then export MACOSX_DEPLOYMENT_TARGET=10.15; fi
cargo +1.97.1 build --release --locked --target "$target" --manifest-path aivpn-source/Cargo.toml -p aivpn-client --bin aivpn-client
mkdir -p csqtt-artifact
extension=''
if [ "$RUNNER_OS" = Windows ]; then extension=.exe; fi
cp "aivpn-source/target/$target/release/aivpn-client$extension" "csqtt-artifact/aivpn-client$extension"
cp aivpn-source/LICENSE csqtt-artifact/AIVPN-LICENSE
printf '%s\n' 'Original source: https://github.com/infosave2007/aivpn' "Revision: $revision" 'Desktop integration: script/aivpn in Rawline' > csqtt-artifact/AIVPN-SOURCE
tar -czf csqtt-artifact/aivpn-source.tar.gz --exclude=target --exclude=.git -C aivpn-source .
