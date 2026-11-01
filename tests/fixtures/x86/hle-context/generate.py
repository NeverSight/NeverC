"""Independent raw HLE reference and frontend/IR callers for native comparison."""
from pathlib import Path

OPERATIONS = ('add', 'adc', 'and', 'btc', 'btr', 'bts', 'cmpxchg', 'cmpxchg8b',
              'dec', 'inc', 'neg', 'not', 'or', 'sbb', 'sub', 'xor', 'xadd', 'xchg', 'mov')
MASKS = (0x8d5,0x8d5,0x8d5,0x895,0x895,0x895,0x8d5,0x40,0x8d4,0x8d4,
         0x8d5,0,0x8d5,0x8d5,0x8d5,0x8d5,0x8d5,0,0)
UNDEFINED = (0,0,0x10,0x894,0x894,0x894,0,0,0,0,0,0,0x10,0,0,0x10,0,0,0)


def generate(directory):
    directory=Path(directory)
    cases=[]
    for op,name in enumerate(OPERATIONS):
        for width in (1,2,4,8):
            if name in ('btc','btr','bts') and width == 1: continue
            if name == 'cmpxchg8b' and width != 8: continue
            for acquire in (False,True):
                if name == 'mov' and acquire: continue
                cases.append((op,name,width,acquire))
    # C/LLVM inputs are deliberately independent of X86HleState's C++ masks.
    c=['#include <hlecontextintrin.h>', 'typedef unsigned long long u64;',
       'extern double live_values[6],live_result[3];']
    ir=['target triple = "x86_64-unknown-linux-gnu"',
        'declare void @llvm.x86.hle.context(ptr,ptr,ptr,i32 immarg,i32 immarg)',
        '@live_values = external global [6 x double]', '@live_result = external global [3 x double]']
    asm=['.text']
    descriptors=[]
    for i,(op,name,width,acquire) in enumerate(cases):
        control=op|0x200|(0x100 if acquire else 0)
        c.append(f'''u64 candidate_{i}(void*m,void*g,void*f){{
  u64 values[4];__builtin_memcpy(values,g,32);
  double a=live_values[0],b=live_values[1],c=live_values[2];
  double d=live_values[3],e=live_values[4],h=live_values[5];
  __builtin_ia32_hle_context(m,g,f,{control},{width});
  live_result[0]=a+b;live_result[1]=c-d;live_result[2]=e*h;
  return values[0]^(values[1]*13)^(values[2]+71)^(values[3]>>7);
}}''')
        body=[f'define i64 @candidate_{i}(ptr %m,ptr %g,ptr %f) {{']
        for j in range(4):
            body += [f'  %p{j} = getelementptr i64, ptr %g, i64 {j}', f'  %v{j} = load i64, ptr %p{j}, align 1']
        for j in range(6):
            body.append(f'  %f{j} = load double, ptr getelementptr([6 x double], ptr @live_values, i64 0, i64 {j})')
        body += [f'  call void @llvm.x86.hle.context(ptr %m,ptr %g,ptr %f,i32 {control},i32 {width})',
                 '  %a = fadd double %f0,%f1', '  %b = fsub double %f2,%f3', '  %c = fmul double %f4,%f5']
        for j,result_name in enumerate(('a','b','c')):
            body.append(f'  store double %{result_name}, ptr getelementptr([3 x double],ptr @live_result,i64 0,i64 {j})')
        body += ['  %v1m = mul i64 %v1,13', '  %v2a = add i64 %v2,71', '  %v3s = lshr i64 %v3,7',
                 '  %r1 = xor i64 %v0,%v1m', '  %r2 = xor i64 %r1,%v2a', '  %r3 = xor i64 %r2,%v3s',
                 '  ret i64 %r3', '}']
        ir.append('\n'.join(body))
        asm.extend([f'.globl original_{i}', f'.type original_{i},@function', f'original_{i}:',
                    'pushq %rbx','movq %rsi,%r10','movq %rdx,%r11',
                    'movq (%r10),%rax','movq 8(%r10),%rcx',
                    'movq 16(%r10),%rdx','movq 24(%r10),%rbx',
                    'movq (%r11),%r8','btq $0,%r8'])
        # Independent GNU assembly selects the original operand-size encoding.
        prefix='xacquire ' if acquire else 'xrelease '
        if name != 'mov': prefix+='lock '
        suffix={1:'b',2:'w',4:'l',8:'q'}[width]
        reg={1:'%cl',2:'%cx',4:'%ecx',8:'%rcx'}[width]
        if name == 'cmpxchg8b': instruction='cmpxchg8b (%rdi)'
        elif name in ('inc','dec','neg','not'): instruction=f'{name}{suffix} (%rdi)'
        else: instruction=f'{name}{suffix} {reg},(%rdi)'
        asm.extend([prefix+instruction,'pause','pushfq','popq %r8',
                    'movq %rax,(%r10)','movq %rcx,8(%r10)',
                    'movq %rdx,16(%r10)','movq %rbx,24(%r10)',
                    f'andq ${MASKS[op]},%r8',f'andq ${~MASKS[op]},(%r11)',
                    'orq %r8,(%r11)','popq %rbx','ret',
                    f'.size original_{i},.-original_{i}'])
        descriptors.append(f'{{original_{i},candidate_{i},{op},{width},{int(acquire)},{UNDEFINED[op]}}}')
    asm.append('.section .note.GNU-stack,"",@progbits')
    h=['#include <stdint.h>', 'typedef void(*hle_fn)(void*,void*,void*);',
       'typedef uint64_t(*candidate_fn)(void*,void*,void*);',
       'struct test_case { hle_fn original; candidate_fn candidate; unsigned op,bytes,acquire,undefined_flags; };']
    for i in range(len(cases)):
        h.append(f'void original_{i}(void*,void*,void*); uint64_t candidate_{i}(void*,void*,void*);')
    h.append('static const struct test_case cases[]={'+',\n'.join(descriptors)+'};')
    for name,lines in [('candidate.c',c),('candidate.ll',ir),('original.S',asm),('cases.h',h)]:
        (directory/name).write_text('\n'.join(lines)+'\n')
    return len(cases)


