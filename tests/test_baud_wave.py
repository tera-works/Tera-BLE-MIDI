"""Exercise production baud scoring with simulated 8N1 bit sampling.
This does not model ESP32 oversampling, jitter, electrical noise or hardware ISR.
"""
import ctypes, math, sys
from pathlib import Path
lib=ctypes.CDLL(str(Path(sys.argv[1]).resolve()))
lib.wave_feed.argtypes=[ctypes.POINTER(ctypes.c_ubyte),ctypes.c_uint,ctypes.c_uint,ctypes.c_uint]
def observe(data,errors,now):
    buf=(ctypes.c_ubyte*len(data))(*data)
    return lib.wave_feed(buf,len(data),errors,now)
def sample(data,actual,candidate):
    bits=[1]*4
    for b in data: bits.extend([0]+[(b>>i)&1 for i in range(8)]+[1])
    bits.extend([1]*20)
    def level(t):
        i=int(t)
        return bits[i] if 0<=i<len(bits) else 1
    ratio=actual/candidate
    received=[];errors=0;cursor=0.
    while cursor<len(bits)-20:
        start=max(1,math.ceil(cursor))
        while start<len(bits)-20 and not(bits[start-1]==1 and bits[start]==0):start+=1
        if start>=len(bits)-20:break
        b=sum(level(start+(1.5+i)*ratio)<<i for i in range(8))
        if not level(start+9.5*ratio):errors+=1
        else:received.append(b)
        cursor=start+9.75*ratio
    return received,errors
checks=0
for actual in [31250,38400,57600,115200,230400]:
    for running in [False,True]:
        data=[]
        for i in range(128):
            if not running or i%8==0:data.append(0x90)
            data.extend([40+i%40,64+i%63])
        correct,errors=sample(data,actual,actual)
        assert errors==0 and correct==data,(actual,errors)
        lib.wave_reset(31250)
        for attempt in range(15):
            candidate=lib.wave_baud()
            received,errors=sample(data,actual,candidate)
            observe(received,errors,1+attempt*1600)
            if lib.wave_confirmed():break
        assert lib.wave_confirmed() and lib.wave_baud()==actual,(actual,running,lib.wave_baud())
        checks+=1
        print(f'PASS physical baud={actual}, running_status={running}, trials={attempt+1}')
lib.wave_reset(31250)
for now in range(10000):assert not observe([],0,now*1000)
assert lib.wave_baud()==31250 and not lib.wave_searching()
print(f'PASS {checks} baud wave cases + 10000 silent calls')
