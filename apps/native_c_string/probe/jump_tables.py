import re
import struct

def certify(binary, disassembly, ranges):
    decoded = {}
    lines = {}
    for line in disassembly.splitlines():
        m = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+)+([a-z][a-z0-9]*)\s*(.*)', line)
        if m:
            pc = int(m[1], 16)
            if any(a <= pc < b for a, b, _ in ranges):
                decoded[pc] = (m[2], m[3])
                lines[pc] = line
    labels = {int(m[1], 16): m[2] for line in disassembly.splitlines() if (m := re.match(r'^([0-9a-f]+) <([^>]+)>:', line))}
    certificates = []
    result = disassembly
    for pc, (op, operands) in decoded.items():
        if '*' not in operands or op not in ('jmp', 'jmpl', 'call', 'calll'):
            continue
        assert op == 'jmp', 'Indirect call lacks a bounded target proof'
        match = re.fullmatch(r'\*0x([0-9a-f]+)\(,%([a-z]+),4\)', operands)
        assert match, 'Unknown indirect branch form'
        table = int(match[1], 16); register = match[2]
        owners = [(name, s) for name, s in binary.symbols.items() if s['type'] == 2 and s['size'] and s['value'] <= pc < s['value'] + s['size']]
        assert owners and len({(s['value'], s['size']) for _, s in owners}) == 1, 'Ambiguous branch function'
        name, symbol = owners[0]; start = symbol['value']; end = start + symbol['size']
        name = labels[start]; assert name in {n for n, _ in owners}
        addresses = sorted(a for a in decoded if start <= a < end)
        pos = addresses.index(pc); assert pos >= 2
        branch = addresses[pos - 1]; compare = addresses[pos - 2]
        assert decoded[branch][0] == 'ja'
        m = re.fullmatch(r'\$0x([0-9a-f]+),%' + register, decoded[compare][1])
        assert decoded[compare][0] == 'cmp' and m, 'Unsigned index bound is missing'
        maximum = int(m[1], 16); assert maximum <= 64
        section = binary.containing(table, (maximum + 1) * 4)
        assert not section['flags'] & 1, 'Writable jump table'
        offset = section['offset'] + table - section['addr']
        targets = list(struct.unpack_from('<' + 'I' * (maximum + 1), binary.data, offset))
        assert all(t in addresses for t in targets), 'Jump target outside same function instructions'
        edges = {}
        for i, a in enumerate(addresses):
            operation, argument = decoded[a]
            fall = [addresses[i + 1]] if i + 1 < len(addresses) else []
            target = re.match(r'([0-9a-f]+) <', argument)
            if operation.startswith('ret') or operation in ('ud2', 'hlt'):
                successors = []
            elif operation in ('jmp', 'jmpl'):
                if a == pc: successors = targets
                else:
                    assert '*' not in argument and target, 'Unproved second indirect branch'
                    t = int(target[1], 16); successors = [t] if start <= t < end else []
            elif operation.startswith('j') or operation.startswith('loop'):
                assert target, 'Unknown conditional branch'
                t = int(target[1], 16); successors = fall + ([t] if start <= t < end else [])
            else:
                successors = fall
            assert all(t in addresses for t in successors)
            edges[a] = successors
        def reachable_without(blocked, wanted):
            pending = [start]; seen = set()
            while pending:
                a = pending.pop()
                if a == blocked or a in seen: continue
                if a == wanted: return True
                seen.add(a); pending.extend(edges[a])
            return False
        assert start in edges and not reachable_without(compare, branch), 'Compare does not dominate range branch'
        assert not reachable_without(branch, pc), 'Range branch can be bypassed'
        assert edges[branch] == [pc, int(re.match(r'([0-9a-f]+) <', decoded[branch][1])[1], 16)]
        assert int(re.match(r'([0-9a-f]+) <', decoded[branch][1])[1], 16) != pc, 'Out-of-range index enters table'
        certificates.append(dict(function=name, function_start=start, function_end=end, compare=compare, range_branch=branch,
            indirect_jump=pc, table=table, unsigned_maximum=maximum, targets=targets,
            method='Adjacent unsigned cmp/ja dominates table dispatch; immutable entries all target decoded instructions of the same function and frame'))
        prefix = lines[pc].split('\tjmp', 1)[0]
        assert prefix != lines[pc]
        result = result.replace(lines[pc], prefix + '\tjmp ' + format(start, 'x') + ' <' + name + '>', 1)
    return result, certificates
