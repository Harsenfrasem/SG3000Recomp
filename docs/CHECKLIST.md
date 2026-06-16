# Checklist de Continuidade

Use esta lista como guia de implementacao. Marque cada item somente depois de ter teste ou ROM de validacao cobrindo o comportamento.

## Base do Projeto

- [x] Estrutura CMake com runtime, ferramenta CLI e testes.
- [x] Presets CMake para Debug, Release, Visual Studio e Zig.
- [x] Separacao entre `include`, `src/runtime`, `src/tools`, `docs`, `config` e `templates`.
- [x] Gerar pacote CMake consumivel com `find_package(SG3000Recomp)`.
- [ ] Adicionar CI para Windows, Linux e macOS.
- [ ] Adicionar formatador (`clang-format`) e preset de lint.

## Recompilador

- [x] CLI `sgrecomp` com entrada ROM, saida C++ e modo disassembly.
- [x] Geracao de dispatcher C++ por `pc`.
- [x] Fallback para interpretador quando opcode ainda nao e levantado.
- [x] ROM embutida no arquivo gerado com funcao `sgrecomp_load_rom`.
- [x] Smoke runner de ROM com estado de registradores e trace opcional.
- [x] Smoke runner com resumo de PCs, audio, framebuffer e dumps locais de frame/VRAM/CRAM.
- [ ] Descoberta de basic blocks e fluxo de controle real.
- [ ] Emissao de funcoes por bloco em vez de um `switch` monolitico.
- [ ] Comentarios opcionais de disassembly no C++ gerado.
- [ ] Relatorio de cobertura: opcodes levantados, fallback usado, bytes analisados.
- [ ] Config TOML real para opcoes de alvo.

## CPU Z80

- [x] Estado base de registradores e flags.
- [x] Subconjunto inicial de load, ALU, jump, call, ret, in/out e halt.
- [x] Saltos relativos `jr`, `jr z`, `jr nz`.
- [x] Loads basicos via `(HL)`.
- [x] `inc/dec` basicos e `cp n`.
- [x] Matriz completa `ld r,r`, `ld r,(hl)`, `ld (hl),r`.
- [x] ALU de registradores: `add`, `adc`, `sub`, `sbc`, `and`, `or`, `xor`, `cp`.
- [x] ALU imediata: `add`, `adc`, `sub`, `sbc`, `and`, `or`, `xor`, `cp`.
- [x] Operacoes de 16 bits iniciais: `add hl,rr`, `inc rr`, `dec rr`, stack `push/pop`.
- [x] Branches condicionais iniciais: `jp`, `call`, `ret`, `djnz`.
- [x] `rst`, `ex af,af'`, `exx`, `ex de,hl`, `ex (sp),hl`, `ei`, `di`.
- [x] Ajuste decimal `daa`.
- [x] `ldi/ldir`, `cpi/cpir`.
- [x] Prefixo `CB`: rotate, shift, bit, res, set.
- [x] Prefixo `ED` inicial: `im`, `reti/retn`, `neg`, `ld i/r`, `ld dd,(nn)`, `ld (nn),dd`, `ldi/ldir`, `cpi/cpir`.
- [x] Prefixo `ED` restante parcial: block I/O, `adc hl,rr`, `sbc hl,rr`, `rrd/rld`.
- [ ] Prefixo `ED` restante: flags/ciclos exatos e opcodes nao documentados.
- [x] Prefixos `DD/FD` iniciais: IX/IY, stack, `jp (ix/iy)`, loads absolutos, deslocamento `ld/inc/dec/alu`, `DD/FD CB`, `IXH/IXL/IYH/IYL`.
- [ ] Prefixos `DD/FD` restantes: opcodes espelhados raros, flags/ciclos exatos e casos nao documentados completos.
- [x] Interrupt mode: IM 0/1/2, `ei/di`, `reti/retn`.
- [x] Interrupts: IRQ maskable inicial, delayed `ei`, servico NMI inicial.
- [ ] Interrupts: NMI ligado ao Pause, prioridades e timing exato.
- [ ] Testar flags contra suite conhecida de Z80.
- [ ] Validar cycle counts por opcode e por branch tomado/nao tomado.

## Memoria e Cartucho

- [x] Mapa Z80 de 64 KiB.
- [x] Slots de ROM de 16 KiB para SMS.
- [x] Escrita RAM em `0xC000-0xFFFF`.
- [x] Espelhamento correto de RAM de 8 KiB.
- [x] BIOS opcional local, carregada por `--bios` e ignorada pelo Git.
- [x] SMapper parcial: slots de ROM e SRAM de cartucho em dois bancos.
- [x] Remocao automatica de header generico de 512 bytes.
- [ ] Deteccao completa de header e regioes de ROM/BIOS.
- [ ] SMapper completo: control, slots, SRAM, enable bits.
- [ ] Codemasters mapper.
- [ ] Korean mappers usados por ROMs especificas.
- [ ] SG-3000 cartridge layouts.
- [ ] Modelar porta de controle de memoria `0x3E` com todos os bits reais.

## VDP

- [x] VRAM, CRAM, registradores e portas iniciais.
- [ ] TMS9918/SG-3000 modes.
- [ ] SMS Mode 4 tile renderer.
- [x] Sprites 8x8 iniciais com transparencia.
- [ ] Sprites: prioridade, colisao, overflow e limite por scanline.
- [x] Paleta CRAM inicial e framebuffer com background Mode 4 basico.
- [x] VBlank, line interrupts e status flags iniciais.
- [ ] Timing NTSC/PAL configuravel.
- [x] Pause/NMI do SMS conectado ao runtime.

## PSG / Audio

- [x] Parser inicial do latch SN76489.
- [x] Gerar ondas dos 3 canais de tom.
- [x] Canal de ruido inicial.
- [x] Tabela de volume/log attenuation inicial.
- [ ] Mixer estereo e sample rate configuravel.
- [ ] Buffer de audio para host backend.

## Entrada

- [x] Leitura active-low do player 1.
- [x] Player 2.
- [x] Botao Pause do console.
- [ ] Light Phaser.
- [ ] Paddle/Sports Pad, se desejado.

## Host Runtime

- [ ] Loop host de janela, video e audio.
- [ ] Backend SDL2 opcional.
- [ ] Save RAM em arquivo.
- [ ] Estado serializavel para save states.
- [ ] Debug overlay de PC, registradores, FPS e fallback count.

## Testes

- [x] Smoke test de programa Z80 minimo.
- [ ] Testes unitarios por familia de opcode.
- [ ] ROMs sinteticas para mapper, VDP, PSG e input.
- [ ] Comparacao de traces com emulador de referencia.
- [ ] Testes do gerador C++ compilando o arquivo emitido.
- [ ] Testes de regressao com ROMs homebrew permitidas.
- [x] Smoke runner testado com ROMs locais privadas, sem imagens versionadas.

## Documentacao

- [x] README com build e uso.
- [x] Arquitetura inicial.
- [x] Status inicial do Z80.
- [x] Checklist de continuidade.
- [ ] Guia de criacao de port por ROM.
- [ ] Especificacao de config TOML.
- [x] Guia de build/toolchain.
- [ ] Guia de contribuicao e estilo de codigo.
