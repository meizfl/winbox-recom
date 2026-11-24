"""Experimental PE64 -> C static translator. Unsupported operations trap.

Default: bootstrap and small real functions used by the differential tests.
--full discovers the whole image, but is NOT a claim of full instruction/API coverage.
"""
import argparse
from collections import Counter, deque
import hashlib
import json
from pathlib import Path
import capstone
from capstone import x86
from pe64 import PE64
from discover64 import discover

PROBE_SHA256 = "448d32841bb9afa24435db7f78a650a8dfb54042c3ffef38efc94e5e9e8c2d8f"
UNITS = 64

REGS = {}
for idx, names in enumerate([
    ('rax','eax','ax','al','ah'), ('rcx','ecx','cx','cl','ch'),
    ('rdx','edx','dx','dl','dh'), ('rbx','ebx','bx','bl','bh'),
    ('rsp','esp','sp','spl'), ('rbp','ebp','bp','bpl'),
    ('rsi','esi','si','sil'), ('rdi','edi','di','dil'),
    *[(f'r{i}',f'r{i}d',f'r{i}w',f'r{i}b') for i in range(8,16)]
]):
    for name, bits, shift in zip(names, (64,32,16,8,8), (0,0,0,0,8)):
        REGS[name]=(idx,bits,shift)

CC = {'e':'c->zf','z':'c->zf','ne':'!c->zf','nz':'!c->zf',
      'b':'c->cf','c':'c->cf','ae':'!c->cf','nc':'!c->cf',
      'be':'(c->cf || c->zf)','a':'(!c->cf && !c->zf)',
      'l':'c->sf != c->of','ge':'c->sf == c->of',
      'le':'(c->zf || c->sf != c->of)','g':'(!c->zf && c->sf == c->of)',
      's':'c->sf','ns':'!c->sf','o':'c->of','no':'!c->of',
      'p':'c->pf','np':'!c->pf'}


def write(path, content):
    if not path.exists() or path.read_text()!=content:
        path.write_text(content)


def u(value):
    return f'UINT64_C(0x{value & ((1<<64)-1):x})'


