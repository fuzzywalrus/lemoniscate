#!/usr/bin/env bash
#
# Lemoniscate Linux CLI release:
#   build x86_64 server binary via Docker -> strip -> zip as
#   lemoniscate-linux-x86_64-<VERSION>.zip
#
# Requires Docker running on the host.
#
# Usage: scripts/release-linux.sh [VERSION]
# VERSION defaults to CFBundleShortVersionString in resources/Info.plist.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

command -v docker >/dev/null 2>&1 || die "docker not found on PATH"
docker info >/dev/null 2>&1 || die "docker daemon not running"

export DOCKER_BUILDKIT=1

VERSION="${1:-$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' resources/Info.plist)}"
[[ -n "$VERSION" ]] || die "Could not determine VERSION"

ZIP_NAME="lemoniscate-linux-x86_64-${VERSION}.zip"
IMAGE_TAG="lemoniscate-build:${VERSION}"

log "Building Linux x86_64 binary in Docker (image: $IMAGE_TAG)"
docker build --platform linux/amd64 -t "$IMAGE_TAG" .

log "Extracting binary from image"
STAGE="$(mktemp -d)"
CID="$(docker create "$IMAGE_TAG")"
docker cp "$CID:/usr/local/bin/lemoniscate" "$STAGE/lemoniscate-linux-x86_64"
docker rm "$CID" >/dev/null

file "$STAGE/lemoniscate-linux-x86_64" | grep -q 'x86-64' \
    || die "Extracted binary is not x86_64"

log "Packaging $ZIP_NAME"
rm -f "$ZIP_NAME"
(cd "$STAGE" && zip -q "$REPO_ROOT/$ZIP_NAME" "lemoniscate-linux-x86_64")
rm -rf "$STAGE"

log "Done: $ZIP_NAME"
shasum -a 256 "$ZIP_NAME"
