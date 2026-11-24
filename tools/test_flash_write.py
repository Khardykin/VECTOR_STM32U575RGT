#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_flash_write.py — запись sounds.bin во внешнюю flash + audio_reload.

  py tools/test_flash_write.py COM4 --slave 100 --file tools/sounds.bin
  py tools/test_flash_write.py COM4 --slave 100 --file tools/sounds.bin --no-erase
"""
import serial, struct, sys, time, os, argparse

def crc16(d):
    c=0xFFFF
    for b in d:
        c^=b
        for _ in range(8): c=(c>>1)^0xA001 if c&1 else c>>1
    return c

def xfer(ser,req,rest):
    ser.reset_input_buffer(); ser.write(req+struct.pack("<H",crc16(req))); ser.flush()
    h=ser.read(2)
    if len(h)<2: return None
    if h[1]&0x80:
        t=ser.read(3); raise RuntimeError("exception 0x%02X"%(t[0] if t else 0))
    b=ser.read(rest)
    if len(b)<rest: return None
    f=h+b
    if crc16(f[:-2])!=struct.unpack("<H",f[-2:])[0]: return None
    return f

def rd(ser,s,a):
    f=xfer(ser,struct.pack(">BBHH",s,0x03,a,1),5)
    return struct.unpack(">H",f[3:5])[0] if f else None
def wr(ser,s,a,v):
    return xfer(ser,struct.pack(">BBHH",s,0x06,a,v),6) is not None
def wr32(ser,s,a,v):
    return wr(ser,s,a,v&0xFFFF) and wr(ser,s,a+1,(v>>16)&0xFFFF)

def wait_status(ser,s,timeout):
    """0=OK,1=BUSY,2=PROT,3=ADDR,4=VERIFY,5=IO."""
    t0=time.time()
    while time.time()-t0<timeout:
        st=rd(ser,s,0x0047)
        if st==0: return True
        if st in (2,3,4,5):
            print("  flash_status=%d (ошибка)"%st); return False
        time.sleep(0.05)
    print("  таймаут flash_status"); return False

def write_chunk(ser,s,addr,data):
    req=struct.pack(">BBI",s,0x0F,addr)+bytes([len(data)])+data
    return xfer(ser,req,6) is not None   # эхо 6 байт

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("port"); ap.add_argument("--slave",type=int,default=100)
    ap.add_argument("--file",required=True); ap.add_argument("--no-erase",action="store_true")
    ap.add_argument("--chunk",type=int,default=247) # Max for 4 full USB packets (256B frame)
    a=ap.parse_args()

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
    if not os.path.exists(a.file): print("Файл не найден: %s"%a.file); return 1
    size=os.path.getsize(a.file); s=a.slave
    print("port=%s slave=%d file=%s (%d байт)"%(a.port,s,a.file,size))
    ser=serial.Serial(a.port,115200,timeout=1)

    print("\n1. block_sound=1 (0x0058)")
    if not wr(ser,s,0x0058,1): print("FAIL"); return 1

    print("2. flash_key=1, flash_target=SOUND(1)")
    wr32(ser,s,0x0045,1); wr(ser,s,0x0040,1)

    if not a.no_erase:
        er=((size+4095)//4096)*4096
        print("3. erase SOUND: %d байт (%d секторов), жди до 240 с..."%(er,er//4096))
        wr32(ser,s,0x0041,0); wr32(ser,s,0x0043,er); wr(ser,s,0x0049,1)
        if not wait_status(ser,s,240): print("FAIL erase"); wr(ser,s,0x0058,0); return 1
        print("  erase OK")
    else:
        print("3. erase пропущен (--no-erase)")

    print("4. flash_len=%d, запись чанками по %d Б"%(size,a.chunk))
    wr32(ser,s,0x0043,size)
    t0=time.time(); off=0; n=0; total=(size+a.chunk-1)//a.chunk
    with open(a.file,"rb") as f:
        while off<size:
            data=f.read(a.chunk)
            if not write_chunk(ser,s,off,data):
                print("FAIL chunk @%d"%off); wr(ser,s,0x0058,0); return 1
            if not wait_status(ser,s,2.0):
                print("FAIL status @%d"%off); wr(ser,s,0x0058,0); return 1
            off+=len(data); n+=1
            if n%64==0 or off>=size:
                el=time.time()-t0; rate=off/el if el>0 else 0
                eta=(size-off)/rate if rate>0 else 0
                print("  %d/%d (%.1f%%) %.0f Б/с ETA %.0f с"%(n,total,100.0*off/size,rate,eta))

    print("5. audio_reload_pending=1 (0x0059)")
    if not wr(ser,s,0x0059,1):
        print("  FAIL: 0x0059 нет в карте — пересобери и прошей прошивку (патч 5)")
    else:
        wait_status(ser,s,3.0); print("  reload запрошен")

    print("6. block_sound=0")
    wr(ser,s,0x0058,0)
    ser.close()
    print("\nDONE. Проверь воспроизведение (кнопка/API) или лог старта 'image ok=1'.")
    return 0

if __name__=="__main__":
    sys.exit(main())