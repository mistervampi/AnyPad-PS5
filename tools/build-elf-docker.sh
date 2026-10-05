#!/bin/sh
# Builds AnyPad PS5 in Docker. By default, downloads the current ps5-payload-dev
# SDK release inside the container; an extracted SDK directory can be supplied
# to build offline. The ELF and SHA-256 file are written under dist/.
#
#   tools/build-elf-docker.sh [path/to/extracted/ps5-payload-sdk]
set -eu

PROJ=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE="${ANYPAD_BUILD_IMAGE:-anypad-build}"
SDK_ARG="${1:-}"
VERSION="$(sed -n 's/^#define ANYPAD_VERSION "\(.*\)"/\1/p' "$PROJ/src/version.h" | tr -d '\r')"
[ -n "$VERSION" ] || { echo "no version in src/version.h" >&2; exit 1; }
OUT="dist/AnyPad-PS5-$VERSION.elf"

if ! command -v docker >/dev/null 2>&1; then
  echo "Docker is required. In WSL, start Docker Desktop and enable WSL integration." >&2
  exit 1
fi

mkdir -p "$PROJ/dist"
docker build --platform linux/amd64 -t "$IMAGE" -f "$PROJ/tools/Dockerfile.build-elf" "$PROJ"

set -- docker run --rm --platform linux/amd64 \
  -e OUT="$OUT" \
  -v "$PROJ:/work" \
  -w /work

if [ -n "$SDK_ARG" ]; then
  SDK=$(CDPATH= cd -- "$SDK_ARG" 2>/dev/null && pwd) || {
    echo "SDK directory does not exist: $SDK_ARG" >&2
    exit 1
  }
  if [ ! -f "$SDK/toolchain/prospero.mk" ] || [ ! -x "$SDK/bin/prospero-clang" ]; then
    echo "SDK directory must contain toolchain/prospero.mk and bin/prospero-clang: $SDK" >&2
    exit 1
  fi
  set -- "$@" -e SDK_MODE=mounted -v "$SDK:/opt/ps5-payload-sdk-ro:ro"
else
  set -- "$@" -e SDK_MODE=download
fi

set -- "$@" "$IMAGE" bash /work/tools/build-elf-container.sh

"$@"
cd "$PROJ"
sha256sum "$OUT" | tee "$OUT.sha256"
ls -lh "$OUT" "$OUT.sha256"
