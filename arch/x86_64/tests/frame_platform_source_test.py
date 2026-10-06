#!/usr/bin/env python3
"""Pin the narrow one-shot platform initialization; no mocked CPU/TLB proof."""
from frame_source_test import body, compact, require_order, preprocessed, ARCH
from vm_source_test import source


def verify(backend, guest):
    assert compact(body(backend, 'x64_frame_boot_root')) == 'returnpml4;'
    assert compact(body(backend, 'x64_frame_flush_count')) == 'returnflushes;'
    # No callback supplied by the caller and no mutable root parameter. Pin the
    # complete operation, including the rollback edge and publication order.
    assert compact(body(backend, 'x64_frame_vm_init')) == compact('''
        if (!p || !s) return VM_ARGUMENT;
        if (bound_pool || bound_vm || p->ready!=1 || p->busy || s->ready || s->busy ||
            p->service_owner || p->service_root) return VM_STATE;
        if (p->platform.context_ok!=context || p->platform.read_leaf!=leaf ||
            p->platform.write_leaf!=write_leaf || p->platform.flush!=flush ||
            p->platform.alias!=alias || p->platform.opaque) return VM_STATE;
        if (!context(0)) return VM_STATE;
        p->service_root=pml4;
        enum vm_error e=vm_init(s,p,pml4);
        if (e) { p->service_root=0;return e; }
        bound_pool=p;bound_vm=s;
        return VM_OK;
    '''), 'initialize fixed root successfully before one-shot binding; rollback every error'
    context = compact(body(backend, 'context'))
    for check in ('if(bound_vm&&(bound_pool->ready!=1||bound_pool->service_root!=pml4||bound_vm->ready!=1||bound_vm->pool!=bound_pool||bound_vm->root!=pml4||bound_pool->service_owner!=bound_vm))return0;',
                  'bound_vm->pool!=bound_pool', 'bound_vm->root!=pml4',
                  '!vm_owned_hierarchy_valid(bound_vm)'):
        assert check in context, 'bound root/owner identity must remain exact'
    assert 'frame_pool_' not in context, 'context callback must not recurse into the pool'
    run = compact(body(guest, 'frame_guest_tests'))
    require_order(run, 'const struct frame_platform *platform=&x64_frame_platform;',
                  'volatile uint64_t *root=x64_frame_boot_root();',
                  'frame_boot_tests(private_copy,size,original,platform);',
                  'frame_pool_init(&pool,private_copy,size,&request,platform,&why)',
                  'pool.service_root=root;',
                  'vm_init(&vm,&pool,alternate_root)==VM_STATE', 'pool.service_root=0;',
                  'x64_frame_vm_init(&pool,&vm)==VM_OK', 'vm_guest_tests(&vm);')
    assert 'rdmsr' not in guest, 'hardware context belongs to production backend'


def main():
    backend, guest = source('frame_platform.c'), source('frame_guest.c')
    verify(backend, guest)
    plain = preprocessed(ARCH / 'frame_platform.c', 'gcc')
    assert 'x64_frame_vm_init' not in plain and 'vm_owned_hierarchy_valid' not in plain
    assert 'if(i&&pml4[i])return0;' in compact(body(plain, 'context'))
    mutations = [
        ('bound_pool || bound_vm ||', '', 'replacement binding'),
        ('p->ready!=1 || p->busy', 'p->ready!=1', 'busy pool binding'),
        ('s->ready || s->busy ||', '', 'live VM binding'),
        ('p->service_owner || p->service_root', '0', 'root/owner replacement'),
        ('p->platform.context_ok!=context', '0', 'foreign context backend'),
        ('p->platform.flush!=flush', '0', 'foreign flush backend'),
        ('p->platform.opaque', '0', 'foreign backend state'),
        ('if (!context(0)) return VM_STATE;', '(void)0;', 'unchecked real context'),
        ('p->service_root=pml4;', 'p->service_root=s->root;', 'caller root selection'),
        ('vm_init(s,p,pml4)', 'VM_OK', 'binding without initialized VM'),
        ('p->service_root=0;return e;', 'return e;', 'failed initialization leaks root'),
        ('if (e) { p->service_root=0;return e; }', '(void)e;', 'failed initialization binds'),
        ('bound_vm=s;', 'bound_vm=0;', 'missing VM publication'),
        ('bound_pool->service_root!=pml4', '0', 'mutable service root'),
        ('bound_vm->pool!=bound_pool', '0', 'foreign pool identity'),
        ('bound_vm->root!=pml4', '0', 'foreign VM root identity'),
        ('bound_vm->ready!=1', '0', 'cleared bound readiness'),
        ('bound_pool->ready!=1', '0', 'cleared bound pool readiness'),
        ('bound_pool->service_owner!=bound_vm', '0', 'foreign service owner'),
        ('return pml4;', 'return pdpt;', 'wrong root accessor'),
        ('enum vm_error e=vm_init(s,p,pml4);',
         'bound_pool=p;bound_vm=s;enum vm_error e=vm_init(s,p,pml4);', 'premature binding publication'),
    ]
    for old, new, label in mutations:
        assert old in backend, 'stale mutation: ' + label
        try:
            verify(backend.replace(old,new), guest)
        except AssertionError:
            continue
        raise AssertionError('accepted unsafe binding mutation: ' + label)
    print(f'x64 frame platform source tests: PASS ({len(mutations)} rejected initialization/binding mutations; VM_TEST=0 retained)')


if __name__ == '__main__':
    main()
