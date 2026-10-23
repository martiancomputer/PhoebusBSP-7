#!/bin/sh
# Run serial profiles on the board; never change passwords, network, or NAND.
set -eu
umask 077
ulimit -c 0
DIR="${1:-/run/password-bench}"
BIN="$DIR/password-bench"
test -x "$BIN"
mkdir "$DIR/running" 2>/dev/null || { echo 'Benchmark already running or needs review'; exit 1; }
trap 'rmdir "$DIR/running" 2>/dev/null || :' EXIT
"$BIN" selftest > "$DIR/selftest.txt"
"$BIN" inventory > "$DIR/inventory.txt"
cat /proc/crypto > "$DIR/providers.txt"
cat /proc/meminfo > "$DIR/memory-before.txt"
: > "$DIR/results.jsonl"
: > "$DIR/run.log"
: > "$DIR/status.tsv"
profile() {
	printf 'start %s\n' "$*"
	printf 'start %s uptime=' "$*" >> "$DIR/status.tsv"
	cut -d' ' -f1 /proc/uptime >> "$DIR/status.tsv"
	set +e
	nice -n 10 timeout -s TERM -k 5 90 "$BIN" "$@" >> "$DIR/results.jsonl" 2>> "$DIR/run.log"
	status=$?
	set -e
	printf 'end %s exit=%s\n' "$*" "$status" | tee -a "$DIR/status.tsv"
	# Memory-reserve refusals skip a profile. A temperature trip stops the suite.
	if [ "$status" -eq 78 ]; then echo 'Temperature guard stopped suite'; exit 78; fi
	if [ "$status" -ne 0 ] && [ "$status" -ne 77 ]; then echo 'Profile failed; stopping suite'; exit "$status"; fi
	sleep 2
}
# OWASP minimum-equivalent memory/time alternatives, plus resource comparisons.
profile argon2id 5 7168 1 1 3
profile argon2id 2 19456 1 1 3
profile argon2id 2 32768 1 1 3
profile argon2id 2 19456 4 1 3
profile argon2id 2 19456 1 2 3
# RFC 9106's lower-memory profile: skipped if current free headroom is unsafe.
profile argon2id 3 65536 4 1 3
profile bcrypt 10 0 1 1 3
profile bcrypt 11 0 1 1 3
profile bcrypt 12 0 1 1 3
# 100k is a timing reference, not the proposed production work factor.
profile pbkdf2 100000 0 1 1 3
profile pbkdf2 600000 0 1 1 3
profile pbkdf2 1200000 0 1 1 3
cat /proc/meminfo > "$DIR/memory-after.txt"
echo COMPLETE > "$DIR/complete"
