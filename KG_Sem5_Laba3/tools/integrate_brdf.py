import numpy as np
import struct
from pathlib import Path
size=256
samples=1024
nv,rough=np.meshgrid((np.arange(size)+0.5)/size,(np.arange(size)+0.5)/size)
a=rough*rough
vx=np.sqrt(1-nv*nv)
A=np.zeros_like(nv)
B=np.zeros_like(nv)
for i in range(samples):
    bits=int(f'{i:032b}'[::-1],2)
    u=bits/4294967296
    phi=2*np.pi*i/samples
    hz=np.sqrt((1-u)/(1+(a*a-1)*u))
    hs=np.sqrt(np.maximum(0,1-hz*hz))
    hx=np.cos(phi)*hs
    vh=np.maximum(0,vx*hx+nv*hz)
    nl=2*vh*hz-nv
    k=a/2
    gv=nv/(nv*(1-k)+k)
    gl=np.maximum(0,nl)/(np.maximum(0,nl)*(1-k)+k)
    vis=gv*gl*vh/np.maximum(hz*nv,1e-8)
    fc=(1-vh)**5
    A+=np.where(nl>0,(1-fc)*vis,0)
    B+=np.where(nl>0,fc*vis,0)
data=np.stack((A,B),-1)/samples
assert np.isfinite(data).all() and (data>=0).all()
assert data[0,-1,0]>0.99 and data[-1,-1,0]<0.4
header=[124,0x100F,size,size,size*4,0,1]+[0]*11+[32,4,int.from_bytes(b'DX10','little'),0,0,0,0,0]+[0x1000,0,0,0,0]
p=Path(__file__).resolve().parents[1]/'assets/ibl/brdf.dds'
p.write_bytes(b'DDS '+struct.pack('<31I',*header)+struct.pack('<5I',34,3,0,1,0)+data.astype('<f2').tobytes())
print('BRDF integration verified')
