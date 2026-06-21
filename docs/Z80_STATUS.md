# Z80 Implementation Status

## Implemented Fallback Opcodes

- `nop`
- `ld r,n` for all base registers and `(hl)`
- `ld rr,nn` for `bc`, `de`, `hl`, `sp`
- `ld r,r`, `ld r,(hl)`, `ld (hl),r`
- `ld (nn),a`, `ld a,(nn)`
- `ld dd,(nn)`, `ld (nn),dd` through ED
- `ld i,a`, `ld r,a`, `ld a,i`, `ld a,r`
- 8-bit ALU register groups: `add`, `adc`, `sub`, `sbc`, `and`, `xor`, `or`, `cp`
- 8-bit ALU immediate groups: `add`, `adc`, `sub`, `sbc`, `and`, `xor`, `or`, `cp`
- `inc/dec r`, `inc/dec rr`, `add hl,rr`
- `daa`
- `push/pop qq`
- `jp`, `call`, `ret`, conditional `jp/call/ret`, `djnz`, `rst`
- `jr`, `jr z`, `jr nz`, `jr c`, `jr nc`
- `in a,(n)`, `out (n),a`
- `ex af,af'`, `exx`, `ex de,hl`, `ex (sp),hl`
- `ei`, `di`, `im 0/1/2`, `reti`, `retn`
- `neg`
- `ldi`, `ldir`, `cpi`, `cpir`
- `ldd`, `lddr`, `cpd`, `cpdr`
- ED block I/O: `ini`, `ind`, `inir`, `indr`, `outi`, `outd`, `otir`, `otdr`
- `adc hl,rr`, `sbc hl,rr`, `rrd`, `rld`
- IX/IY basics: `ld ix/iy,nn`, `push/pop ix/iy`, `jp (ix/iy)`, `ld sp,ix/iy`
- IX/IY indexed memory operations for common load, ALU, inc/dec, and `DD/FD CB` forms
- IXH/IXL/IYH/IYL load, inc/dec, ALU source, and redundant prefixed register operations
- Ignored `DD`/`FD` prefixes for unaffected instructions, including repeated/mixed prefix chains
- Undefined `ED` encodings as two-byte, 8-cycle NOPs
- Runtime X/Y flags for 8-bit ALU, INC/DEC, CB rotate/shift/BIT, indexed BIT, 16-bit arithmetic, accumulator control, `LD A,I/R`, and block transfer/search
- Generated C++ X/Y flags for directly emitted ALU, INC/DEC, 16-bit add, accumulator rotate, and LDI/LDIR paths
- Generated C++ refresh-register M1 accounting for base, ED/DD direct paths and HALT cycles without duplicating fallback increments
- Generated dispatcher delayed-EI promotion before the following direct or fallback instruction, including HALT handling
- ED block-I/O flags, including direction-dependent sum, H/C overflow, N from transferred data, X/Y from B or repeat PC, parity formula, and repeat quirks
- Runtime refresh register `R` with bit-7 preservation and M1 counts for CB/ED/DD/FD, indexed-CB, repeated prefixes, and HALT cycles
- IRQ/NMI acknowledge state: R increment, HALT release, stack return address, IFF transitions, IM1 13-cycle and IM2 19-cycle service
- Conditional cycle matrix for all eight `RET`/`JP`/`CALL` conditions, all four conditional `JR` forms, `DJNZ`, and final/repeating block instructions
- Generated executable validation for taken and untaken conditional `CALL`, including PC, SP, cycles, and refresh register state
- `halt`
- CB-prefixed `rlc`, `rrc`, `rl`, `rr`, `sla`, `sra`, `sll`, `srl`, `bit`, `res`, `set`
- EI habilita IFF imediatamente e usa `ei_pending` somente para bloquear IRQ por uma instrução.
- MEMPTR/WZ e latch Q preservados no save-state v12 e usados por flags X/Y de BIT, blocos e SCF/CCF.
- 1.604 sequências passam contra SingleStepTests/z80 na revisão fixada
  `ebe1875d48f374bcfd4b505d8eb8ee751568b5f7`, incluindo I/O determinístico e comparando
  registradores, flags, RAM, escritas em portas e ciclos.

## Implemented Generated Opcodes

- `nop`
- `ld a,n`
- `ld b,n`
- `ld c,n`
- `jr`
- `jp nn`
- `halt`

## Next Opcode Families

- Expand external coverage from one representative vector to all randomized vectors per sequence.
- Remaining interrupt priority edge cases and external trace conformance.
