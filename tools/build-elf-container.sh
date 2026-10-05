#!/bin/bash
set -euo pipefail

if [[ "$SDK_MODE" == mounted ]]; then
  cp -a /opt/ps5-payload-sdk-ro /opt/ps5-payload-sdk
else
  echo "Downloading the latest ps5-payload-dev/sdk release..."
  curl -fL --retry 3 \
    https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip \
    -o /tmp/ps5-payload-sdk.zip
  mkdir -p /tmp/ps5-payload-sdk-unpacked
  unzip -q /tmp/ps5-payload-sdk.zip -d /tmp/ps5-payload-sdk-unpacked
  sdk_mk="$(find /tmp/ps5-payload-sdk-unpacked -type f -path '*/toolchain/prospero.mk' -print -quit)"
  if [[ -z "$sdk_mk" ]]; then
    echo "Downloaded SDK does not contain toolchain/prospero.mk" >&2
    exit 1
  fi
  sdk_root="${sdk_mk%/toolchain/prospero.mk}"
  cp -a "$sdk_root" /opt/ps5-payload-sdk
fi

export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
if [[ ! -f "$PS5_PAYLOAD_SDK/toolchain/prospero.mk" ||
      ! -x "$PS5_PAYLOAD_SDK/bin/prospero-clang" ]]; then
  echo "SDK is missing toolchain/prospero.mk or bin/prospero-clang" >&2
  exit 1
fi

# The SDK wrappers query llvm-config for the host LLVM installation.
printf '#!/bin/sh\ncase "$1" in --bindir) echo /usr/lib/llvm-18/bin;; --version) echo 18.1.3;; *) exit 1;; esac\n' \
  > /usr/local/bin/llvm-config
chmod +x /usr/local/bin/llvm-config

cc="$PS5_PAYLOAD_SDK/bin/prospero-clang"
mkdir -p build/elf
objects=()
for source in util log crc32 smp_crypto profiles generic host le web web_page config evstream hotkey lock netinfo usb_desc hci_usb ps5_power ps5_ui ps5_apps ps5_sysinfo launcher icon_data ps5_vpad ps5_main; do
  object="build/elf/$source.o"
  "$cc" -std=c11 -Wall -Wextra -Werror -O2 -Isrc -c "src/$source.c" -o "$object"
  objects+=("$object")
done

"$cc" -o "$OUT" "${objects[@]}" \
  -lScePad -lSceUserService -lSceSystemService -lSceAppInstUtil -ldl