class Translator:
    def __init__(self, insn, pe):
        self.i, self.pe = insn, pe

    def r(self, name):
        n,b,s=REGS[name]
        return f'reg(c,{n},{b},{s})'

    def address(self, op):
        mem=op.mem
        parts=[u(mem.disp)]
        if mem.segment:
            segment=self.i.reg_name(mem.segment)
            if segment not in ('fs','gs'): raise NotImplementedError('segment-relative memory')
            parts.append(f'c->{segment}_base')
        if mem.base:
            name=self.i.reg_name(mem.base)
            parts.append(u(self.i.address+self.i.size) if name=='rip' else self.r(name))
        if mem.index:
            parts.append(f'({self.r(self.i.reg_name(mem.index))}*{mem.scale})')
        value='('+'+'.join(parts)+')'
        if self.i.addr_size==4:
            value=f'((uint32_t){value})'
        return value

    def read(self, op):
        if op.type==x86.X86_OP_REG:
            return self.r(self.i.reg_name(op.reg))
        if op.type==x86.X86_OP_IMM:
            return u(op.imm)
        if op.type==x86.X86_OP_MEM:
            return f'load(c,{self.address(op)},{op.size*8})'
        raise NotImplementedError('operand kind')

    def write(self, op, value):
        if op.type==x86.X86_OP_REG:
            n,b,s=REGS[self.i.reg_name(op.reg)]
            return f'setreg(c,{n},{b},{s},{value});'
        if op.type==x86.X86_OP_MEM:
            return f'store(c,{self.address(op)},{op.size*8},{value});'
        raise NotImplementedError('destination')

    def translate(self):
        i=self.i; m=i.mnemonic; ops=i.operands; nxt=u(i.address+i.size)
        body=''
        if m in ('nop','endbr64'):
            pass
        elif m in ('movaps','movups','movapd','movdqa','movdqu','movss','movsd','movq','movd') and any(o.type==x86.X86_OP_REG and i.reg_name(o.reg).startswith('xmm') for o in ops):
            dest,src=ops
            def vector(op):
                name=i.reg_name(op.reg)
                if not name.startswith('xmm'): raise NotImplementedError('vector operand')
                return f'c->xmm[{int(name[3:])}]'
            width=16 if m in ('movaps','movups','movapd','movdqa','movdqu') else 4 if m in ('movss','movd') else 8
            if src.type==x86.X86_OP_REG and i.reg_name(src.reg).startswith('xmm'):
                source=vector(src)
                if dest.type==x86.X86_OP_MEM:
                    body=f'uint8_t *p=writable_memory(c,{self.address(dest)},{width}); if(p) memcpy(p,{source},{width});'
                elif i.reg_name(dest.reg).startswith('xmm'):
                    body=f'memcpy({vector(dest)},{source},{width});'
                else:
                    body='uint64_t v=0;'+f'for(unsigned k=0;k<{width};k++) v|=(uint64_t){source}[k]<<(k*8);'+self.write(dest,'v')
            else:
                target=vector(dest)
                if src.type==x86.X86_OP_MEM:
                    body=f'uint8_t *p=memory(c,{self.address(src)},{width}); if(p) {{ memset({target},0,16); memcpy({target},p,{width}); }}'
                else:
                    body=f'uint64_t v={self.read(src)}; memset({target},0,16); for(unsigned k=0;k<{width};k++) {target}[k]=(uint8_t)(v>>(k*8));'
        elif m in ('cvtsi2ss','cvtsi2sd','cvtss2sd','cvtsd2ss','cvttsd2si','cvttss2si','cvtsd2si','cvtss2si'):
            dest,src=ops
            if m.startswith('cvtsi'):
                n=int(i.reg_name(dest.reg)[3:]);body=f'scalar_convert(c,{n},{self.read(src)},{src.size*8},{32 if m.endswith("ss") else 64},0);'
            else:
                srcbits=32 if 'ss' in m else 64
                value=f'vector_bits(c,{int(i.reg_name(src.reg)[3:])},{srcbits})' if src.type==x86.X86_OP_REG else self.read(src)
                if m.endswith('si'):
                    n,b,shift=REGS[i.reg_name(dest.reg)];body=f'scalar_convert(c,{n},{value},{srcbits},{b},{2 if m.startswith("cvtt") else 3});'
                else:
                    n=int(i.reg_name(dest.reg)[3:]);body=f'scalar_convert(c,{n},{value},{srcbits},{64 if m.endswith("sd") else 32},1);'
        elif m in ('addsd','subsd','mulsd','divsd','addss','subss','mulss','divss','comisd','ucomisd','comiss','ucomiss','cmpnlesd'):
            dest,src=ops;bits=32 if m.endswith('ss') else 64
            value=f'vector_bits(c,{int(i.reg_name(src.reg)[3:])},{bits})' if src.type==x86.X86_OP_REG else self.read(src)
            operation=0 if m.startswith('add') else 1 if m.startswith('sub') else 2 if m.startswith('mul') else 3 if m.startswith('div') else 5 if m.startswith('cmp') else 4
            body=f'scalar_math(c,{int(i.reg_name(dest.reg)[3:])},{value},{bits},{operation});'
        elif m in ('andpd','andnpd','orpd'):
            dest,src=ops;n=int(i.reg_name(dest.reg)[3:])
            if src.type==x86.X86_OP_MEM: body=f'uint8_t *source=memory(c,{self.address(src)},16); if(source) {{'
            else: body=f'uint8_t *source=c->xmm[{int(i.reg_name(src.reg)[3:])}]; {{'
            op='|' if m=='orpd' else '&';neg='~' if m=='andnpd' else ''
            body+=f'for(unsigned k=0;k<16;k++) c->xmm[{n}][k]=({neg}c->xmm[{n}][k]){op}source[k]; }}'
        elif m in ('pxor','xorps','xorpd'):
            a=i.reg_name(ops[0].reg); b=i.reg_name(ops[1].reg)
            if not (a.startswith('xmm') and b.startswith('xmm')): raise NotImplementedError('vector xor operand')
            body=f'for(unsigned k=0;k<16;k++) c->xmm[{int(a[3:])}][k]^=c->xmm[{int(b[3:])}][k];'
        elif m in ('rep stosq','rep stosd','rep stosb','rep stosw','stosq','stosd','stosb','stosw'):
            bits=ops[0].size*8; repeated=m.startswith('rep ')
            count='c->r[RCX]' if repeated else '1'
            body=f'uint64_t count={count}; if(count>c->limit-c->steps) {{ trap(c,"string instruction limit"); return 1; }}'
            body+=f'while(count-- && !c->failed) {{ store(c,c->r[RDI],{bits},c->r[RAX]); c->r[RDI]+=c->df ? (uint64_t)-{bits//8} : {bits//8}; ++c->steps;'
            if repeated: body+='--c->r[RCX];'
            body+='}'
        elif m in ('cdqe','cwde','cbw'):
            src,dest={'cdqe':(32,64),'cwde':(16,32),'cbw':(8,16)}[m]
            body=f'setreg(c,0,{dest},0,(uint64_t)(int64_t)(int{src}_t)reg(c,0,{src},0));'
        elif m in ('cqo','cdq','cwd'):
            bits={'cqo':64,'cdq':32,'cwd':16}[m]
            body=f'setreg(c,2,{bits},0,(reg(c,0,{bits},0)>>{bits-1}) ? mask({bits}) : 0);'
        elif m in ('div','idiv','mul') or (m=='imul' and len(ops)==1):
            bits=ops[0].size*8
            body=f'wide_integer(c,{self.read(ops[0])},{bits},{0 if m=="div" else 1 if m=="idiv" else 2 if m=="mul" else 3});'
        elif m in ('rol','ror'):
            bits=ops[0].size*8
            body=f'uint64_t a={self.read(ops[0])},n={self.read(ops[1])}&{63 if bits==64 else 31}; n%={bits}; uint64_t v=a; if(n) {{'
            if m=='rol': body+=f'v=((a<<n)|(a>>({bits}-n)))&mask({bits}); c->cf=v&1; if(n==1) c->of=((v>>{bits-1})&1)^c->cf;'
            else: body+=f'v=((a>>n)|(a<<({bits}-n)))&mask({bits}); c->cf=(v>>{bits-1})&1; if(n==1) c->of=((v>>{bits-1})^(v>>{bits-2}))&1;'
            body+='}'+self.write(ops[0],'v')
        elif m=='bswap':
            bits=ops[0].size*8
            body=f'uint64_t a={self.read(ops[0])},v=0; for(unsigned k=0;k<{bits//8};k++) v=(v<<8)|((a>>(8*k))&255);'+self.write(ops[0],'v')
        elif m in ('bsf','bsr'):
            bits=ops[0].size*8
            body=f'uint64_t a={self.read(ops[1])},v=0; c->zf=!a; if(a) {{'
            body+=('while(!(a&1)) { ++v; a>>=1; }' if m=='bsf' else 'while(a>>=1) ++v;')+self.write(ops[0],'v')+'}'
        elif m=='bt':
            bits=ops[0].size*8
            if ops[0].type==x86.X86_OP_MEM: raise NotImplementedError('memory bit string')
            body=f'c->cf=({self.read(ops[0])}>>({self.read(ops[1])}&{bits-1}))&1;'
        elif m in ('repne scasb','repne scasd','repne scasq','repne scasw'):
            bits=ops[1].size*8
            body=f'while(c->r[RCX] && !c->failed) {{ if(++c->steps>c->limit) {{ trap(c,"string instruction limit");break; }} alu(c,c->r[RAX],load(c,c->r[RDI],{bits}),{bits},1); c->r[RDI]+=c->df ? (uint64_t)-{bits//8} : {bits//8}; --c->r[RCX]; if(c->zf) break; }}'
        elif m=='fninit': body='c->fp_top=0; memset(c->fp,0,sizeof c->fp);'
        elif m in ('xchg','lock xchg'):
            body=f'uint64_t a={self.read(ops[0])},b={self.read(ops[1])};'+self.write(ops[0],'b')+self.write(ops[1],'a')
        elif m in ('cmpxchg','lock cmpxchg'):
            bits=ops[0].size*8
            body=f'uint64_t a=reg(c,0,{bits},0),b={self.read(ops[0])}; alu(c,a,b,{bits},1); if(c->zf) {{'+self.write(ops[0],self.read(ops[1]))+f'}} else setreg(c,0,{bits},0,b);'
        elif m in ('adc','sbb'):
            bits=ops[0].size*8; sub=int(m=='sbb')
            body=f'uint64_t a={self.read(ops[0])},b={self.read(ops[1])},carry=c->cf; uint64_t v=alu(c,a,b,{bits},{sub}); unsigned cf=c->cf; v=alu(c,v,carry,{bits},{sub}); c->cf|=cf; c->of=(unsigned)((({"(a^b)" if sub else "~(a^b)"})&(a^v))>>({bits}-1))&1;'+self.write(ops[0],'v')
        elif m in ('mov','movabs','movzx','movsx','movsxd'):
            v=self.read(ops[1])
            if m in ('movsx','movsxd'):
                v=f'(uint64_t)(int64_t)(int{ops[1].size*8}_t)({v})'
            body=self.write(ops[0],v)
        elif m=='lea':
            body=self.write(ops[0],self.address(ops[1]))
        elif m in ('add','sub','cmp','and','or','xor','test'):
            a,b=map(self.read,ops); bits=ops[0].size*8
            if m in ('add','sub','cmp'):
                v=f'alu(c,{a},{b},{bits},{int(m!="add")})'
            else:
                symbol={'and':'&','test':'&','or':'|','xor':'^'}[m]
                v=f'logic(c,({a}){symbol}({b}),{bits})'
            body=f'uint64_t v={v};'
            if m not in ('cmp','test'):
                body+=self.write(ops[0],'v')
        elif m in ('inc','dec'):
            body=f'unsigned cf=c->cf; uint64_t v=alu(c,{self.read(ops[0])},1,{ops[0].size*8},{int(m=="dec")}); c->cf=cf;'
            body+=self.write(ops[0],'v')
        elif m in ('shl','sal','shr','sar'):
            kind={'shl':0,'sal':0,'shr':1,'sar':2}[m]
            body=f'uint64_t v=shift(c,{self.read(ops[0])},{self.read(ops[1])},{ops[0].size*8},{kind});'
            body+=self.write(ops[0],'v')
        elif m=='imul' and len(ops) in (2,3):
            a,b=(ops[0],ops[1]) if len(ops)==2 else (ops[1],ops[2])
            body=f'uint64_t v=multiply(c,{self.read(a)},{self.read(b)},{ops[0].size*8});'
            body+=self.write(ops[0],'v')
        elif m in ('not','neg'):
            v=f'(~({self.read(ops[0])}))' if m=='not' else f'alu(c,0,{self.read(ops[0])},{ops[0].size*8},1)'
            body=self.write(ops[0],v)
        elif m.startswith('cmov') and m[4:] in CC:
            body=f'uint64_t v={self.read(ops[1])};'+self.write(ops[0],f'({CC[m[4:]]}) ? v : {self.read(ops[0])}')
        elif m in ('movsb','movsw','movsd','movsq','rep movsb','rep movsw','rep movsd','rep movsq') and all(o.type==x86.X86_OP_MEM for o in ops):
            if i.addr_size!=8 or any(o.mem.segment for o in ops):
                raise NotImplementedError('segmented/address-size string move')
            bits=ops[0].size*8
            count='c->r[RCX]' if m.startswith('rep ') else '1'
            body=f'uint64_t count={count}; if(count>c->limit-c->steps) {{ trap(c,"string instruction limit"); return 1; }}'
            body+=f'while(count-- && !c->failed) {{ uint64_t v=load(c,c->r[RSI],{bits}); store(c,c->r[RDI],{bits},v); uint64_t d=c->df ? (uint64_t)-{bits//8} : {bits//8}; c->r[RSI]+=d; c->r[RDI]+=d; ++c->steps;'
            if m.startswith('rep '): body+='--c->r[RCX];'
            body+='}'
        elif m=='push':
            if ops[0].size!=8: raise NotImplementedError('non-64-bit push')
            body=f'uint64_t v={self.read(ops[0])}; push(c,v);'
        elif m=='pop':
            if ops[0].size!=8: raise NotImplementedError('non-64-bit pop')
            body='uint64_t v=pop(c);'+self.write(ops[0],'v')
        elif m=='leave':
            body='c->r[RSP]=c->r[RBP]; c->r[RBP]=pop(c);'
        elif m=='ret':
            return 'uint64_t target=pop(c);'+(f'c->r[RSP]+={ops[0].imm};' if ops else '')+'c->pc=target; return 1;'
        elif m in ('jmp','call'):
            # Calls through an IAT slot are dispatched by slot address. The runtime
            # does not treat loader hint/name RVAs as executable guest addresses.
            target=None
            op=ops[0]
            if op.type==x86.X86_OP_MEM and not op.mem.index:
                base=i.reg_name(op.mem.base) if op.mem.base else ''
                addr=(i.address+i.size if base=='rip' else 0)+op.mem.disp
                if base in ('','rip') and addr in self.pe.imports:
                    target=u(addr)
            target=target or self.read(op)
            body=f'uint64_t target={target};'
            if m=='call': body+=f'push(c,{nxt});'
            return body+'c->pc=target; return 1;'
        elif m.startswith('j') and m[1:] in CC:
            return f'c->pc=({CC[m[1:]]}) ? {self.read(ops[0])} : {nxt}; return 1;'
        elif m.startswith('set') and m[3:] in CC:
            body=self.write(ops[0],f'({CC[m[3:]]})')
        elif m=='cld': body='c->df=0;'
        elif m=='std': body='c->df=1;'
        else:
            raise NotImplementedError(m)
        return body+f' if(!c->failed) c->pc={nxt}; return 1;'


