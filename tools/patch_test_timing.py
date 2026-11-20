#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_test_timing.py

Fixes verify_registers.py and send_frames.py:
  1. send_frames.py: pause between frames 50ms -> 120ms (UART resync).
  2. verify_registers.py: remove 0x001F from the "hole" list
     (it is cnt_fall_events in in_map, not a hole).
No .bak files are created.
"""
import os, re, sys

SF = "tools/send_frames.py"
VR = "tools/verify_registers.py"

for p in (SF, VR):
    if not os.path.exists(p):
        print("[!] missing %s" % p); sys.exit(1)

def rd(p):
    with open(p, encoding="utf-8", errors="ignore") as f: return f.read()

def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f: f.write(s)

# 1. send_frames.py: bump pause to 120ms
s = rd(SF)
s2 = re.sub(r'time\.sleep\(0\.05\)', 'time.sleep(0.12)', s)
if s2 != s:
    wr(SF, s2)
    print("[+] send_frames.py: pause 50ms -> 120ms")
else:
    print("[=] send_frames.py: pause already >=120ms")

# 2. verify_registers.py: remove 0x001F from hole list
v = rd(VR)
# Pattern: for a in (0x0000, 0x001F, 0x003A, 0x003F, 0x0059, 0x00FF, 0x0F00):
v2 = v.replace(
    "(0x0000, 0x001F, 0x003A, 0x003F, 0x0059, 0x00FF, 0x0F00)",
    "(0x0000, 0x003A, 0x003F, 0x0059, 0x00FF, 0x0F00)"
)
if v2 != v:
    wr(VR, v2)
    print("[+] verify_registers.py: removed 0x001F from hole list")
else:
    print("[=] verify_registers.py: 0x001F already not in hole list")

print("")
print("Re-run:")
print("  py tools/send_frames.py COM4 --slave 100")
print("  py tools/verify_registers.py COM4 --slave 100")
print("")
print("Expect: working_hours may still differ (volatile), other FAILs should go away.")