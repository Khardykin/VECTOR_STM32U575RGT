#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_p8.py — Fix chunk size to fit full USB packets (no latency tail).
No firmware changes. No UART timing changes.
Run from project root: py apply_p8.py
"""
import os
from pathlib import Path

ROOT = Path(".")
TARGET = ROOT / "tools" / "test_flash_write.py"

if not TARGET.exists():
    print(f"[SKIP] {TARGET} not found")
    exit(1)

print(f"Patching {TARGET}...")

# Read file
with open(TARGET, "r", encoding="utf-8") as f:
    content = f.read()

# Replace default chunk size logic
# We want max chunk such that (chunk + 9) is multiple of 64 OR <= 64
# Best balance: Chunk 247 -> Frame 256 bytes (4 packets)
# Or Chunk 119 -> Frame 128 bytes (2 packets)
# Let's use 247 for speed, but add safety check in code.

old_arg = 'ap.add_argument("--chunk",type=int,default=256)'
new_arg = 'ap.add_argument("--chunk",type=int,default=247) # Max for 4 full USB packets (256B frame)'

if old_arg in content:
    content = content.replace(old_arg, new_arg)
    print("  [OK] Default chunk changed to 247")
else:
    # Try finding generic line if format differs slightly
    import re
    pattern = r'(--chunk.*default=\d+)'
    match = re.search(pattern, content)
    if match:
        print(f"  [WARN] Found existing arg: {match.group(1)}. Replacing manually recommended.")
        # Simple replace for common cases
        content = re.sub(r'default=\d+', 'default=247', content)
        print("  [OK] Default chunk forced to 247 via regex")
    else:
        print("  [ERROR] Could not find --chunk argument definition")
        exit(1)

# Add validation logic right after parsing args
validation_code = """
    # Validate chunk size against USB packet boundaries
    # Frame length = chunk + 9. Must be <= 64*k to avoid partial packet delay > 10ms timeout
    frame_len = a.chunk + 9
    if frame_len % 64 != 0 and frame_len > 64:
        print(f"WARNING: Frame length {frame_len} is not multiple of 64. Last packet will have tail ({frame_len % 64} bytes).")
        print(f"         This may cause timeouts with CV_RX_TIMEOUT_MS=10. Consider using chunk={247} or {119}.")
        # Auto-adjust to nearest safe value below current
        while (a.chunk + 9) % 64 != 0 and a.chunk > 55:
            a.chunk -= 1
        print(f"         Adjusted chunk to {a.chunk} (Frame len {a.chunk+9}).")
"""

# Insert validation after 'a=ap.parse_args()'
insert_marker = "    a=ap.parse_args()\n"
if insert_marker in content:
    content = content.replace(insert_marker, insert_marker + validation_code)
    print("  [OK] Added USB packet alignment validation")
else:
    print("  [WARN] Could not find parse_args marker. Validation skipped.")

# Write back
with open(TARGET, "w", encoding="utf-8") as f:
    f.write(content)

print("\nDone. Next steps:")
print("1. Ensure device is ON (sys_state.power != DEVICE_TURNED_OFF)")
print("2. Run test:")
print("   py tools/test_flash_write.py COM4 --slave 100 --file tools/sounds.bin --no-erase")
print("(It will automatically use chunk 247 which fits 4 full USB packets)")