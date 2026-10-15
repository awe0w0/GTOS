#!/usr/bin/env python3
"""Read-only, bounded validation of the reviewed ELF32 shared-link fixtures."""
import hashlib
import pathlib
import struct

def require(ok,why):
    if not ok: raise RuntimeError(why)

def inspect(path,soname,needed,exports,imports):
    path=pathlib.Path(path)
    require(52<=path.stat().st_size<=4*1024*1024,'Unexpected shared file size')
    data=path.read_bytes()
    def span(offset,size):
        require(0<=offset<=len(data) and 0<=size<=len(data)-offset,'Out-of-file ELF range')
        return data[offset:offset+size]
    header=struct.unpack('<16sHHIIIIIHHHHHH',span(0,52))
    ident,kind,machine,version,entry,phoff,shoff,flags,ehsize,phentsize,phnum,shentsize,shnum,shstr=header
    require(ident[:8]==b'\x7fELF\x01\x01\x01\x00' and kind==3 and machine==3 and version==1
            and entry==0 and flags==0 and ehsize==52 and phentsize==32 and 1<=phnum<=32
            and shentsize==40 and 1<=shnum<=200 and 0<shstr<shnum,'Not the qualified ELF32/i386 ET_DYN format')
    span(phoff,phnum*phentsize);span(shoff,shnum*shentsize)
    programs=[struct.unpack('<8I',span(phoff+i*32,32)) for i in range(phnum)]
    require(all(row[0] not in (3,7) for row in programs),'Unexpected interpreter or TLS segment')
    loads=[row for row in programs if row[0]==1]
    require(loads and any(row[6]&1 for row in loads),'Missing loadable code')
    for row in loads:
        _,offset,vaddr,paddr,filesz,memsz,rights,alignment=row
        span(offset,filesz)
        require(filesz<=memsz and memsz<=0x100000000-vaddr and rights&4 and rights&3!=3
                and alignment>0 and alignment&(alignment-1)==0 and (offset-vaddr)%alignment==0,
                'Invalid/WX LOAD segment')
    for first,second in zip(sorted(loads,key=lambda row:row[2]),sorted(loads,key=lambda row:row[2])[1:]):
        require(first[2]+first[5]<=second[2],'Overlapping LOAD memory')
    stacks=[row for row in programs if row[0]==0x6474e551]
    require(len(stacks)==1 and stacks[0][6]&1==0,'Executable/missing GNU_STACK')
    def virtual(address,size,file_backed=True,writable=False):
        require(0<=address<=0xffffffff and 0<=size<=0x100000000-address,'Virtual range overflow')
        candidates=[row for row in loads if row[2]<=address and address+size<=row[2]+row[4 if file_backed else 5]
                    and (not writable or row[6]&2)]
        require(len(candidates)==1,'Virtual range not in one expected LOAD')
        row=candidates[0]
        return row[1]+address-row[2]
    dynamic=[row for row in programs if row[0]==2]
    require(len(dynamic)==1,'Missing/duplicate PT_DYNAMIC')
    row=dynamic[0];require(row[4]%8==0 and 8<=row[4]<=4096,'Invalid dynamic table size')
    require(virtual(row[2],row[4],writable=True)==row[1],'Dynamic table mapping mismatch')
    tags={};terminated=False
    for offset in range(row[1],row[1]+row[4],8):
        tag,value=struct.unpack('<II',span(offset,8))
        if tag==0: terminated=True;break
        tags.setdefault(tag,[]).append(value)
    require(terminated and not any(tag in tags for tag in (15,22,29,35,36,37)),'Unterminated/textrel/path/packed dynamic table')
    def one(tag,default=None):
        values=tags.get(tag,[])
        require(len(values)<=1,'Duplicate unique dynamic tag')
        require(values or default is not None,'Missing dynamic tag '+str(tag))
        return values[0] if values else default
    require(one(30,0)&4==0,'DF_TEXTREL is forbidden')
    strings=span(virtual(one(5),one(10)),one(10))
    def string(offset):
        require(0<=offset<len(strings),'Invalid dynamic string offset')
        end=strings.find(b'\x00',offset);require(end>=0,'Unterminated dynamic string')
        return strings[offset:end].decode('ascii')
    actual_needed=[string(value) for value in tags.get(1,[])]
    require(string(one(14))==soname and actual_needed==needed,'Wrong SONAME/NEEDED or host library dependency')
    sections=[struct.unpack('<10I',span(shoff+i*40,40)) for i in range(shnum)]
    for section in sections:
        if section[1]!=8: span(section[4],section[5])
    dyn_indices=[i for i,section in enumerate(sections) if section[1]==11]
    require(len(dyn_indices)==1,'Missing/duplicate DYNSYM')
    index=dyn_indices[0];symbols_section=sections[index]
    require(symbols_section[9]==16 and symbols_section[5]%16==0 and symbols_section[5]//16<=128
            and one(11)==16 and symbols_section[3]==one(6)
            and virtual(one(6),symbols_section[5])==symbols_section[4]
            and symbols_section[6]<shnum,'Invalid dynamic symbol table')
    string_section=sections[symbols_section[6]]
    require(string_section[1]==3 and string_section[3]==one(5) and string_section[5]==one(10),'Dynamic string table mismatch')
    symbols=[];defined={};undefined=set()
    for offset in range(symbols_section[4],symbols_section[4]+symbols_section[5],16):
        name,value,size,info,other,section=struct.unpack('<IIIBBH',span(offset,16))
        text=string(name);symbol={'name':text,'value':value,'bytes':size,'binding':info>>4,'type':info&15,'visibility':other&3,'section':section}
        symbols.append(symbol)
        if not text: continue
        require(info>>4==1 and other&3==0,'Unexpected weak/local/hidden dynamic symbol')
        if section==0: require(value==0,'Undefined symbol has an address');undefined.add(text)
        else:
            require(text not in defined and section<shnum and size>0,'Invalid exported symbol')
            virtual(value,size,False);defined[text]=symbol
    require(set(defined)==set(exports) and undefined==set(imports),'Unexpected exports/imports')
    for name,kind in exports.items(): require(defined[name]['type']==kind,'Wrong export symbol type')
    relocations=[];rel_sections=[]
    require(not any(section[1]==4 for section in sections),'Unexpected ELF32 RELA')
    for section in sections:
        if section[1]!=9: continue
        require(section[9]==8 and section[5]%8==0 and section[6]==index and section[5]//8<=128,'Invalid dynamic relocation table')
        rel_sections.append(section)
        for offset in range(section[4],section[4]+section[5],8):
            address,info=struct.unpack('<II',span(offset,8));symbol_index=info>>8;kind=info&255
            require(kind in (1,6,7,8) and symbol_index<len(symbols),'Unexpected relocation kind/symbol')
            virtual(address,4,False,True)
            if kind==8: require(symbol_index==0,'RELATIVE relocation has a symbol')
            relocations.append({'address':address,'type':kind,'symbol':symbols[symbol_index]['name']})
    for pointer,size_tag in ((17,18),(23,2)):
        if pointer not in tags: continue
        address=one(pointer);size=one(size_tag)
        require(any(section[3]==address and section[5]==size for section in rel_sections),'Dynamic relocation range mismatch')
    if 17 in tags: require(one(19)==8,'Wrong DT_RELENT')
    if 23 in tags: require(one(20)==17,'PLT relocation format is not REL')
    require(relocations,'Fixture did not exercise dynamic relocations')
    return {'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'elf_class':32,'machine':'i386','type':'ET_DYN',
            'entry':entry,'soname':soname,'needed':actual_needed,'exports':defined,'imports':sorted(undefined),
            'relocations':relocations,'loads':[{'vaddr':row[2],'filesz':row[4],'memsz':row[5],'flags':row[6]} for row in loads],
            'no_interpreter':True,'no_host_libraries':True,'no_textrel':True,'no_wx_load':True,'no_executable_stack':True,
            'guest_executed':False,'guest_dynamic_loader_qualified':False}
