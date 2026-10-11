import pathlib
import struct

def geometry(path,mode):
    data=pathlib.Path(path).read_bytes();h=struct.unpack_from('<HHIIIIIHHHHHH',data,16)
    assert data[:7]==b'\x7fELF\x01\x01\x01' and h[0]==2 and h[1]==3 and h[8]==32
    assert h[9]==3 and h[3]==0x40000000
    pages=0;readonly=[];ranges=[];record=False
    for i in range(h[9]):
        kind,offset,address,physical,size,memory,flags,alignment=struct.unpack_from('<8I',data,h[4]+i*32)
        assert kind==1 and 0<size<=memory and offset+size<=len(data) and alignment==4096
        assert flags in (5,6) and physical==address and address%4096==offset%4096==0
        assert address in [0x40000000,0x40040000,0x40041000]
        if flags==5:
            assert address in [0x40000000,0x40041000]
            assert memory<=(4096 if address==0x40000000 else 0xfe000)
            readonly.append([address,address+memory])
        else:
            assert address==0x40040000 and 120<=size<=memory<=4096
            assert struct.unpack_from('<2I',data,offset)==(1,mode);record=True
        pages+=(memory+4095)//4096;ranges.append([address,address+memory])
    ranges.sort()
    assert all(ranges[i][1]<=ranges[i+1][0] for i in range(2))
    assert record and len(readonly)==2 and 0<pages<=256
    return {'load_pages':pages,'process_cost':pages+5,'heap_cost':17,'readonly_segments':readonly}
