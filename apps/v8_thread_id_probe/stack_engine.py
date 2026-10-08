"""Reproducible native i386 stack analysis; no compilation or source mutation.

Frames are measured from function-entry ESP, excluding its existing incoming
return address. A call adds its own four-byte return address. ESP state includes
all transient argument pushes, reserved areas, hidden-sret cleanup and alignment.
CFG switch targets come from object relocations and are checked against ELF data.
"""
import argparse,collections,datetime,hashlib,json,pathlib,re,shlex,shutil,struct,subprocess,traceback
from elf32 import Elf32

def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
INSN=re.compile(r'^\s*([0-9a-f]+):\s*(?:[0-9a-f]{2}\s+)*([a-z][a-z0-9.]*)\b\s*(.*)$')
TARGET=re.compile(r'^(?:0x)?([0-9a-f]+)\b')
def signed(n): return n-0x100000000 if n>=0x80000000 else n
def immediate(s): return signed(int(s.replace('$',''),0))

class Analysis:
    def __init__(self,elfpath,objects,sus,disassembly,annotations,symbolpath=None):
        self.elf=Elf32(elfpath);self.input_hashes={str(elfpath):sha(elfpath),str(disassembly):sha(disassembly)}
        symbols=Elf32(symbolpath) if symbolpath else self.elf
        if symbolpath:
            self.input_hashes[str(symbolpath)]=sha(symbolpath)
            left={s['name']:s for s in self.elf.sections if s['flags']&2}
            right={s['name']:s for s in symbols.sections if s['flags']&2}
            assert set(left)==set(right)
            for name,a in left.items():
                b=right[name];assert all(a[k]==b[k] for k in ('type','flags','addr','size','align'))
                if a['type']!=8:assert self.elf.bytes(a)==symbols.bytes(b),name
        self.instructions={};self.instruction_bytes={}
        for line in pathlib.Path(disassembly).read_text().splitlines():
            columns=[s.strip() for s in line.split('\t') if s.strip()]
            if len(columns)<3 or not re.fullmatch(r'[0-9a-f]+:',columns[0]) or not re.fullmatch(r'(?:[0-9a-f]{2}\s*)+',columns[1]):continue
            address=int(columns[0][:-1],16);code=bytes.fromhex(columns[1]);text=' '.join(columns[2:]).split(None,1)
            assert self.elf.virtual(address,len(code))==code,('disassembly bytes differ',hex(address))
            self.instructions[address]=(text[0],text[1] if len(text)>1 else '',line.strip())
            self.instruction_bytes[address]=code
        # Real local C++ functions can have the same mangled name in different
        # object files. Bind each linked symbol to exact LLD input placement,
        # then preserve every distinct VA with an owner-qualified analysis key.
        obj_inputs={pathlib.Path(path).name:Elf32(path) for path in objects}
        assert len(obj_inputs)==len(objects)
        placement={}
        map_path=pathlib.Path(symbolpath).parent/'native-link.map'
        self.input_hashes[str(map_path)]=sha(map_path)
        pattern=re.compile(r'^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)\s+(.+):\(([^)]+)\)$')
        for line in map_path.read_text().splitlines():
            match=pattern.match(line)
            if not match or match[5]=='<internal>':continue
            owner=pathlib.Path(match[5]).name
            assert owner in obj_inputs,('unknown map owner',owner)
            key=(owner,match[6]);assert key not in placement,('duplicate placement',key)
            placement[key]=int(match[1],16)
        linked=[dict(sym) for sym in symbols.symbols if sym['type']==2 and sym['size']]
        counts=collections.Counter(sym['name'] for sym in linked)
        self.functions={};self.owners={}
        for sym in linked:
            candidates=[]
            for owner,obj in obj_inputs.items():
                for source in obj.symbols:
                    if source['type']!=2 or source['name']!=sym['name'] or source['size']!=sym['size'] or not source['section']:continue
                    key=(owner,obj.sections[source['section']]['name'])
                    if key in placement and placement[key]+source['value']==sym['value']:candidates.append((obj,source))
            assert len(candidates)==1,('ambiguous/unowned linked function',sym,candidates)
            obj,source=candidates[0]
            key=sym['name'] if counts[sym['name']]==1 else obj.path.name+'::'+sym['name']+'@'+hex(sym['value'])
            assert key not in self.functions
            self.functions[key]=dict(sym,analysis_key=key)
            self.owners[key]=(obj,source)
        start=next(sym['value'] for sym in symbols.symbols if sym['name']=='_start')
        entry=sorted(at for at in self.instructions if at>=start)[:6]
        assert [self.instructions[at][0] for at in entry]==['and','call','mov','mov','int','ud2']
        assert self.functions['_start']['size']==entry[-1]-start+len(self.instruction_bytes[entry[-1]])
        for name,fn in self.functions.items():
            covered=fn['value']
            for address in sorted(at for at in self.instructions if fn['value']<=at<fn['value']+fn['size']):
                assert address==covered,('instruction coverage gap',name,hex(covered),hex(address))
                covered=address+len(self.instruction_bytes[address])
            assert covered==fn['value']+fn['size'],('incomplete function coverage',name,hex(covered))
        self.starts={fn['value']:name for name,fn in self.functions.items()}
        reports={}
        for supath in sus:
            self.input_hashes[str(supath)]=sha(supath)
            rows={}
            for line in pathlib.Path(supath).read_text().splitlines():
                fields=line.split('\t')
                if len(fields)!=3:continue
                name=fields[0].rsplit(':',1)[-1]
                assert name not in rows,('duplicate function stack report',supath,name)
                rows[name]=dict(bytes=int(fields[1]),kind=fields[2],source=fields[0],report=str(supath))
            key=pathlib.Path(supath).stem+'.o';assert key not in reports
            reports[key]=rows
        self.su={}
        for key,(obj,symbol) in self.owners.items():
            self.input_hashes[str(obj.path)]=sha(obj.path)
            if key=='_start':continue
            assert obj.path.name in reports and symbol['name'] in reports[obj.path.name],('missing owner stack report',key,obj.path.name,symbol['name'])
            self.su[key]=reports[obj.path.name][symbol['name']]
        self.annotations=annotations;self.unknowns=[];self.tables=[];self.indirect=[]
        self.results={};self.cleanup={}
        for name,f in self.functions.items():
            returns=set()
            for a,(op,arg,line) in self.instructions.items():
                if f['value']<=a<f['value']+f['size'] and op in ('ret','retl'):
                    returns.add(immediate(arg) if arg else 0)
            self.cleanup[name]=returns
        # Tail-only wrappers inherit their callee's return cleanup. Unknown or
        # recursive tails are rejected when the full graph is evaluated.
        changed=True
        while changed:
            changed=False
            for name,f in self.functions.items():
                for a,(op,arg,line) in self.instructions.items():
                    if not f['value']<=a<f['value']+f['size'] or not op.startswith('j'):continue
                    m=TARGET.match(arg)
                    if not m:continue
                    target=int(m[1],16)
                    if target in self.starts and self.starts[target]!=name:
                        updated=self.cleanup[name]|self.cleanup[self.starts[target]]
                        if updated!=self.cleanup[name]:self.cleanup[name]=updated;changed=True
    def inherit_identical_reports(self,reference):
        def payload(analysis,name):
            obj,sym=analysis.owners[name];section=obj.sections[sym['section']]
            data=bytearray(obj.bytes(section)[sym['value']:sym['value']+sym['size']]);relocations=[]
            for r in obj.relocations:
                if r['section']!=sym['section'] or not sym['value']<=r['offset']<sym['value']+sym['size']:continue
                offset=r['offset']-sym['value'];symbol=r['symbol']
                relocations.append((offset,r['type'],symbol['name'] or obj.sections[symbol['section']]['name'],symbol['value']))
                data[offset:offset+4]=b'\0'*4
            return bytes(data),relocations
        for name in self.functions:
            if name=='_start' or name in self.su:continue
            assert name.startswith('__') and name in reference.su,('missing report',name)
            assert payload(self,name)==payload(reference,name),('runtime machine code differs',name)
            self.su[name]=dict(reference.su[name],report_inherited_from_byte_identical_function=True,
                actual_object=str(self.owners[name][0].path),reference_object=str(reference.owners[name][0].path))
            report=pathlib.Path(reference.su[name]['report']);self.input_hashes[str(report)]=sha(report)
            reference_object=reference.owners[name][0].path;self.input_hashes[str(reference_object)]=sha(reference_object)
    def target_names(self,name,arg,address):
        matches=[a for a in self.annotations if a['callsite_function']==name and re.fullmatch(a['operand_regex'],arg)
            and address in [int(v,16) for k,v in a.items() if k.endswith('_address') and isinstance(v,str)]]
        if len(matches)!=1:raise ValueError('unresolved indirect '+name+' '+arg)
        annotation=matches[0]
        missing=[t for t in annotation['targets'] if t not in self.functions]
        assert not missing,missing
        assert set().union(*(self.cleanup[t] for t in annotation['targets']))=={annotation['target_return_pop_bytes']},annotation
        return annotation['targets'],annotation
    def switch_targets(self,name,address,arg,next_address):
        jump_address=address;guard=None
        m=re.fullmatch(r'\*0x([0-9a-f]+)\(,%[a-z]+,4\)',arg)
        if not m:
            register=re.fullmatch(r'\*(%e(?:ax|bx|cx|dx|si|di))',arg)
            assert register,('unresolved computed jump',name,arg)
            fn=self.functions[name]
            sequence=sorted(at for at in self.instructions if fn['value']<=at<=address)
            assert len(sequence)>=7
            recent=sequence[-7:]
            dec,save,compare,branch,reload,table,jump=[self.instructions[at] for at in recent]
            reg=register[1]
            assert dec[:2]==('dec',reg) and save[0]=='mov' and save[1].startswith(reg+',')
            slot=save[1].split(',',1)[1]
            assert re.fullmatch(r'-0x[0-9a-f]+\(%ebp\)',slot)
            assert compare[0]=='sub' and compare[1].endswith(','+reg)
            limit=immediate(compare[1].split(',')[0]);assert 0<=limit<256
            assert branch[0]=='ja' and TARGET.match(branch[1])
            default=int(TARGET.match(branch[1])[1],16)
            assert default==next_address and reload[:2]==('mov',slot+','+reg)
            table_match=re.fullmatch(r'0x([0-9a-f]+)\(,'+re.escape(reg)+r',4\),'+re.escape(reg),table[1])
            assert table[0]=='mov' and table_match and jump[:2]==('jmp',arg)
            # No branch may bypass the unsigned bound by entering the reload,
            # table load or register jump. The exact saved index is reloaded.
            for at,(op,operand,line) in self.instructions.items():
                if not fn['value']<=at<fn['value']+fn['size'] or not op.startswith(('j','call')):continue
                target=TARGET.match(operand)
                assert not target or int(target[1],16) not in recent[4:],('control transfer bypasses switch guard',name,line)
            guard=dict(kind='Exact dec/save/unsigned-bound/reload/table-load/register-jump',index_register=reg,
                saved_stack_slot=slot,unsigned_maximum=limit,default_target=hex(default),instruction_addresses=[hex(at) for at in recent])
            m=re.fullmatch(r'\*0x([0-9a-f]+)\(,%[a-z]+,4\)','*0x'+table_match[1]+'(,'+reg+',4)')
            address=recent[-2];next_address=recent[-1]
        tablebase=int(m[1],16);fn=self.functions[name]
        obj,sym=self.owners[name];section=obj.sections[sym['section']]
        off=address-fn['value']+sym['value'];end=next_address-fn['value']+sym['value']
        references=[r for r in obj.relocations if r['section']==sym['section'] and off<=r['offset']<end]
        assert len(references)==1,(name,hex(address),references)
        ref=references[0];assert ref['type']==1
        targetsec=obj.sections[ref['symbol']['section']]
        assert targetsec['name']=='.rodata.'+name,targetsec['name']
        offset=struct.unpack_from('<I',obj.bytes(section),ref['offset'])[0]+ref['symbol']['value']
        allrefs=[r for r in obj.relocations if r['section']==sym['section'] and r['type']==1 and r['symbol']['section']==targetsec['index']]
        alloffsets=[struct.unpack_from('<I',obj.bytes(section),r['offset'])[0]+r['symbol']['value'] for r in allrefs]
        limit=min([n for n in alloffsets if n>offset]+[targetsec['size']])
        targetrelocs={r['offset']:r for r in obj.relocations if r['section']==targetsec['index']}
        addresses=[]
        for entry in range(offset,limit,4):
            reloc=targetrelocs[entry];assert reloc['type']==1 and reloc['symbol']['section']==sym['section']
            addend=struct.unpack_from('<I',obj.bytes(targetsec),entry)[0]
            target=fn['value']-sym['value']+reloc['symbol']['value']+addend
            linked=struct.unpack('<I',self.elf.virtual(tablebase+entry-offset,4))[0]
            assert linked==target and target in self.instructions,(hex(linked),hex(target))
            addresses.append(target)
        assert addresses
        if guard:assert len(addresses)==guard['unsigned_maximum']+1 and offset==0 and limit==targetsec['size']
        self.tables.append(dict(function=name,site=hex(jump_address),base=hex(tablebase),object_section=targetsec['name'],
                                object_offset=offset,bytes=limit-offset,targets=[hex(a) for a in addresses],bounded_register_dispatch=guard))
        return sorted(set(addresses))
    def analyse_function(self,name):
        f=self.functions[name];addresses=sorted(a for a in self.instructions if f['value']<=a<f['value']+f['size'])
        assert addresses and addresses[0]==f['value'],name
        index={a:i for i,a in enumerate(addresses)}
        states=collections.defaultdict(set);todo=collections.deque([(f['value'],0,None)])
        calls={};localmax=0;retdepths=set();alignment=[]
        switches={}
        while todo:
            address,depth,bp=todo.popleft();state=(depth,bp)
            if state in states[address]:continue
            states[address].add(state)
            if len(states[address])>256 or depth>16384:raise ValueError('unbounded/mismatched stack CFG '+name+' '+hex(address))
            if depth<0:raise ValueError('negative stack state '+name+' '+hex(address)+' '+str(depth))
            op,arg,line=self.instructions[address];localmax=max(localmax,depth)
            if op in ('enter','enterl','pusha','pushal','pushad','popa','popal','popad','pushf','pushfl','pushfd','pushfw','popf','popfl','popfd','iret','iretd','iretw','lcall','ljmp','retf','lret'):
                raise ValueError('unsupported implicit stack instruction '+name+' '+line)
            if op.startswith('xchg') and '%esp' in arg:raise ValueError('unsupported xchg esp '+name+' '+line)
            i=index[address];following=addresses[i+1] if i+1<len(addresses) else None
            nextstates=[(depth,bp)];successors=[following] if following is not None else []
            if op.startswith('push'):nextstates=[(depth+(2 if op.endswith('w') else 4),bp)]
            elif op.startswith('pop'):
                if arg=='%esp':raise ValueError('pop esp '+name)
                nextstates=[(depth-(2 if op.endswith('w') else 4),None if arg=='%ebp' else bp)]
            elif op in ('sub','subl','add','addl') and arg.endswith(',%esp'):
                value=arg.split(',')[0]
                if not value.startswith('$'):raise ValueError('dynamic ESP arithmetic '+name+' '+line)
                delta=immediate(value);nextstates=[(depth+(delta if op.startswith('sub') else -delta),bp)]
            elif op in ('and','andl') and arg.endswith(',%esp'):
                mask=int(arg.split(',')[0][1:],0)&0xffffffff;unit=(~mask&0xffffffff)+1
                assert unit in (8,16),(name,line)
                losses=[0] if name=='_start' else range(unit)
                nextstates=[(depth+n,bp) for n in losses]
                if not any(a['site']==hex(address) for a in alignment):alignment.append(dict(site=hex(address),unit=unit,max_loss=max(losses)))
            elif op in ('mov','movl') and arg=='%esp,%ebp':nextstates=[(depth,depth)]
            elif op in ('mov','movl') and arg=='%ebp,%esp':
                assert bp is not None;nextstates=[(bp,bp)]
            elif op in ('lea','leal') and arg.endswith(',%esp'):
                m=re.fullmatch(r'(-?0x[0-9a-f]+|[+-]?\d+)?\(%ebp\),%esp',arg)
                if not m or bp is None:raise ValueError('unknown ESP lea '+name+' '+line)
                nextstates=[(bp-int(m[1] or '0',0),bp)]
            elif op=='leave':
                assert bp is not None;nextstates=[(bp-4,None)]
            elif op in ('ret','retl'):
                retdepths.add(depth);successors=[]
                assert depth==0,(name,line,depth)
            elif op in ('ud2','hlt'):successors=[]
            elif op in ('call','calll'):
                if arg.startswith('*'):
                    targets,annotation=self.target_names(name,arg,address)
                    if not any(row['site']==hex(address) for row in self.indirect):
                        self.indirect.append(dict(function=name,site=hex(address),operand=arg,targets=targets,evidence=annotation))
                else:
                    m=TARGET.match(arg);assert m,(name,line)
                    targets=[self.starts[int(m[1],16)]]
                for target in targets:
                    key=(address,target,'call');calls[key]=max(calls.get(key,0),depth)
                cleanup=set().union(*(self.cleanup[t] for t in targets))
                if not cleanup:successors=[]
                nextstates=[(depth-n,bp) for n in cleanup]
            elif op=='jmp' or op.startswith('j') or op.startswith('loop'):
                conditional=op!='jmp'
                if arg.startswith('*'):
                    if address not in switches:switches[address]=self.switch_targets(name,address,arg,following or f['value']+f['size'])
                    destinations=switches[address]
                else:
                    m=TARGET.match(arg);assert m,(name,line)
                    destinations=[int(m[1],16)]
                successors=([following] if conditional and following is not None else [])
                for target in destinations:
                    if target in index:successors.append(target)
                    elif target in self.starts:
                        callee=self.starts[target];calls[(address,callee,'tail')]=max(calls.get((address,callee,'tail'),0),depth)
                        assert depth==0,(name,'tail jump keeps frame',line,depth)
                    else:raise ValueError('branch target outside function '+name+' '+hex(target))
            elif arg.endswith(',%esp') or arg=='%esp':
                if op not in ('cmp','test','cmpl','testl'):raise ValueError('unknown ESP write '+name+' '+line)
            if (arg.endswith(',%ebp') or arg=='%ebp') and op not in ('cmp','cmpl','test','testl','push','pushl','pop','popl') and not (op in ('mov','movl') and arg=='%esp,%ebp'):
                nextstates=[(d,None) for d,b in nextstates]
            for d,b in nextstates:
                localmax=max(localmax,d)
                for target in successors:
                    if target is not None:todo.append((target,d,b))
        su=self.su.get(name)
        if name!='_start':assert su and su['kind']=='static',(name,su)
        self.results[name]=dict(name=name,address=hex(f['value']),size=f['size'],su=su,
            local_high_water_bytes=localmax,reachable_instruction_count=len(states),instruction_count=len(addresses),
            alignment=alignment,callee_return_cleanup=sorted(self.cleanup[name]),
            calls=[dict(site=hex(a),target=t,kind=k,esp_depth=d,return_address_bytes=4 if k=='call' else 0) for (a,t,k),d in sorted(calls.items())])
    def bound(self,name,active=()):
        if name in active:raise ValueError('recursive call graph '+str(active+(name,)))
        result=self.results[name]
        if 'full_chain_bound_bytes' in result:return result['full_chain_bound_bytes'],result['critical_path']
        maximum=result['local_high_water_bytes'];path=[dict(function=name,terminal_local_bytes=maximum)]
        for call in result['calls']:
            subbound,subpath=self.bound(call['target'],active+(name,))
            edgebytes=call['esp_depth']+call['return_address_bytes']
            total=edgebytes+subbound
            if total>maximum:
                maximum=total;path=[dict(function=name,callsite=call['site'],kind=call['kind'],
                    live_frame_and_arguments_bytes=call['esp_depth'],return_address_bytes=call['return_address_bytes'])]+subpath
        result.update(full_chain_bound_bytes=maximum,critical_path=path)
        return maximum,path
