import struct
from pathlib import Path
root=Path(__file__).resolve().parents[1]/'assets/ibl'
for name in ('diffuse','specular'):
    data=(root/(name+'.ktx2')).read_bytes()
    fmt,typ,w,h,d,layers,faces,levels,compression=struct.unpack_from('<9I',data,12)
    assert fmt==97 and faces==6 and compression==0 and layers==0
    chunks=[[] for _ in range(6)]
    for mip in range(levels):
        offset,length,raw=struct.unpack_from('<3Q',data,80+mip*24)
        size=max(1,w>>mip)*max(1,h>>mip)*8
        assert length==size*6
        for face in range(6):
            chunks[face].append(data[offset+face*size:offset+(face+1)*size])
    header=[124,0x2100F,h,w,w*8,0,levels]+[0]*11+[32,4,int.from_bytes(b'DX10','little'),0,0,0,0,0]+[0x401008,0xFE00,0,0,0]
    out=b'DDS '+struct.pack('<31I',*header)+struct.pack('<5I',10,3,4,1,0)+b''.join(b''.join(c) for c in chunks)
    assert len(out)==148+sum(len(c) for face in chunks for c in face)
    (root/('irradiance.dds' if name=='diffuse' else 'prefilter.dds')).write_bytes(out)
print('IBL maps verified')
