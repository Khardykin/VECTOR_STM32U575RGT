#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
patch_slave_addr.py

Adds Modbus slave-address filtering to parsing_command(), matching the
reference design:
    address = ((SerialHi << 16) | SerialLo) % 100;  if 0 -> 100
    accept frame if mas[0] == address OR mas[0] == 0x00 OR mas[0] == 0xFF
    otherwise stay silent (return 0 -> no answer is sent)

No .bak files are created.
"""
import os, re, sys

CV = "Core/VectorLib/Src/Command_vector.c"

if not os.path.exists(CV):
    print("[!] missing %s - run from project root" % CV); sys.exit(1)

def rd(p):
    with open(p, encoding="utf-8", errors="ignore") as f: return f.read()

def wr(p, s):
    with open(p, "w", encoding="utf-8", newline="") as f: f.write(s)

c = rd(CV)

# ---- 1. slave address helper --------------------------------------------
HELPER = r'''
/* ==========================================================================
 * Modbus slave address.
 *
 * Derived from the device serial number exactly like the reference design:
 *     address = ((SerialHi << 16) | SerialLo) % 100,  0 -> 100
 * Frames addressed to another slave are ignored (the receiver stays silent).
 * Broadcast addresses 0x00 and 0xFF are always accepted.
 * ========================================================================== */
static uint16_t cv_slave_addr(void)
{
  uint32_t sn = ((uint32_t)Sns_Cfg_struct.Config_common.SerialHi << 16) |
                (uint32_t)Sns_Cfg_struct.Config_common.SerialLo;
  uint16_t a  = (uint16_t)(sn % 100u);

  return (a == 0u) ? 100u : a;
}

'''

if "cv_slave_addr" not in c:
    anchor = "uint16_t parsing_command(const uint8_t *mas"
    i = c.find(anchor)
    if i < 0:
        print("[!] parsing_command not found"); sys.exit(1)
    # insert before the comment block that precedes parsing_command
    j = c.rfind("/* ====", 0, i)
    if j < 0: j = i
    c = c[:j] + HELPER + c[j:]
    print("[+] added cv_slave_addr()")
else:
    print("[=] cv_slave_addr() already present")

# ---- 2. address filter inside parsing_command ---------------------------
OLD_HEAD = '''uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t answer = 0u;
  uint8_t  op;
  uint8_t  locked = 0u;

  if (len < 5u)
  {
    answer = CmdError(out, mas, CMD_ERR_VALUE);
    (void)CreateCRC(out, answer);
    return (uint16_t)(answer + 2u);
  }

  op = mas[1];'''

NEW_HEAD = '''uint16_t parsing_command(const uint8_t *mas, uint16_t len, uint8_t *out, uint16_t cap)
{
  uint16_t answer = 0u;
  uint16_t slave;
  uint8_t  op;
  uint8_t  locked = 0u;

  if (len < 5u)
  {
    answer = CmdError(out, mas, CMD_ERR_VALUE);
    (void)CreateCRC(out, answer);
    return (uint16_t)(answer + 2u);
  }

  /* Slave-address filter: our address, or broadcast 0x00 / 0xFF.
     Anything else is ignored - return 0, the caller sends no answer. */
  slave = cv_slave_addr();
  if ((mas[0] != (uint8_t)slave) && (mas[0] != 0x00u) && (mas[0] != 0xFFu))
  {
    return 0u;
  }

  op = mas[1];'''

if OLD_HEAD in c:
    c = c.replace(OLD_HEAD, NEW_HEAD, 1)
    print("[+] parsing_command: slave-address filter inserted")
elif "cv_slave_addr()" in c and "mas[0] != (uint8_t)slave" in c:
    print("[=] filter already present")
else:
    print("[!] parsing_command head not found in expected form")
    print("    insert the filter manually after the 'len < 5u' check:")
    print("      slave = cv_slave_addr();")
    print("      if (mas[0]!=slave && mas[0]!=0x00 && mas[0]!=0xFF) return 0u;")
    sys.exit(1)

wr(CV, c)
print("")
print("Done. Clean + Build All.")
print("")
print("Slave address = ((SerialHi<<16)|SerialLo) %% 100, 0 -> 100.")
print("Read SerialLo (0x0007) / SerialHi (0x0008) via broadcast 0x00 to learn it.")