#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_log_read.py — журнал: номера, epoch-время, полный разбор payload."""
import serial, struct, sys, time, argparse
from datetime import datetime, timezone
TYPE_NAMES = {0x00:"TURN_ON",0x01:"TURN_OFF",0x02:"FW_UPDATE",0x04:"READINGS",
              0x08:"PARAM",0x10:"GAS",0x20:"BEACONS",0x30:"PPE",0x40:"SNAPSHOT"}
EPOCH_MIN, EPOCH_MAX = 946684800, 4102444800
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
def rd32(ser,s,a):
    lo,hi=rd(ser,s,a),rd(ser,s,a+1)
    return None if lo is None or hi is None else (hi<<16)|lo
def wr32(ser,s,a,v):
    return wr(ser,s,a,v&0xFFFF) and wr(ser,s,a+1,(v>>16)&0xFFFF)
def stats(ser,s):
    b,c=rd32(ser,s,0x0026),rd32(ser,s,0x0028)
    if b is None or c is None: return None
    return {"begin":b,"current":c,"count":(c-b)&0xFFFFFFFF}
def read_recs(ser,s,ptr,n):
    out=[]
    while n>0:
        k=min(n,50)
        f=xfer(ser,struct.pack(">BBHH",s,0x0C,ptr&0xFFFF,k),1+k*40+2)
        if f is None: raise RuntimeError("no answer ptr=%d"%ptr)
        g=f[2]
        for i in range(g):
            r=f[3+i*40:3+(i+1)*40]
            out.append({"num":struct.unpack("<I",r[8:12])[0],"type":r[0],"fmt":r[1],
                        "plen":r[2],"t":struct.unpack("<I",r[4:8])[0],"pl":r[12:]})
        ptr+=g; n-=g
        if g<k: break
    return out
