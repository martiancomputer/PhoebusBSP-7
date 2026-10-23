#!/usr/bin/env python3
"""Exercise the actual shell state machines with synthetic ports and leases."""
import re
import struct
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / "sdk/rootfs"
leds = (SDK / "usr/sbin/phoebus-leds").read_text()
leases = (SDK / "usr/sbin/phoebus-led-lease").read_text()
for path in ("usr/sbin/phoebus-leds", "usr/sbin/phoebus-led-lease",
             "usr/share/udhcpc/default.script", "etc/s6/source/leds/run"):
    subprocess.run(["sh", "-n", str(SDK / path)], check=True)

parser = re.search(r"od -An -v -tu1 .*? \| awk '(.*?)' >", leases, re.S)[1]
diff = re.search(r"\nawk '(.*?)' \\\n", leases, re.S)[1]
mac = bytes.fromhex("020000000001")


def parse(written, remaining):
    binary = struct.pack(">QII6s20s2x", written, remaining, 0xC0000201, mac, b"test")
    octets = " ".join(str(b) for b in binary)
    return subprocess.check_output(["awk", parser], input=octets, text=True)


with tempfile.TemporaryDirectory(prefix="led-status-test-") as temp:
    old, new = Path(temp) / "old", Path(temp) / "new"

    def changed(before, after):
        old.write_text(before)
        new.write_text(after)
        return subprocess.check_output(["awk", diff, str(old), str(new)], text=True)

    first = parse(100000, 86400)
    assert first == "02:00:00:00:00:01 186400\n"
    assert changed("", first).strip() == "02:00:00:00:00:01", "first ACK"
    assert changed(first, parse(100100, 86300)) == "", "periodic rewrite is not ACK"
    assert changed(first, parse(100100, 86400)).strip() == "02:00:00:00:00:01", "renewal"
    assert parse(100000, 0) == "", "expired lease"

lan = re.search(r"\n(lan\(\) \{.*?\n\})\n\nvpn", leds, re.S)[1]
harness = r'''
STATE=/fixture
frame=0
led() { printf '%s %s\n' "$frame" "$2"; }
on() { led "$1" 255; }
off() { led "$1" 0; }
carrier() {
 case "$1" in
  eth0.2) return 0;;
  eth0.3) [ "$frame" -ge 25 ];;
  eth0.4) [ "$frame" -ge 3 ];;
  *) return 1;;
 esac
}
cut() { echo 10; }
cat() {
 case "$1" in
  /fixture/lease-2) if [ "$frame" -ge 40 ]; then echo 11; else echo 0; fi;;
  *) echo 0;;
 esac
}
sleep() {
 if [ "$1" = 0.125 ]; then
  frame=$((frame+1))
  [ "$frame" -lt 48 ] || exit 0
 fi
}
'''
result = subprocess.check_output(["sh", "-c", harness + lan + "\nlan\n"], text=True)
events = [tuple(map(int, row.split())) for row in result.splitlines()]
by_frame = {}
for frame, brightness in events:
    by_frame.setdefault(frame, []).append(brightness)
assert [by_frame[i][0] for i in range(3, 9)] == [255, 0, 255, 0, 255, 0]
assert [by_frame[i][0] for i in range(25, 29)] == [255, 0, 255, 0]
assert all(by_frame[i] == [255] for i in range(9, 25)), "existing port not replayed"
assert by_frame[40] == [0, 255], "ACK produces quick tick"
assert all(by_frame[i] == [255] for i in range(41, 48)), "ACK tick not repeated"
print("PASS: lease format, first ACK, renewal, periodic rewrite, expiry, newest-port preemption, numbered bursts, one-second hold, one DHCP tick")
