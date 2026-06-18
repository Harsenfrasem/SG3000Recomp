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
- `halt`
- CB-prefixed `rlc`, `rrc`, `rl`, `rr`, `sla`, `sra`, `sll`, `srl`, `bit`, `res`, `set`

## Implemented Generated Opcodes

- `nop`
- `ld a,n`
- `ld b,n`
- `ld c,n`
- `jr`
- `jp nn`
- `halt`

## Next Opcode Families

- Exact undocumented X/Y flag behavior for arithmetic, bit, block, and I/O families.
- Remaining DD/FD rare mirrored semantics and cycle conformance.
- Pause/NMI source wiring and exact interrupt timing.
- Flag parity conformance tests against a known Z80 suite.
- Cycle-count validation for taken and untaken branches.