def fmt_t(t,local=False):
    if EPOCH_MIN<=t<=EPOCH_MAX:
        s=datetime.fromtimestamp(t,tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        if local: s+=" / "+datetime.fromtimestamp(t).strftime("%Y-%m-%d %H:%M:%S local")
        return s,True
    d,r=divmod(t,86400); h,r=divmod(r,3600); m,s=divmod(r,60)
    return "%dд %02d:%02d:%02d [не epoch]"%(d,h,m,s),False
def motion_str(m):
    parts=["pose=%d"%(m&3)]
    if m&0x04: parts.append("FALL")
    if m&0x08: parts.append("SHOCK")
    if m&0x10: parts.append("INACTIVE")
    return ",".join(parts)
def decode(t,p):
    d={}
    if t in (0x00,0x01,0x02) and len(p)>=19:
        d['fw']=struct.unpack("<H",p[0:2])[0]; d['hw']=p[2]; d['brand']=p[3]
        d['reset']=struct.unpack("<I",p[4:8])[0]
        d['state']=struct.unpack("<I",p[8:12])[0]; d['state_err']=struct.unpack("<I",p[12:16])[0]
        d['bat_pct']=p[16]; d['bat_mv']=struct.unpack("<H",p[17:19])[0]
    elif t==0x04 and len(p)>=20:
        d['state']=struct.unpack("<I",p[0:4])[0]; d['state_err']=struct.unpack("<I",p[4:8])[0]
        d['bat_pct']=p[8]; d['bat_mv']=struct.unpack("<H",p[9:11])[0]; d['aqi']=struct.unpack("<H",p[11:13])[0]
        d['temp_C']=struct.unpack("<h",p[13:15])[0]/100.0
        d['hum_pct']=struct.unpack("<H",p[15:17])[0]/100.0
        d['press_hPa']=struct.unpack("<H",p[17:19])[0]/10.0
        d['motion']=motion_str(p[19])
    elif t==0x40 and len(p)>=28:
        d['lat']=struct.unpack("<i",p[0:4])[0]/1e7; d['lon']=struct.unpack("<i",p[4:8])[0]/1e7
        d['bat_pct']=p[8]; d['active_sensors']=p[9]
        d['conc']=list(struct.unpack("<8H",p[10:26]))
        d['chan_thr_bits']=p[26]; d['motion']=motion_str(p[27])
    elif t==0x10 and len(p)>=28:
        d['slot']=p[0]; d['dev_charge']=p[1]; d['active_sensors']=p[2]
        d['conc']=list(struct.unpack("<8H",p[3:19])); d['chan_state']=list(p[19:27])
    elif t==0x30 and len(p)>=26:
        d['presence_mask']=p[0]; d['charge']=list(p[1:9]); d['ppe_type']=list(p[9:17]); d['serial']=list(p[17:25])
    elif t==0x20 and len(p)>=26:
        d['count']=p[0]; d['total']=p[1]; d['beacons']=[]
        for i in range(min(p[0],6)):
            o=2+i*4
            d['beacons'].append((struct.unpack("<H",p[o:o+2])[0],struct.unpack("<b",p[o+2:o+3])[0],p[o+3]))
    elif t==0x08 and len(p)>=20:
        d['param_id']=struct.unpack("<H",p[0:2])[0]; d['source']=p[2]
        d['old_v']=list(p[3:11]); d['new_v']=list(p[11:19])
    return d
def do_clear(ser,s,erase):
    if erase:
        print("  стирание первого сектора журнала (target=LOG)...")
        wr(ser,s,0x0045,1); wr(ser,s,0x0046,0); wr(ser,s,0x0040,4)
        wr32(ser,s,0x0041,0); wr32(ser,s,0x0043,4096); wr(ser,s,0x0049,1)
        t0=time.time()
        while time.time()-t0<3.0:
            if rd(ser,s,0x0047)==0: break
            time.sleep(0.1)
    print("  обнуление указателей 0x0026/0x0028...")
    ok=wr32(ser,s,0x0026,0) and wr32(ser,s,0x0028,0); wr(ser,s,0x004A,1)
    time.sleep(0.2); st=stats(ser,s)
    good=ok and st and st["begin"]==0 and st["current"]==0
    print("  OK: указатели 0/0" if good else "  FAIL: %s"%st)
    return good
def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("port"); ap.add_argument("--slave",type=int,default=100)
    ap.add_argument("--count",type=int,default=5); ap.add_argument("--ptr",type=int,default=None)
    ap.add_argument("--tail",action="store_true"); ap.add_argument("--clear",action="store_true")
    ap.add_argument("--erase",action="store_true"); ap.add_argument("--watch",type=int,default=0)
    ap.add_argument("--local",action="store_true")
    a=ap.parse_args()
    ser=serial.Serial(a.port,115200,timeout=1)
    print("port=%s slave=%d"%(a.port,a.slave))
    if a.clear:
        print("\nОчистка журнала:"); ok=do_clear(ser,a.slave,a.erase); ser.close(); return 0 if ok else 1
    s=stats(ser,a.slave)
    if s is None: print("Нет 0x0026/0x0028"); ser.close(); return 1
    print("\n=== Статистика ==="); print("  beg=%d cur=%d записей=%d"%(s["begin"],s["current"],s["count"]))
    if a.watch:
        print("\nЖдём %d записей..."%a.watch); t0=time.time()
        while s["count"]<a.watch and time.time()-t0<300:
            time.sleep(2.0); s=stats(ser,a.slave); print("  записей: %d"%s["count"])
        if s["count"]<a.watch: print("FAIL: таймаут"); ser.close(); return 1
    if s["count"]==0: print("\nЖурнал пуст."); ser.close(); return 0
    ptr=a.ptr if a.ptr is not None else (max(s["begin"],s["current"]-a.count) if a.tail else s["begin"])
    n=min(a.count,(s["current"]-ptr)&0xFFFFFFFF)
    if n<=0: print("ptr вне окна"); ser.close(); return 1
    print("\n=== Чтение ptr=%d count=%d ==="%(ptr,n))
    recs=read_recs(ser,a.slave,ptr,n); recs.sort(key=lambda r:r["num"])
    fail=0; prev=None; ep=0
    for k,r in enumerate(recs):
        ts,isep=fmt_t(r["t"],a.local); ep+=isep; d=""
        if prev is not None:
            if r["t"]>=prev: d="  (+%d с)"%(r["t"]-prev)
            else: d="  [!] НАЗАД на %d с"%(prev-r["t"]); fail+=1
        if k>0 and r["num"]!=recs[k-1]["num"]+1:
            print("  [!] разрыв %d->%d"%(recs[k-1]["num"],r["num"])); fail+=1
        print("\n#%d %s fmt=%d plen=%d%s"%(r["num"],TYPE_NAMES.get(r["type"],"0x%02X"%r["type"]),r["fmt"],r["plen"],d))
        print("  время: %s (%d)"%(ts,r["t"]))
        for key,val in decode(r["type"],r["pl"]).items():
            print("    %s: %s"%(key,val))
        prev=r["t"]
    print("\n=== Итог ===")
    print("номера %d..%d, epoch %d/%d, монотонность: %s"%(recs[0]["num"],recs[-1]["num"],ep,len(recs),"OK" if fail==0 else "НАРУШЕНА(%d)"%fail))
    if 0<ep<len(recs): print("Смешаны базы: сделай --clear и набери заново.")
    ser.close(); return 1 if fail else 0
if __name__=="__main__":
    sys.exit(main())
