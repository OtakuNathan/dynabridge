#!/usr/bin/env python3
"""Independent Python encoder: do not compute expected bytes with the C++ codec."""
import struct, subprocess, sys
MASK=(1<<64)-1
SEED=14695981039346656037
def mix(h,v):
    for b in struct.pack('<Q',v): h=((h^b)*1099511628211)&MASK
    return h
integer=mix(SEED,7)
h=mix(mix(SEED,integer),2)
for _ in range(2): h=mix(h,integer)
method_hash=h
request=struct.pack('<IHHQIIQii',0x31525450,1,0,0x101,1,8,h,-2,0x01020304)
record=(struct.pack('<QI',0x0123456789abcdef,4)+b'hi\0\xff'+
        struct.pack('<IIII',3,0,1,0xffffffff)+struct.pack('<BdHHH',1,-0.125,3,7,65535))
codecs=[mix(SEED,4),mix(mix(SEED,20),4096),
        mix(mix(mix(SEED,22),64),mix(SEED,3)),
        mix(mix(SEED,23),mix(SEED,10)),
        mix(mix(mix(SEED,21),3),mix(SEED,2))]
h=mix(mix(mix(SEED,30),0x1001),1)
for i,c in enumerate(codecs,1): h=mix(h,mix(c,i))
expected=[request.hex(),record.hex(),format(h,'x')]
expected += [struct.pack('<IHHQIIQi',0x31535450,1,0,0x101,1,4,method_hash,42).hex(),
             struct.pack('<IHHQIIQ',0x31535450,1,6,0x101,1,0,method_hash).hex()]
actual=subprocess.check_output([sys.argv[1]],text=True).splitlines()
if actual!=expected:raise SystemExit(f'wire v1 golden mismatch: {actual!r} != {expected!r}')
print('Python-independent golden bytes and fingerprints: passed')