def selected(pe):
    # Bootstrap is not present in the compiler's unwind records. Stop at its
    # final tail jump: subsequent CRT bodies need an explicit full translation.
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64); md.detail=True
    out={}
    for start in [pe.entry,0x401560,0x40156a,0x401571,0x4015d0]:
        pc=start
        for _ in range(2048):
            i=next(md.disasm(pe.read(pc),pc,1),None)
            if not i: break
            out[pc]=i
            pc+=i.size
            if i.mnemonic in ('ret','jmp'): break
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary')
    parser.add_argument('--output',default='gen')
    parser.add_argument('--full',action='store_true')
    args=parser.parse_args()
    pe=PE64(args.binary)
    if not args.full and hashlib.sha256(pe.data).hexdigest()!=PROBE_SHA256:
        parser.error('Default probe addresses are specific to the supplied WinBox build; use --full for other inputs')
    insns=discover(pe,[pe.entry,*pe.functions])[0] if args.full else selected(pe)
    out=Path(args.output); out.mkdir(parents=True,exist_ok=True)
    generated=[[] for _ in range(UNITS)]; unsupported=[]; supported=Counter()
    for address,i in sorted(insns.items()):
        try:
            body=Translator(i,pe).translate(); supported[i.mnemonic]+=1
        except (NotImplementedError,KeyError) as exc:
            reason=f'{i.mnemonic} {i.op_str}: {exc}'
            body=f'trap(c,{json.dumps(reason)}); return 1;'
            unsupported.append(dict(address=hex(address),instruction=i.mnemonic+' '+i.op_str))
        generated[(address>>12)%UNITS].append(f'case {u(address)}: {{ {body} }}\n')
    declarations=''.join(f'int translated_{n}(CPU *c);\n' for n in range(UNITS))
    dispatch=''.join(f'case {n}: return translated_{n}(c);\n' for n in range(UNITS))
    write(out/'translated.c','#include "rt.h"\n'+declarations+'int translated_step(CPU *c) { switch((c->pc>>12)%64) {\n'+dispatch+'default: return 0;\n}}\n')
    for n, bodies in enumerate(generated):
        write(out/f'translated_{n}.c','#include "rt.h"\n'+f'int translated_{n}(CPU *c) {{ switch(c->pc) {{\n'+''.join(bodies)+'default: return 0;\n}}\n')
    image='\n'.join(','.join(str(x) for x in pe.image[p:p+32])+',' for p in range(0,len(pe.image),32))
    # Executable PE sections also contain writable data (bootstrap markers).
    # Guard the bytes actually translated, not whole section ranges.
    spans=[]
    for address,i in sorted(insns.items()):
        end=address+i.size
        if spans and address<=spans[-1][1]:
            spans[-1][1]=max(spans[-1][1],end)
        else:
            spans.append([address,end])
    intervals=','.join('{'+u(a)+','+u(b)+'}' for a,b in spans) or '{0,0}'
    overlap='static const uint64_t spans[][2]={'+intervals+'};\n'
    overlap+='int executable_overlap(uint64_t a, size_t n) { size_t lo=0,hi='+str(len(spans))+'; while(lo<hi) { size_t mid=lo+(hi-lo)/2; if(spans[mid][1]<=a) lo=mid+1; else hi=mid; } return n && lo<'+str(len(spans))+' && (a>=spans[lo][0] || n>spans[lo][0]-a); }\n'
    addresses=','.join(u(a) for a in sorted(insns)) or '0'
    lengths=','.join(str(insns[a].size) for a in sorted(insns)) or '0'
    overlap+='static const uint64_t instruction_addresses[]={'+addresses+'};\nstatic const uint8_t instruction_sizes[]={'+lengths+'};\n'
    overlap+='int instruction_length(uint64_t pc) { size_t lo=0,hi='+str(len(insns))+'; while(lo<hi) { size_t mid=lo+(hi-lo)/2; if(instruction_addresses[mid]<pc) lo=mid+1;else hi=mid; }return lo<'+str(len(insns))+' && instruction_addresses[lo]==pc ? instruction_sizes[lo] : 0; }\n'
    write(out/'image.c','#include "rt.h"\nconst uint8_t guest_image[]={\n'+image+'\n};\n'+f'const size_t guest_image_size=sizeof guest_image;\nconst uint64_t guest_base={u(pe.base)}, guest_entry={u(pe.entry)};\n'+overlap)
    imports=''.join(f'case {u(addr)}: return {json.dumps(name)};\n' for addr,name in pe.imports.items())
    binding=''.join(f'store(c,{u(addr)},64,crt_data(c,{json.dumps(name.split("!")[1])}));' if name.split('!')[1] in ('_acmdln','_fmode','__initenv') else f'store(c,{u(addr)},64,{u(addr)});' for addr,name in pe.imports.items())
    write(out/'imports.c','#include "rt.h"\nconst char *import_name(uint64_t pc) { switch(pc) {\n'+imports+'default: return NULL;\n}}\nvoid bind_imports(CPU *c) {'+binding+'}\n')
    import re
    api_sources=[Path(__file__).resolve().parent.parent/'runtime'/name for name in ('api.c','host.c','crt.c','ui.c','network.c','threads.c','process.c')]
    dispatched=set()
    for source in api_sources:
        content=source.read_text()
        dispatched.update(re.findall(r'IS\("([^"!]+)"\)',content))
        dispatched.update(re.findall(r'!([A-Za-z_][A-Za-z_0-9]+)"',content))
    dispatched_imports=sorted({name for name in pe.imports.values() if name.split('!')[1] in dispatched})
    report=dict(binary_sha256=hashlib.sha256(pe.data).hexdigest(),mode='full-discovery' if args.full else 'bootstrap-probe',
                discovered_instructions=len(insns),translated_instructions=sum(supported.values()),
                supported_mnemonics=dict(supported),unsupported=unsupported,
                dispatched_import_count=len(dispatched_imports),dispatched_imports=dispatched_imports,complete=False)
    write(out/'coverage.json',json.dumps(report,indent=2)+'\n')
    print(f'{sum(supported.values())}/{len(insns)} instructions translated; {len(unsupported)} explicit traps; {len(dispatched_imports)} imported symbols dispatched')


if __name__=='__main__': main()
