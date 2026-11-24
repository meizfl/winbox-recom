"""Recursive x64 instruction discovery, seeded by entry and PE unwind records.

Indirect targets and undecodable bytes are recorded, never guessed complete.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
import struct
from pathlib import Path

import capstone
from capstone import x86
from pe64 import PE64


def discover(pe, seeds):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    # Vtables and callback arrays contain leaf routines absent from .pdata.
    # Some arrays are embedded in executable sections, so inspect image-wide
    # aligned pointer slots. Candidate addresses still need recursive decoding.
    pointer_targets={value for (value,) in struct.iter_unpack('<Q',bytes(pe.image[:len(pe.image)//8*8])) if pe.code(value)}
    pending = deque([*seeds,*sorted(pointer_targets)])
    decoded, indirect, failures, calls = {}, {}, set(), set()
    while pending:
        history=deque(maxlen=12)
        pc = pending.popleft()
        while pc not in decoded and pc not in failures and pe.code(pc):
            i = next(md.disasm(pe.read(pc), pc, 1), None)
            if i is None:
                failures.add(pc)
                break
            decoded[pc] = i
            nxt = pc+i.size
            # GCC/MinGW AMD64 relative switch tables:
            # lea base,[rip+table]; movsxd target,[base+index*4]; add target,base; jmp target.
            if i.mnemonic=='lea' and len(i.operands)==2 and i.operands[0].type==x86.X86_OP_REG and i.operands[1].type==x86.X86_OP_MEM and i.operands[1].mem.base==x86.X86_REG_RIP:
                # A table base can be kept in a callee-saved register long
                # before the indexed load (e.g. WinBox's packet parser).
                table=nxt+i.operands[1].mem.disp
                relative=[]
                for k in range(4096):
                    raw=pe.read(table+4*k,4)
                    if len(raw)!=4: break
                    target=table+struct.unpack('<i',raw)[0]
                    if not pe.code(target): break
                    relative.append(target)
                if len(relative)>=2: pending.extend(relative)
                following=list(md.disasm(pe.read(nxt,32),nxt,3))
                base_register=i.operands[0].reg
                if len(following)==3:
                    move,add,transfer=following
                    if move.mnemonic=='movsxd' and len(move.operands)==2 and move.operands[1].type==x86.X86_OP_MEM and move.operands[1].mem.base==base_register and move.operands[1].mem.scale==4 and add.mnemonic=='add' and transfer.mnemonic=='jmp':
                        table=nxt+i.operands[1].mem.disp
                        bound=4096
                        for previous in reversed(history):
                            if previous.mnemonic=='cmp' and len(previous.operands)==2 and previous.operands[1].type==x86.X86_OP_IMM and 0<=previous.operands[1].imm<4096:
                                bound=previous.operands[1].imm+1;break
                        for k in range(bound):
                            raw=pe.read(table+4*k,4)
                            if len(raw)!=4: break
                            target=table+struct.unpack('<i',raw)[0]
                            if not pe.code(target): break
                            pending.append(target)
            if i.mnemonic in ('mov','movabs') and len(i.operands)==2 and i.operands[1].type==x86.X86_OP_IMM and pe.code(i.operands[1].imm):
                pending.append(i.operands[1].imm)
            if i.mnemonic=='lea' and len(i.operands)==2 and i.operands[1].type==x86.X86_OP_MEM and i.operands[1].mem.base==x86.X86_REG_RIP:
                target=nxt+i.operands[1].mem.disp
                if pe.code(target): pending.append(target)
            history.append(i)
            jump = i.group(capstone.CS_GRP_JUMP)
            call = i.group(capstone.CS_GRP_CALL)
            if jump or call:
                if i.operands and i.operands[0].type == x86.X86_OP_IMM:
                    target = i.operands[0].imm
                    if pe.code(target):
                        pending.append(target)
                        if call:
                            calls.add(target)
                else:
                    indirect[pc] = i.mnemonic+' '+i.op_str
                if i.mnemonic == 'jmp':
                    break
            if i.group(capstone.CS_GRP_RET) or i.mnemonic in ('int3', 'ud2', 'hlt'):
                break
            pc = nxt
    return decoded, indirect, failures, calls


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary')
    parser.add_argument('--output', default='analysis/discovery.json')
    args = parser.parse_args()
    pe = PE64(args.binary)
    instructions, indirect, failures, calls = discover(pe, [pe.entry, *pe.functions])
    counts = Counter(i.mnemonic for i in instructions.values())
    result = dict(sha256=hashlib.sha256(pe.data).hexdigest(), architecture='x86-64',
                  image_base=hex(pe.base), entry=hex(pe.entry), sections=pe.sections,
                  unwind_function_count=len(pe.functions), direct_call_target_count=len(calls),
                  instruction_count=len(instructions), mnemonics=dict(counts.most_common()),
                  imports={hex(k): v for k, v in pe.imports.items()},
                  unresolved_indirect_transfers={hex(k): v for k, v in indirect.items()},
                  decode_failures=[hex(x) for x in sorted(failures)],
                  complete=False)
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2)+'\n')
    print(f'{len(pe.functions)} unwind entries; {len(instructions)} instructions; '
          f'{len(indirect)} indirect transfers; {len(failures)} decode failures')


if __name__ == '__main__':
    main()
