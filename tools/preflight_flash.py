#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import serial, struct, sys
def crc16(d):
    c=0xFFFF
    for b in d:
        c^=b
        for _ in range(8): c=(c>>1)^0xA001 if c&1 else c>>1
    return c
def rd(ser,s,a):
    req=struct.pack(">BBHH",s,0x03,a,1)
    ser.reset_input_buffer(); ser.write(req+struct.pack("<H",crc16(req))); ser.flush()
    h=ser.read(2)
    if len(h)<2: return "no answer"
    if h[1]&0x80:
        t=ser.read(3); return "exception 0x%02X"%(t[0] if t else 0)
    b=ser.read(5)
    if len(b)<5: return "short"
    return struct.unpack(">H",b[1:3])[0]
port=sys.argv[1]; s=int(sys.argv[2]) if len(sys.argv)>2 else 100
ser=serial.Serial(port,115200,timeout=1)
print("port=%s slave=%d"%(port,s))
for name,a in [("flash_target",0x0040),("flash_status",0x0047),
               ("block_sound",0x0058),("audio_reload_pending",0x0059)]:
    print("  0x%04X %-22s = %s"%(a,name,rd(ser,s,a)))
ser.close()