def generate_source(directory, source, manifest):
    """Wrap actual NeverD output, checking every represented non-output byte."""
    units = sorted(manifest['units'], key=lambda u: int(u['pc'], 0))
    units = [u for u in units if u['bytes'] != 'C3']
    assert len(units) == 136 and all(not u['native_boundary'] for u in units)
    text = [source, '#include <stdlib.h>', '#include <string.h>',
            'typedef uint64_t u64;', 'extern double live_values[6],live_result[3];']
    for i, unit in enumerate(units):
        pc = int(unit['pc'], 0)
        end = pc + len(bytes.fromhex(unit['bytes']))
        text.append(f'''u64 candidate_{i}(void *m,void *g,void *f){{
  u64 values[4];__builtin_memcpy(values,g,32);
  double a=live_values[0],b=live_values[1],c=live_values[2];
  double d=live_values[3],e=live_values[4],h=live_values[5];
  neverd_x64_state_v6 s={{0}};
  for(unsigned r=0;r<16;++r)s.GPR[r]=0xfedcba9876543210ull+r*971;
  for(unsigned j=0;j<sizeof(s.ZMM);++j)((unsigned char*)s.ZMM)[j]=j*7+13;
  __builtin_memcpy(s.GPR,g,32);__builtin_memcpy(&s.RFlags,f,8);
  s.GPR[7]=(uintptr_t)m;s.PC={pc};s.MXCSR=0x1f80;s.CR8=7;s.CR2=0x12345678;
  s.CS=(neverd_x64_segment){{0,UINT32_MAX,8,0xa09b}};
  s.SS=(neverd_x64_segment){{0,UINT32_MAX,16,0xc093}};
  neverd_x64_state_v6 before=s;u64 budget=1;
  if(neverd_x64_continue_v6(&s,&budget)!=9||budget||s.PC!={end})abort();
  __builtin_memcpy(before.GPR,s.GPR,32);before.RFlags=s.RFlags;before.PC=s.PC;
  if(memcmp(&s,&before,sizeof(s)))abort();
  __builtin_memcpy(g,s.GPR,32);__builtin_memcpy(f,&s.RFlags,8);
  live_result[0]=a+b;live_result[1]=c-d;live_result[2]=e*h;
  return values[0]^(values[1]*13)^(values[2]+71)^(values[3]>>7);
}}''')
    (Path(directory)/'candidate.c').write_text('\n'.join(text)+'\n')
