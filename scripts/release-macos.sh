#!/usr/bin/env bash
#
# Lemoniscate macOS release pipeline:
#   build universal (x86_64 + arm64) -> sign -> DMG -> notarize -> staple
#
# Reads credentials from .env at repo root:
#   APPLE_ID=you@example.com
#   APPLE_PASSWORD=app-specific-password
#   APPLE_TEAM_ID=XXXXXXXXXX
#   APPLE_SIGNING_IDENTITY=Developer ID Application: Name (TEAMID)
#
# Usage: scripts/release-macos.sh [VERSION]
# VERSION defaults to CFBundleShortVersionString in resources/Info.plist.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

log() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# --- Load .env ---
[[ -f .env ]] || die ".env not found at repo root"
set -a; source .env; set +a
: "${APPLE_ID:?APPLE_ID missing from .env}"
: "${APPLE_PASSWORD:?APPLE_PASSWORD missing from .env}"
: "${APPLE_TEAM_ID:?APPLE_TEAM_ID missing from .env}"
: "${APPLE_SIGNING_IDENTITY:?APPLE_SIGNING_IDENTITY missing from .env}"

# --- Version ---
VERSION="${1:-$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' resources/Info.plist)}"
[[ -n "$VERSION" ]] || die "Could not determine VERSION"
log "Releasing Lemoniscate $VERSION"

DMG_NAME="Lemoniscate-${VERSION}.dmg"
VOL_NAME="Lemoniscate ${VERSION}"
ENTITLEMENTS="resources/entitlements.plist"
APP_BUNDLE="Lemoniscate.app"

# --- Tool checks ---
for tool in codesign xcrun hdiutil lipo /usr/libexec/PlistBuddy curl tar make; do
    command -v "$tool" >/dev/null 2>&1 || [[ -x "$tool" ]] || die "Missing required tool: $tool"
done

# --- libyaml universal ---
LIBYAML_VERSION="0.2.5"
LIBYAML_URL="https://pyyaml.org/download/libyaml/yaml-${LIBYAML_VERSION}.tar.gz"
UNIV_DIR="$REPO_ROOT/build/libyaml-universal"
X86_LIB="$UNIV_DIR/x86_64/libyaml.a"
ARM_LIB="$UNIV_DIR/arm64/libyaml.a"

build_libyaml_arch() {
    local arch="$1" min="$2" host="$3" outdir="$4"
    local src="$REPO_ROOT/build/libyaml-src/yaml-${LIBYAML_VERSION}"
    log "Building libyaml for $arch (min macOS $min)"
    cd "$src"
    make distclean >/dev/null 2>&1 || true
    CC="cc" \
    CFLAGS="-arch $arch -mmacosx-version-min=$min -O2" \
    LDFLAGS="-arch $arch -mmacosx-version-min=$min" \
        ./configure --host="$host" --disable-shared --enable-static \
            --prefix="$src/_install-$arch" >/tmp/libyaml-configure-$arch.log 2>&1 \
        || { cat /tmp/libyaml-configure-$arch.log; die "libyaml configure failed for $arch"; }
    make -j4 >/tmp/libyaml-build-$arch.log 2>&1 \
        || { tail -60 /tmp/libyaml-build-$arch.log; die "libyaml build failed for $arch"; }
    make install >/dev/null
    mkdir -p "$outdir"
    cp "$src/_install-$arch/lib/libyaml.a" "$outdir/libyaml.a"
    cd "$REPO_ROOT"
}

if [[ ! -f "$X86_LIB" || ! -f "$ARM_LIB" ]]; then
    log "Fetching libyaml $LIBYAML_VERSION source"
    mkdir -p build/libyaml-src
    cd build/libyaml-src
    if [[ ! -f "yaml-${LIBYAML_VERSION}.tar.gz" ]]; then
        curl -fsSL "$LIBYAML_URL" -o "yaml-${LIBYAML_VERSION}.tar.gz"
    fi
    rm -rf "yaml-${LIBYAML_VERSION}"
    tar xzf "yaml-${LIBYAML_VERSION}.tar.gz"
    cd "$REPO_ROOT"
    [[ -f "$X86_LIB" ]] || build_libyaml_arch x86_64 10.11 x86_64-apple-darwin "$UNIV_DIR/x86_64"
    [[ -f "$ARM_LIB" ]] || build_libyaml_arch arm64  11.0  aarch64-apple-darwin "$UNIV_DIR/arm64"
fi

log "Verifying libyaml slice arches"
file "$X86_LIB" | grep -q 'x86_64'  || die "$X86_LIB is not x86_64"
file "$ARM_LIB" | grep -q 'arm64'   || die "$ARM_LIB is not arm64"

# --- Per-arch app builds ---
BIN_GUI="Lemoniscate"
BIN_SERVER="lemoniscate-server"
BIN_COMPAT="mobius-hotline-server"
ARCH_BUILDS="$REPO_ROOT/build/app-arches"
rm -rf "$ARCH_BUILDS"
mkdir -p "$ARCH_BUILDS/x86_64" "$ARCH_BUILDS/arm64"

