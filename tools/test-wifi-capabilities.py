#!/usr/bin/env python3
"""Reproduce the zero-MIB registration error and test the actual HT switch."""
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VENDOR = ROOT / 'sdk/vendor/realtek-wireless/rtl8192cd'
SOURCE = ROOT / 'overlay/drivers/net/wireless/realtek/rtl8192cd'
cfg = (SOURCE / '8192cd_cfg80211.c').read_text()
osdep = (SOURCE / '8192cd_osdep.c').read_text()
util = (VENDOR / 'core/8192cd_core_util.h').read_text()
enums = (VENDOR / 'cmn_info_file/rtw_sta_info.h').read_text()


def block(text, offset):
    start = text.index('{', offset)
    depth = 1
    end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


assert osdep.index('realtek_cfg80211_init(rtk,priv);') < osdep.index('\n\tset_mib_default(priv);')
assert 'int rf_mode = priv->pshare->phw->MIMO_TR_hw_support;' in cfg
assert 'wiphy_new(&realtek_cfg80211_ops, sizeof(struct realtek_wiphy_private))' in cfg
mode_function = 'static int get_rf_mimo_mode(struct rtl8192cd_priv *priv)\n' + block(
    util, util.index('static __inline__ int get_rf_mimo_mode'))
switch = 'switch (rf_mode) ' + block(cfg, cfg.index('switch (rf_mode)'))
enum = 'enum rf_type ' + block(enums, enums.index('enum rf_type')) + ';\n'
program = r'''
#include <assert.h>
#include <string.h>
#define IEEE80211_BAND_2GHZ 0
#define cpu_to_le16(x) (x)
#define NDEBUG(...) ((void)0)
struct hw { int MIMO_TR_hw_support; };
struct share { struct hw *phw; };
struct mib { struct { int MIMO_TR_mode; } dot11RFEntry; };
struct rtl8192cd_priv { struct share *pshare; struct mib *pmib; };
struct band { struct { struct { unsigned short rx_highest; unsigned char rx_mask[10]; } mcs; } ht_cap; };
struct wiphy { struct band *bands[1]; };
'''
program += enum + mode_function
program += '\nstatic void set_caps(struct wiphy *wiphy, int rf_mode) {\n' + switch + '\n}\n'
program += r'''
int main(void) {
 struct hw hw = {.MIMO_TR_hw_support = RF_2T2R};
 struct share share = {.phw = &hw};
 struct mib mib = {0};
 struct rtl8192cd_priv priv = {.pshare=&share, .pmib=&mib};
 struct band band = {0};
 struct wiphy wiphy = {.bands={&band}};
 assert(get_rf_mimo_mode(&priv) == RF_1T1R); /* old registration bug */
 set_caps(&wiphy, hw.MIMO_TR_hw_support);
 assert(band.ht_cap.mcs.rx_mask[0] == 255 && band.ht_cap.mcs.rx_mask[1] == 255);
 assert(band.ht_cap.mcs.rx_mask[2] == 0 && band.ht_cap.mcs.rx_highest == 300);
 mib.dot11RFEntry.MIMO_TR_mode = RF_2T2R;
 assert(get_rf_mimo_mode(&priv) == RF_2T2R); /* later runtime looks correct */
 set_caps(&wiphy, RF_1T1R);
 assert(band.ht_cap.mcs.rx_mask[0] == 255 && band.ht_cap.mcs.rx_mask[1] == 0);
 set_caps(&wiphy, RF_2T2R);
 assert(band.ht_cap.mcs.rx_mask[1] == 255); /* no inherited 1T mask */
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wifi-capability-test-') as temp:
    code = Path(temp) / 'test.c'
    exe = Path(temp) / 'test'
    code.write_text(program)
    subprocess.run(['cc', '-Wall', '-Werror', str(code), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: initialization order reproduced; zero policy misreports 1T; probed 2T advertises MCS0–15; 1T/2T transitions reset mask')
