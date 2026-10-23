#!/bin/sh
# Build a disposable native password-KDF benchmark, not production auth code.
set -eu
BSP=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORK="$BSP/build/crypto-bench"
SRC="$WORK/argon2-src"
ARGON_COMMIT=f57e61e19229e23c4445b85494dbf7c07de721cb
mkdir -p "$WORK"
if [ ! -d "$SRC/.git" ]; then
	git clone https://github.com/P-H-C/phc-winner-argon2.git "$SRC"
	git -C "$SRC" checkout --detach "$ARGON_COMMIT"
fi
[ "$(git -C "$SRC" rev-parse HEAD)" = "$ARGON_COMMIT" ] || exit 1
[ -z "$(git -C "$SRC" status --porcelain)" ] || exit 1
CC="${CC:-$BSP/toolchain/mips32--glibc--stable-2025.08-1/bin/mips-buildroot-linux-gnu-gcc}"
STAGING="$BSP/build/userspace/staging"
"$CC" -O3 -std=c11 -Wall -Wextra -Werror -Wno-error=type-limits -pthread -static \
	-I"$SRC/include" -I"$SRC/src" -I"$STAGING/include" \
	"$BSP/tools/password-bench.c" "$SRC/src/argon2.c" "$SRC/src/core.c" \
	"$SRC/src/blake2/blake2b.c" "$SRC/src/thread.c" "$SRC/src/encoding.c" "$SRC/src/ref.c" \
	"$STAGING/lib/libcrypt.a" "$STAGING/lib/libcrypto.a" -ldl -latomic \
	-o "$WORK/password-bench"
STRIP="${STRIP:-$BSP/toolchain/mips32--glibc--stable-2025.08-1/bin/mips-buildroot-linux-gnu-strip}"
"$STRIP" --strip-unneeded "$WORK/password-bench"
file "$WORK/password-bench"
sha256sum "$WORK/password-bench"