build_app_arch() {
    local arch="$1" min="$2" yaml_lib="$3" out="$4"
    log "Building Lemoniscate.app for $arch (min macOS $min)"
    make clean >/dev/null
    make app APP_SKIP_ARCH_CHECK=1 \
        CFLAGS="-std=c11 -Wall -Wextra -pedantic -O2 -arch $arch -mmacosx-version-min=$min -I./include -I/usr/local/include -DTARGET_OS_MAC=1" \
        OBJCFLAGS="-Wall -Wextra -O2 -arch $arch -mmacosx-version-min=$min -I./include -I/usr/local/include -DTARGET_OS_MAC=1" \
        LDFLAGS="-arch $arch -framework CoreFoundation -framework Foundation -framework Security -framework CoreServices -lpthread" \
        GUI_LDFLAGS="-arch $arch -framework AppKit -framework Foundation -framework CoreFoundation" \
        YAML_LDFLAGS="$yaml_lib" >/tmp/lemoniscate-build-$arch.log 2>&1 \
        || { tail -80 /tmp/lemoniscate-build-$arch.log; die "app build failed for $arch"; }
    cp "$APP_BUNDLE/Contents/MacOS/$BIN_GUI"    "$out/$BIN_GUI"
    cp "$APP_BUNDLE/Contents/MacOS/$BIN_SERVER" "$out/$BIN_SERVER"
    cp "$APP_BUNDLE/Contents/MacOS/$BIN_COMPAT" "$out/$BIN_COMPAT"
}

build_app_arch x86_64 10.11 "$X86_LIB" "$ARCH_BUILDS/x86_64"
build_app_arch arm64  11.0  "$ARM_LIB" "$ARCH_BUILDS/arm64"

# --- Lipo into universal ---
log "Assembling universal Lemoniscate.app"
rm -rf "$APP_BUNDLE"
mkdir -p "$APP_BUNDLE/Contents/MacOS" "$APP_BUNDLE/Contents/Resources"
cp resources/Info.plist "$APP_BUNDLE/Contents/Info.plist"
cp resources/Lemoniscate.icns "$APP_BUNDLE/Contents/Resources/"
[[ -f resources/default-banner.jpg ]] && \
    cp resources/default-banner.jpg "$APP_BUNDLE/Contents/Resources/"
for bin in "$BIN_GUI" "$BIN_SERVER" "$BIN_COMPAT"; do
    lipo -create \
        "$ARCH_BUILDS/x86_64/$bin" \
        "$ARCH_BUILDS/arm64/$bin" \
        -output "$APP_BUNDLE/Contents/MacOS/$bin"
    chmod +x "$APP_BUNDLE/Contents/MacOS/$bin"
    log "  $bin  ->  $(lipo -archs "$APP_BUNDLE/Contents/MacOS/$bin")"
done

# --- Codesign (deep, hardened runtime) ---
log "Signing Lemoniscate.app"
SIGN_COMMON=(--force --options runtime --timestamp --sign "$APPLE_SIGNING_IDENTITY")
# Sign each Mach-O inside first, bundle last.
for bin in "$BIN_SERVER" "$BIN_COMPAT"; do
    codesign "${SIGN_COMMON[@]}" --entitlements "$ENTITLEMENTS" \
        "$APP_BUNDLE/Contents/MacOS/$bin"
done
codesign "${SIGN_COMMON[@]}" --entitlements "$ENTITLEMENTS" \
    "$APP_BUNDLE/Contents/MacOS/$BIN_GUI"
codesign "${SIGN_COMMON[@]}" --entitlements "$ENTITLEMENTS" "$APP_BUNDLE"

log "Verifying signature"
codesign --verify --deep --strict --verbose=2 "$APP_BUNDLE"
spctl --assess --type execute --verbose=4 "$APP_BUNDLE" 2>&1 | head -3 || true

# --- DMG ---
log "Building $DMG_NAME"
rm -f "$DMG_NAME"
STAGING="$(mktemp -d)"
cp -R "$APP_BUNDLE" "$STAGING/"
ln -s /Applications "$STAGING/Applications"
hdiutil create -volname "$VOL_NAME" -srcfolder "$STAGING" \
    -ov -format UDZO "$DMG_NAME" >/dev/null
rm -rf "$STAGING"

log "Signing DMG"
codesign --force --timestamp --sign "$APPLE_SIGNING_IDENTITY" "$DMG_NAME"

# --- Notarize ---
log "Submitting to Apple notary service (this can take several minutes)"
xcrun notarytool submit "$DMG_NAME" \
    --apple-id "$APPLE_ID" \
    --password "$APPLE_PASSWORD" \
    --team-id "$APPLE_TEAM_ID" \
    --wait

log "Stapling notarization ticket"
xcrun stapler staple "$DMG_NAME"
xcrun stapler validate "$DMG_NAME"

log "Done: $DMG_NAME"
shasum -a 256 "$DMG_NAME"
