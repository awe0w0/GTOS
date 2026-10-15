"""Inspect this fixed Rust toolchain's ELF32 rlib format without executing it."""
import hashlib
import pathlib

def require(ok,why):
    if not ok: raise RuntimeError(why)

def sha(path):
    value=hashlib.sha256()
    with pathlib.Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''): value.update(block)
    return value.hexdigest()

def inspect_archive(path,rmeta):
    objects=[];metadata={};long_names=b''
    with path.open('rb') as stream:
        def read_at(offset,size):
            stream.seek(offset);value=stream.read(size)
            require(len(value)==size,'Truncated archive data');return value

        def hash_at(offset,size):
            stream.seek(offset);value=hashlib.sha256()
            while size:
                block=stream.read(min(size,1024*1024));require(block,'Truncated archive slice')
                value.update(block);size-=len(block)
            return value.hexdigest()

        require(stream.read(8)==b'!<arch>\n','Not an ordinary rlib archive')
        while True:
            header=stream.read(60)
            if not header: break
            require(len(header)==60 and header[58:]==b'`\n','Malformed archive header')
            name=header[:16].decode('ascii').strip();size=int(header[48:58])
            base=stream.tell();end=base+size
            require(size>0 and end<=path.stat().st_size,'Truncated archive member')
            if size%2: require(read_at(end,1)==b'\n','Malformed archive padding')
            if name=='//':
                require(size<=1024*1024,'Oversized archive name table')
                long_names=read_at(base,size);stream.seek(end+size%2);continue
            if name in ('/','/SYM64/'):
                stream.seek(end+size%2);continue
            if name.startswith('/') and name[1:].isdigit():
                offset=int(name[1:]);require(offset<len(long_names),'Missing long member name')
                name=long_names[offset:].split(b'/\n',1)[0].decode('utf-8')
            else: name=name.rstrip('/')
            require(size>=52,'Missing ELF header')
            eh=read_at(base,52)
            require(eh[:7]==b'\x7fELF'+bytes((1,1,1)) and int.from_bytes(eh[16:18],'little')==1
                    and int.from_bytes(eh[18:20],'little')==3,'Not an ELF32 i386 relocatable object: '+name)
            shoff=int.from_bytes(eh[32:36],'little');shsize=int.from_bytes(eh[46:48],'little')
            shnum=int.from_bytes(eh[48:50],'little');shstr=int.from_bytes(eh[50:52],'little')
            require(shsize==40 and 0<shstr<shnum and shoff+shsize*shnum<=size,'Invalid ELF sections')
            sections=[]
            for index in range(shnum):
                row=read_at(base+shoff+index*40,40)
                item={key:int.from_bytes(row[offset:offset+4],'little') for key,offset in
                      (('name',0),('type',4),('flags',8),('offset',16),('bytes',20),('link',24),('entsize',36))}
                require(item['type']==8 or item['offset']+item['bytes']<=size,'Escaped ELF section')
                sections.append(item)
            strings=sections[shstr];require(strings['type']==3 and strings['bytes']<=1024*1024,'Invalid ELF string table')
            names=read_at(base+strings['offset'],strings['bytes']);named={}
            for item in sections[1:]:
                offset=item['name'];require(offset<len(names) and b'\0' in names[offset:],'Invalid section name')
                section_name=names[offset:].split(b'\0',1)[0].decode('utf-8')
                named.setdefault(section_name,[]).append(item)
            member={'name':name,'bytes':size,'sha256':hash_at(base,size)}
            if name in ('lib.rmeta','lib.rmeta-link'):
                section_name='.rmeta' if name=='lib.rmeta' else '.rmeta-link'
                require(name not in metadata and len(named.get(section_name,[]))==1,'Missing/duplicate Rust metadata')
                payload=named[section_name][0]
                require(payload['type']==1 and payload['bytes']>0,'Invalid Rust metadata payload')
                member.update(payload_bytes=payload['bytes'],payload_sha256=hash_at(base+payload['offset'],payload['bytes']))
                if name=='lib.rmeta':
                    require(payload['bytes']==rmeta.stat().st_size and member['payload_sha256']==sha(rmeta),'Embedded metadata differs from standalone rmeta')
                metadata[name]=member
            else:
                require(name.endswith('.rcgu.o') and not {'.rmeta','.rmeta-link'}&set(named),'Unreviewed object member name')
                member['executable_section_bytes']=sum(item['bytes'] for item in sections if item['flags']&4)
                member['allocated_section_bytes']=sum(item['bytes'] for item in sections if item['flags']&2)
                member['object_kind']='code' if member['executable_section_bytes'] else 'data' if member['allocated_section_bytes'] else 'empty'
                member['data_only']=member['object_kind']=='data'
                if member['object_kind']=='empty':
                    # LLVM emits an empty CGU for module/file attributes. Accept
                    # only its observed harmless ELF form: no payload, relocation,
                    # global symbol, undefined import or nonzero stack marker.
                    require(set(named)=={'.strtab','.text','.comment','.note.GNU-stack','.symtab'}
                            and all(len(items)==1 for items in named.values()),'Unknown empty CGU sections')
                    text=named['.text'][0];stack=named['.note.GNU-stack'][0];sym=named['.symtab'][0]
                    require(text['type']==1 and text['flags']==6 and text['bytes']==0
                            and stack['type']==1 and stack['flags']==0 and stack['bytes']==0
                            and sym['type']==2 and sym['entsize']==16 and sym['bytes']==32
                            and sym['link']<len(sections),'Invalid empty CGU shape')
                    raw=read_at(base+sym['offset'],32)
                    require(raw[:16]==bytes(16) and raw[20:28]==bytes(8)
                            and raw[28:32]==bytes((4,0,241,255)),'Nonlocal symbol in empty CGU')
                    strings=sections[sym['link']]
                    require(strings['type']==3,'Invalid empty CGU symbol names')
                    table=read_at(base+strings['offset'],strings['bytes']);offset=int.from_bytes(raw[16:20],'little')
                    require(offset<len(table) and b'\0' in table[offset:],'Empty CGU file symbol bounds')
                    member['local_file_symbol']=table[offset:].split(b'\0',1)[0].decode('utf-8')
                    require(member['local_file_symbol']==name.split('.',1)[1][:-len('.rcgu.o')],'Empty CGU filename mismatch')
                objects.append(member)
            stream.seek(end+size%2)
    require(objects and any(item['executable_section_bytes']>0 for item in objects)
            and set(metadata)=={'lib.rmeta','lib.rmeta-link'}
            and len({item['name'] for item in objects})==len(objects),'Incomplete/duplicate rlib members')
    return {'objects':objects,'metadata':metadata,'elf_class':32,'elf_machine':'i386','elf_type':'ET_REL'}
