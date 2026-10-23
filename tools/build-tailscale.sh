#!/bin/sh
# Optional RAM-only VPN binary, deliberately not installed in the initramfs.
set -eu
BSP=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE="$BSP/build/tailscale-src"
VERSION=v1.104.2
COMMIT=e26db7a5797683ec99ad1516c40b628c7f2dfd71
mkdir -p "$BSP/build"
if [ ! -d "$SOURCE/.git" ]; then
	git clone --depth 1 --branch "$VERSION" https://github.com/tailscale/tailscale.git "$SOURCE"
fi
[ "$(git -C "$SOURCE" rev-parse HEAD)" = "$COMMIT" ] || {
	echo "Unexpected Tailscale source revision; refusing to build" >&2; exit 1;
}
[ -z "$(git -C "$SOURCE" status --porcelain)" ] || {
	echo "Tailscale source has local changes; refusing an unverified build" >&2; exit 1;
}
cd "$SOURCE"
export CGO_ENABLED=0 GOTOOLCHAIN=auto
# ipnbus is necessary for interactive `tailscale up`, even in a small build.
TAGS=$(GOOS=linux GOARCH=amd64 go run ./cmd/featuretags --min \
	--add=osrouter,nattraversal,bakedroots,cli,ipnbus)
export TAGS GOOS=linux GOARCH=mips GOMIPS=softfloat
./build_dist.sh --strip -p "${JOBS:-4}" -o "$BSP/build/tailscaled-mips" ./cmd/tailscaled
file "$BSP/build/tailscaled-mips"
sha256sum "$BSP/build/tailscaled-mips"
