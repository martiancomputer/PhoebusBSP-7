#!/usr/bin/env python3
"""Check the compiled board LED wiring and software-PWM polarity offline."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "build/linux-7.3"
DTB = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    KERNEL / "arch/mips/boot/dts/realtek/rtl9607c_engboard.dtb")


def get(node, prop, kind="x"):
    value = subprocess.check_output(["fdtget", "-t", kind, str(DTB), node, prop], text=True).strip()
    return value if kind == "s" else [int(cell, 16) for cell in value.split()]


def children(node):
    return subprocess.check_output(["fdtget", "-l", str(DTB), node], text=True).split()


expected = {"power": (0, 0), "wlan2g": (0, 1), "wlan5g": (0, 25),
            "lan": (0, 16), "internetgreen": (0, 12),
            "internetorange": (1, 2), "wps": (0, 15)}
banks = {}
for bank in (0, 1):
    node = f"/gpio-controller@1b000038/bank@{bank}"
    banks[get(node, "phandle")[0]] = bank
providers = {}
for name in children("/"):
    if name.startswith("pwm-"):
        node = "/" + name
        assert get(node, "compatible", "s") == "pwm-gpio"
        assert get(node, "#pwm-cells") == [3]
        handle, offset, gpio_flags = get(node, "gpios")
        assert gpio_flags == 0, "GPIO must not invert again after PWM polarity"
        providers[get(node, "phandle")[0]] = (banks[handle], offset)
assert len(providers) == 7
assert get("/leds", "compatible", "s") == "pwm-leds"
seen = {}
for name in children("/leds"):
    node = "/leds/" + name
    label = get(node, "label", "s")
    handle, channel, period, polarity = get(node, "pwms")
    assert (channel, period, polarity) == (0, 5000000, 1)
    assert get(node, "max-brightness") == [255]
    assert get(node, "default-state", "s") == ("on" if label == "power" else "off")
    assert label not in seen
    seen[label] = providers[handle]
assert seen == expected
assert len(set(seen.values())) == 7
for label, pin in (("reset", 2), ("wps", 3)):
    handle, offset, flags = get("/gpio-keys-polled/" + label, "gpios")
    assert (banks[handle], offset, flags) == (0, pin, 1)
    assert (0, pin) not in seen.values()

config = (KERNEL / ".config").read_text()
for symbol in ("HIGH_RES_TIMERS", "PWM", "PWM_GPIO", "LEDS_PWM",
               "LEDS_TRIGGER_TIMER", "LEDS_TRIGGER_PATTERN"):
    assert f"CONFIG_{symbol}=y\n" in config, symbol

# Exercise the actual upstream toggle function. Electrical high must mean
# off, electrical low full brightness, and endpoint duty must stop callbacks.
source = (KERNEL / "drivers/pwm/pwm-gpio.c").read_text()
function = re.search(r"static u64 pwm_gpio_toggle\([^\n]+\)\n\{.*?\n\}", source, re.S)[0]
program = r'''
#include <stdbool.h>
#include <assert.h>
typedef unsigned long long u64;
#define PWM_POLARITY_INVERSED 1
struct pwm_state { u64 period, duty_cycle; unsigned polarity; };
struct pwm_gpio { struct pwm_state state; void *gpio; bool level, running; };
static bool pin;
static void gpiod_set_value(void *gpio, bool value) { pin = value; }
'''
program += function
program += r'''
int main(void) {
    struct pwm_gpio p = {.state = {.period = 5000000, .polarity = 1}};
    assert(pwm_gpio_toggle(&p, false) == 0 && pin && !p.running);
    p.state.duty_cycle = p.state.period;
    assert(pwm_gpio_toggle(&p, true) == 0 && !pin && !p.running);
    p.state.duty_cycle = p.state.period * 128 / 255;
    assert(pwm_gpio_toggle(&p, true) == p.state.duty_cycle && !pin && p.running);
    assert(pwm_gpio_toggle(&p, false) == p.state.period - p.state.duty_cycle && pin);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="led-pwm-check-") as temporary:
    code = Path(temporary) / "check.c"
    executable = Path(temporary) / "check"
    code.write_text(program)
    subprocess.run(["cc", str(code), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: seven stock LED pins, active-low PWM, 200 Hz, 0..255, button isolation, endpoint timers stop")
