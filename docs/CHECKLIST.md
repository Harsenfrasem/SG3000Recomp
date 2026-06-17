# Checklist de Continuidade

Use esta lista como guia de implementacao. Marque cada item somente depois de ter teste ou ROM de validacao cobrindo o comportamento.

## Base do Projeto

- [x] Estrutura CMake com runtime, ferramenta CLI e testes.
- [x] Presets CMake para Debug, Release, Visual Studio e Zig.
- [x] Separacao entre `include`, `src/runtime`, `src/tools`, `docs`, `config` e `templates`.
- [x] Gerar pacote CMake consumivel com `find_package(SG3000Recomp)`.
- [ ] Adicionar CI para Windows, Linux e macOS.
- [ ] Adicionar formatador (`clang-format`) e preset de lint.

## Roadmap: Emulador/Recompiler com Enhancements

Ordem de trabalho aprovada para transformar o projeto em runtime fiel com melhorias opcionais.

### 1. Completar modo fiel basico

- [ ] CPU Z80: fechar flags, ciclos e opcodes raros contra suite conhecida.
- [ ] Memoria/cartucho: completar SMapper, layouts SG-3000 e mappers alternativos.
- [ ] VDP: completar Mode 4, TMS9918/SG-3000 modes, prioridade de sprites e limite por scanline exato.
- [ ] PSG: mixer estereo, buffer de audio e sample rate configuravel.
- [ ] Timing: NTSC/PAL, VBlank, line IRQ, Pause/NMI e prioridades.
- [ ] Validacao: traces comparados com emulador de referencia e ROMs homebrew permitidas.

### 2. Criar `EnhancementConfig`

- [x] Estrutura publica `EnhancementConfig` no runtime.
- [x] Modo padrao `accurate`, sem melhorias ativadas por acidente.
- [ ] Conectar config ao futuro host runtime.
- [x] Conectar config inicial ao `Console` e `Vdp`.
- [x] Conectar config inicial ao `Psg`.
- [ ] Carregamento de config TOML/JSON por alvo sem guardar caminhos locais.
- [x] Registrar no relatorio de smoke quais enhancements estao ativos.

### 3. Primeiro enhancement: `disable_sprite_limit` / `reduce_flicker`

- [x] VDP com limite original de sprites por scanline no modo fiel.
- [x] Flag `disable_sprite_limit` para renderizar mais sprites por scanline.
- [x] Flag `reduce_flicker` para estrategia menos agressiva mantendo prioridade previsivel.
- [x] Teste sintetico de sprite overflow comparando modo fiel e enhanced.
- [x] Documentar que enhancements podem alterar comportamento visual original.

### 4. Host runtime com janela/audio

- [x] Proximo bloco: retorno visual/audio demonstravel antes do host completo.
- [x] Smoke runner com dump local de frame, audio WAV e log VGM PSG.
- [x] Smoke runner com frame BMP local para preview visual rapido.
- [ ] Loop host de video, audio e input.
- [ ] Backend SDL2 opcional ou camada equivalente configuravel.
- [ ] Sincronizacao de frame, audio buffer e input polling.
- [ ] Save RAM em arquivo, save states e debug overlay.
- [ ] Modo de execucao interpretado, recompilado e hibrido no host.

### 5. Audio FM opcional e perfis por jogo

- [ ] `GameProfile` para configurar compatibilidade e enhancements por jogo.
- [ ] Identificacao de jogo por hash/header sem incluir ROM/BIOS no repositorio.
- [ ] FM opcional para jogos com suporte conhecido.
- [ ] Experimento de FM enhancement para PSG-only com perfis manuais.
- [ ] Fallback obrigatorio para PSG original.
- [ ] Documentar limites: FM sintetico nao deve ser tratado como fidelidade historica.

## Recompilador

- [x] CLI `sgrecomp` com entrada ROM, saida C++ e modo disassembly.
- [x] Geracao de dispatcher C++ por `pc`.
- [x] Fallback para interpretador quando opcode ainda nao e levantado.
- [x] ROM embutida no arquivo gerado com funcao `sgrecomp_load_rom`.
- [x] Emissao direta ampliada para loads simples, stack, ALU 8-bit, controle de fluxo, I/O basico, interrupcoes basicas, IX/IY stack, `rst` e matriz `ld r,r`.
- [x] Smoke runner de ROM com estado de registradores e trace opcional.
- [x] Smoke runner com resumo de PCs, audio, framebuffer e dumps locais de frame/VRAM/CRAM.
- [x] Smoke runner com dump CSV de cobertura de PCs.
- [x] Teste automatizado do dump BMP do smoke runner.
- [x] Descoberta inicial de basic blocks e fluxo de controle estatico alcancavel.
- [ ] Emissao de funcoes por bloco em vez de um `switch` monolitico.
- [x] Comentarios de disassembly no C++ gerado.
- [x] Relatorio de analise: blocos, sucessores, opcodes levantados e fallback usado.
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
- [x] Load/save/dump local de SRAM de cartucho.
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
- [ ] SMS Mode 4 tile renderer completo.
- [x] Mode 4: hscroll/vscroll basicos, locks de topo/direita e coluna esquerda em branco.
- [x] Sprites 8x8 iniciais com transparencia.
- [x] Sprites: deslocamento horizontal e zoom inicial.
- [x] Sprites: colisao e overflow iniciais.
- [ ] Sprites: prioridade e limite por scanline exato.
- [x] Paleta CRAM inicial e framebuffer com background Mode 4 basico.
- [x] VBlank, line interrupts e status flags iniciais.
- [ ] Timing NTSC/PAL configuravel.
- [x] Pause/NMI do SMS conectado ao runtime.

## PSG / Audio

- [x] Parser inicial do latch SN76489.
- [x] Gerar ondas dos 3 canais de tom.
- [x] Canal de ruido inicial.
- [x] Tabela de volume/log attenuation inicial.
- [x] Dump WAV local pelo smoke runner.
- [x] Dump VGM local de writes PSG pelo smoke runner.
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
- [x] Save RAM em arquivo local pelo smoke runner.
- [ ] Estado serializavel para save states.
- [ ] Debug overlay de PC, registradores, FPS e fallback count.

## Enhancements Opcionais

- [x] `EnhancementConfig` integrado ao runtime inicial.
- [x] Modo fiel como padrao imutavel.
- [x] `disable_sprite_limit`.
- [x] `reduce_flicker`.
- [ ] Perfis por jogo.
- [ ] Audio FM opcional.
- [ ] Relatorio/debug mostrando enhancements ativos.

## Testes

- [x] Smoke test de programa Z80 minimo.
- [ ] Testes unitarios por familia de opcode.
- [ ] ROMs sinteticas para mapper, VDP, PSG e input.
- [ ] Comparacao de traces com emulador de referencia.
- [x] Testes do gerador C++ compilando o arquivo emitido.
- [x] Teste do relatorio de analise estatica do gerador.
- [x] Teste do dump WAV do smoke runner.
- [x] Teste do dump VGM PSG do smoke runner.
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
