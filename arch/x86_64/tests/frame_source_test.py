#!/usr/bin/env python3
"""Strict source gates for slice-1 ordering and its real CR3 backend.

These supplement byte-backed host tests and guest QEMU evidence. They do not
claim a host callback simulates an x86 TLB or that QEMU always exposes stale TLBs.
Changes to the deliberate first-implementation control flow require review.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
ARCH = ROOT / 'arch/x86_64'


def compact(source):
    return re.sub(r'\s+', '', source)


def body(source, name):
    # Inputs are preprocessed C, so comments and inactive mutation branches have
    # already gone. Skip string/character contents while balancing braces.
    start = re.search(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{', source)
    assert start, f'missing function definition: {name}'
    opening = start.end() - 1
    depth = 0
    tokens = re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[opening:])
    for token in tokens:
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if depth == 0:
                return source[opening+1:opening+token.start()]
    raise AssertionError(f'unbalanced function: {name}')


def require_order(source, *pieces):
    cursor = 0
    for piece in pieces:
        needle = compact(piece)
        found = source.find(needle, cursor)
        assert found >= 0, f'missing/out-of-order source: {piece}'
        cursor = found + len(needle)


def verify(core, backend, memory):
    # No allocator caller may supply synthetic physical candidates or a raw free
    # list. The real parser supplies this pool's immutable physical selection.
    init = compact(body(core, 'frame_pool_init'))
    require_order(init,
        'p->busy=1;',
        'if (!platform->context_ok(platform->opaque))',
        'boot_memory_select(bytes,available,request,&p->selection)',
        'for (uint32_t i=0;i<count;++i) if (p->platform.read_leaf(p->platform.opaque,p->selection.frames[i])) { clear_pool(p);return FRAME_ALIAS_CONFLICT; }',
        'for (uint32_t i=0;i<count;++i) p->platform.write_leaf(p->platform.opaque,p->selection.frames[i],p->selection.frames[i]|(1UL<<63)|3);',
        'p->platform.flush(p->platform.opaque);',
        'p->roles[FRAME_FREE]=count;', 'p->ready=1;', 'enum frame_error e=audit(p);',
        'if (e) { for (uint32_t i=0;i<count;++i) p->platform.write_leaf(p->platform.opaque,p->selection.frames[i],0); p->platform.flush(p->platform.opaque); clear_pool(p);return e; }')
    assert init.count('p->platform.flush(') == 2, 'init and rollback each flush once'
    # A caller's source cannot smuggle bootstrap tables into the frame pool.
    assert not re.search(r'\b(?:pml4|pdpt|pd|pt)\s*\[', core)
    assert 's.borrowed_table_frames=35;' in compact(body(core, 'frame_pool_stats'))
    select = compact(body(memory, 'boot_memory_select'))
    require_order(select,
        'handoff_parse(private_copy,available,request->kernel_start,request->kernel_end,&view)',
        'if (range.type==1) mark(bitmap,range.start,range.end,1);',
        'mark(bitmap,request->kernel_start,request->kernel_end,0);',
        'mark(bitmap,request->original_info_start,info_end,0);',
        'handoff_next_exclusion(&view,&cursor,&excluded)',
        'mark(bitmap,request->retained[i].start,request->retained[i].end,0);',
        'out->frames[count++]=')
    # Use a literal, approved invalidate-before-release implementation rather
    # than merely checking that both a flush and FREE appear somewhere.
    reclaim = compact(body(core, 'frame_pool_reclaim'))
    assert reclaim == compact('''
        if (!released) return FRAME_BAD_ARGUMENT;
        enum frame_error e=checked_enter(p);if (e) return e;
        uint32_t count=p->roles[FRAME_RETIRING];
        if (count) {
            p->platform.flush(p->platform.opaque);
            __asm__ volatile("":::"memory");
            for (uint32_t i=0;i<p->selection.managed_count;++i)
                if (p->records[i].role==FRAME_RETIRING) role(p,i,FRAME_FREE);
        }
        *released=count;return leave(p,FRAME_OK);
    '''), 'reclaim must invalidate before ANY retiring ownership is freed'
    retire = compact(body(core, 'frame_pool_retire'))
    assert 'role(p,i,FRAME_RETIRING)' in retire and 'FRAME_FREE' not in retire
    cancel = compact(body(core, 'frame_pool_cancel'))
    require_order(cancel,
        'lookup(p,id,owner,&i)',
        'if (!e && p->records[i].role!=FRAME_STAGED_DATA && p->records[i].role!=FRAME_STAGED_TABLE) e=FRAME_WRONG_ROLE;',
        'if (!e) role(p,i,FRAME_FREE);')
    lookup = compact(body(core, 'lookup'))
    require_order(lookup,
        'if (!owner) return FRAME_BAD_ARGUMENT;',
        'if (r->generation!=id.generation || r->role==FRAME_FREE) return FRAME_STALE;',
        'if (r->owner!=owner) return FRAME_WRONG_OWNER;', '*slot=i;return FRAME_OK;')
    allocate = compact(body(core, 'frame_pool_allocate'))
    require_order(allocate,
        'if (p->generation==(18446744073709551615UL)) return leave(p,FRAME_ID_EXHAUSTED);',
        'p->platform.alias(p->platform.opaque,physical)',
        'if (!address) return leave(p,FRAME_CORRUPT);',
        'for (uint32_t j=0;j<4096;++j) address[j]=0;',
        '__asm__ volatile("":::"memory");',
        'p->records[i].owner=owner;', 'p->records[i].generation=++p->generation;',
        'role(p,i,staged);', '*out=')
    checked = compact(body(core, 'checked_enter'))
    require_order(checked, 'enter(p)', 'if (e) return e;', 'audit(p)')
    enter = compact(body(core, 'enter'))
    require_order(enter, 'p->busy', 'return FRAME_BAD_STATE;', 'p->busy=1;',
                  'p->platform.context_ok(p->platform.opaque)')
    # Preprocessing expands FRAME_POOL_NX/UINT64_C. Require the exact 64-bit NX
    # expression and permission comparison, without allowing U/S, global or X.
    valid = compact(body(core, 'valid_alias'))
    assert re.fullmatch(
        r'return\(p->platform\.read_leaf\(p->platform\.opaque,physical\)&~0x60(?:UL|ULL)\)=='
        r'\(physical\|\(1(?:UL|ULL)<<63\)\|3\);', valid), 'exact RW/NX supervisor alias policy'
    # Following the real binding matters: a mock flush-count increment does not
    # establish the presence of the privileged invalidate instruction.
    binding = re.search(r'const\s+struct\s+frame_platform\s+x64_frame_platform\s*=\s*([^;]+);', backend)
    assert binding and compact(binding.group(1)) == '{context,leaf,write_leaf,flush,alias,0}'
    assert compact(body(backend, 'flush')) == compact('''
        (void)opaque;
        __asm__ volatile("mov %0,%%cr3"::"r"(pml4):"memory");
        ++flushes;
    '''), 'production flush must reload the actual bootstrap CR3 with memory clobber'
    assert compact(body(backend, 'leaf')) == '(void)opaque;returnpt[a/4096];'
    assert compact(body(backend, 'write_leaf')) == '(void)opaque;pt[a/4096]=v;'
    assert compact(body(backend, 'alias')) == '(void)opaque;return(volatileunsignedchar*)a;'
    context = compact(body(backend, 'context'))
    for token in ('mov%%cr0', 'mov%%cr3', 'mov%%cr4', 'pushfq', 'mov%%rsp', 'mov%%cs'):
        assert token in context, f'context must read real CPU state: {token}'
    for check in ('cs!=24', 'flags&(1ull<<9)', 'sp<(uint64_t)stack_bottom',
                  'sp>=(uint64_t)stack_top', '(c0&0x8001000d)!=0x8001000d',
                  'c3!=(uint64_t)pml4', 'c4!=0x20',
                  '(msr(0xc0000080)&0xd00)!=0xd00', '!(msr(0x1b)&0x100)',
                  '(pml4[0]&~0x20ull)!=((uint64_t)pdpt|3)',
                  '(pdpt[0]&~0x20ull)!=((uint64_t)pd|3)',
                  '(i&&pdpt[i])', '(i&&pml4[i])',
                  '(pd[i]&~0x20ull)!=(i<32?(uint64_t)&pt[i*512]|3:0)'):
        assert check in context, f'missing architecture restriction: {check}'


def preprocessed(path, cc):
    command = [cc, '-E', '-P', '-std=c11', '-DFRAME_TEST_INJECT=0', '-DFRAME_TEST_SMALL=0',
               '-I' + str(ARCH), str(path)]
    return subprocess.run(command, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True).stdout


def mutation_checks(core, backend, memory):
    # Source gate self-checks: every weakened version must fail for the expected
    # class of source change, even if an emulator happens not to cache a stale VA.
    mutations = [
        ('core', 'p->platform.flush(p->platform.opaque);', '(void)0;', 'missing core flush'),
        ('core', '__asm__ volatile("":::"memory");', '(void)0;', 'missing compiler barrier'),
        ('core', 'role(p,i,FRAME_FREE);', 'role(p,i,FRAME_DATA);', 'wrong free transition'),
        ('core', 'r->owner!=owner', '0', 'missing owner check'),
        ('core', 'physical|(1UL << 63)|3', 'physical|3', 'executable pool alias'),
        ('core', 'physical|(1UL << 63)|3', 'physical|(1UL << 63)|7', 'user-accessible pool alias'),
        ('core', 'for (uint32_t j=0;j<4096;++j) address[j]=0;', '(void)address;', 'missing zeroing'),
        ('backend', '"mov %0,%%cr3"', '""', 'missing real CR3 write'),
        ('backend', ':"memory"', ':"cc"', 'missing architecture memory clobber'),
        ('backend', '{context,leaf,write_leaf,flush,alias,0}', '{context,leaf,write_leaf,leaf,alias,0}', 'wrong backend binding'),
        ('backend', 'c4!=0x20', '0', 'missing CR4 restriction'),
        ('backend', 'c3!=(uint64_t)pml4', '0', 'missing CR3 identity'),
        ('backend', '!(msr(0x1b)&0x100)', '0', 'missing BSP restriction'),
        ('memory', 'if (range.type==1)', 'if (1)', 'reserved memory reclamation'),
        ('memory', 'mark(bitmap,request->kernel_start,request->kernel_end,0);', '(void)0;', 'borrowed table reclamation'),
    ]
    sources = {'core': core, 'backend': backend, 'memory': memory}
    count = 0
    for source, old, new, label in mutations:
        assert old in sources[source], f'mutation fixture no longer matches: {label}'
        changed = dict(sources)
        # Removing every match is intentional for changes that occur both during
        # initialization and reclamation or in more than one role transition.
        changed[source] = changed[source].replace(old, new)
        try:
            verify(**changed)
        except AssertionError:
            count += 1
        else:
            raise AssertionError(f'source gate accepted mutation: {label}')
    # Explicitly swap the retire-loop and flush. Merely checking presence or
    # counting calls would accept this dangerous implementation.
    reclaim = body(core, 'frame_pool_reclaim')
    flush = 'p->platform.flush(p->platform.opaque);'
    assert flush in reclaim
    changed_reclaim = reclaim.replace(flush, '')
    changed_reclaim = changed_reclaim.replace('*released=count;', flush + '\n*released=count;')
    try:
        verify(core.replace(reclaim, changed_reclaim), backend, memory)
    except AssertionError:
        count += 1
    else:
        raise AssertionError('source gate accepted free-before-flush mutation')
    return count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'gcc'))
    args = parser.parse_args()
    core = preprocessed(ARCH / 'frame_pool.c', args.cc)
    backend = preprocessed(ARCH / 'frame_platform.c', args.cc)
    memory = preprocessed(ARCH / 'boot_memory.c', args.cc)
    verify(core, backend, memory)
    count = mutation_checks(core, backend, memory)
    print(f'x64 frame source tests: PASS (real CR3 binding, invalidate-before-free, supervisor NX, boot exclusions, {count} rejected mutations)')


if __name__ == '__main__':
    main()
