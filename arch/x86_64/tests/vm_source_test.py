#!/usr/bin/env python3
"""Structural safety gates supplement actual guest tests, never simulate a TLB."""
import re
import subprocess
from pathlib import Path
from frame_source_test import body, compact, require_order
ROOT = Path(__file__).resolve().parents[3]
ARCH = ROOT / 'arch/x86_64'


def source(name):
    return subprocess.run(['gcc', '-E', '-P', '-std=c11', '-DVM_TEST=1', '-DVM_TEST_INJECT=0',
                           '-I' + str(ARCH), str(ARCH / name)], check=True,
                          stdout=subprocess.PIPE, text=True).stdout


def verify(core, guest):
    own = compact(body(core, 'owned'))
    require_order(own, 'index_of(s->pool,entry&', 'if(i<0)return0;',
                  'f->owner!=s->space_id', 's->pool->platform.alias(', 'returnr;')
    for fn in ('owned', 'hierarchy', 'vm_owned_hierarchy_valid'):
        assert not re.search(r'\bframe_pool_[a-z]+\s*\(', body(core, fn)), 'recursive pool audit callback'
    assert 's->pool->service_owner!=s' in compact(body(core, 'vm_owned_hierarchy_valid'))
    context = compact(body(guest, 'context'))
    for check in ('if(i&&pdpt[i])return0;', 'if(i&&!vm.ready&&pml4[i])return0;',
                  'if(vm.ready&&(vm.root!=pml4||!vm_owned_hierarchy_valid(&vm)))return0;',
                  '(pd[i]&~0x20ull)!=(i<32?(uint64_t)&pt[i*512]|3:0)',
                  '(pml4[0]&~0x20ull)!=((uint64_t)pdpt|3)',
                  '(pdpt[0]&~0x20ull)!=((uint64_t)pd|3)', 'c4!=0x20', 'c3!=(uint64_t)pml4'):
        assert check in context, 'lost exact owned/borrowed hierarchy validation'
    commit = compact(body(core, 'vm_commit'))
    require_order(commit, 'handle(s,h,&r)', 'range(r,offset,bytes)', 'bytes/4096>256u',
                  'needed>s->pool->roles[FRAME_FREE]', 'needed>', 'allocate(s,FRAME_DATA',
                  'frame_pool_promote(', 's->backing[s->staging[i].index]=',
                  '*leaf(s,va)=leaf_value(d)', '*parent=t->id.physical|(1UL<<63)|3;', 's->staged_count=0;flush(s);')
    # Preprocessing expands NX, so compare the actual literal policy below.
    assert 'goto failed' in body(core, 'vm_commit')
    published = commit[commit.index('frame_pool_promote('):commit.index('failed:')]
    assert 'allocate(' not in published and 'goto' not in published
    assert 'corrupt();' in published
    remove = compact(body(core, 'remove_backing'))
    require_order(remove, '*leaf(s,d->va)=0;', 'd->retiring=d->kind;d->kind=0;',
                  '*parent=0;t->retiring=kind;t->kind=0;',
                  '__asm__volatile("":::"memory");', 'frame_pool_retire(', 'frame_pool_reclaim(',
                  'zero(&s->backing[i],sizeof(s->backing[i]))')
    assert 'FRAME_FREE' not in remove, 'only pool reclaim may publish reusable frames'
    assert 's->pool->platform.flush(s->pool->platform.opaque);' in compact(body(core, 'flush'))
    token = compact(body(core, 'handle'))
    require_order(token, 'h.space_id!=s->space_id', 'h.slot>=64u', '!r->live||!h.generation||r->generation!=h.generation')
    reserve = compact(body(core, 'vm_reserve'))
    assert 'generation!=(18446744073709551615UL)' in reserve
    assert '++r->generation;r->live=1;' in reserve
    leaf = compact(body(core, 'leaf_value'))
    assert leaf == 'returnr->permission==VM_NONE?0:r->id.physical|(1UL<<63)|1|(r->permission==VM_READ_WRITE?2:0);'
    assert '(e&~0x20UL)!=(r->id.physical|(1UL<<63)|3)' in compact(body(core, 'hierarchy'))


def main():
    core, guest = source('sparse_vm.c'), source('frame_guest.c')
    # These mutations must be rejected without relying on incidental QEMU TLB eviction.
    mutations = [
        ('core', '*leaf(s,d->va)=0;', '(void)0;', 'missing leaf unlink'),
        ('core', '*parent=0;', '(void)0;', 'missing table unlink'),
        ('core', 'frame_pool_reclaim(s->pool,&released)', 'frame_pool_audit(s->pool)', 'missing retirement flush'),
        ('core', 'h.space_id!=s->space_id', '0', 'missing space authority'),
        ('core', 'r->generation!=h.generation', '0', 'missing generation authority'),
        ('core', 'f->owner!=s->space_id', '0', 'foreign physical ownership'),
        ('core', 'r->id.physical|(1UL << 63)|1', 'r->id.physical|1', 'executable service leaf'),
        ('guest', 'vm.ready && (vm.root!=pml4 || !vm_owned_hierarchy_valid(&vm))', '0', 'disabled dynamic hierarchy audit'),
        ('guest', 'i && pdpt[i]', '0', 'lost borrowed hierarchy protection'),
        ('guest', '"mov %0,%%cr3"', '""', 'missing CR3 instruction'),
    ]
    # The shared pool source gate already pins the complete real CR3 binding.
    # VM callback also must retain that exact instruction.
    def gate(c, g):
        # Macro expansion is compiler evidence, never a policy weakening.
        verify(c, g)
        assert '"mov %0,%%cr3"' in body(g, 'flush')
    gate(core, guest)
    for which, old, new, label in mutations:
        original = core if which == 'core' else guest
        assert old in original, 'stale mutation fixture: ' + label
        c, g = (core.replace(old,new), guest) if which == 'core' else (core, guest.replace(old,new))
        try:
            gate(c, g)
        except AssertionError:
            continue
        raise AssertionError('accepted unsafe mutation: ' + label)
    print(f'x64 VM source tests: PASS ({len(mutations)} rejected mutations; ownership, publication, unlink-before-retire, actual CR3)')


if __name__ == '__main__':
    main()
