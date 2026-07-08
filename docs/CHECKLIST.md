# Checklist de Continuidade

Use esta lista como guia de implementacao. Marque cada item somente depois de ter teste ou ROM de validacao cobrindo o comportamento.

## Base do Projeto

- [x] Estrutura CMake com runtime, ferramenta CLI e testes.
- [x] Presets CMake para Debug, Release, Visual Studio e Zig.
- [x] Separacao entre `include`, `src/runtime`, `src/tools`, `docs`, `config` e `templates`.
- [x] Gerar pacote CMake consumivel com `find_package(SG3000Recomp)`.
- [x] Adicionar CI para Windows, Linux e macOS.
- [x] Adicionar formatador (`clang-format`) e preset de lint.

## Roadmap: Emulador/Recompiler com Enhancements

Ordem de trabalho aprovada para transformar o projeto em runtime fiel com melhorias opcionais.

### 0. Gate de jogabilidade minima

- [x] Corrigir inicializacao de RAM para permitir boot de software que le estado antes da limpeza explicita.
- [x] Corrigir pattern table de background Mode 4 para `0x0000`, ignorando R4 nesse caminho.
- [x] Validacao privada local produz telas/titulos reconheciveis em multiplas imagens sem versionar artefatos.
- [x] Replay deterministico alcanca gameplay em quatro imagens privadas; uma possui gate completo de cinco minutos.
- [x] Host Win32 possui video, teclado, Pause/NMI, audio e toggles de enhancement por CLI/perfil.
- [x] Script headless de input por frame para automatizar entrada em gameplay.
- [x] Validar uma sessao jogavel privada de cinco minutos com input, audio e enhancement, sem versionar artefatos proprietarios.
- [x] Validar save state durante gameplay real com round-trip byte a byte.
- [ ] Validar SRAM durante gameplay em titulo que realmente habilite cartridge RAM.

Status detalhado e ordem de retomada: `docs/PLAYABLE_STATUS.md`.

### 1. Completar modo fiel basico

- [ ] CPU Z80: fechar flags, ciclos e opcodes raros contra suite conhecida.
- [ ] Memoria/cartucho: completar SMapper, layouts SG-3000 e mappers alternativos.
- [ ] VDP: completar Mode 4, TMS9918/SG-3000 modes, prioridade de sprites e limite por scanline exato.
- [x] PSG: mixer estereo, buffer de audio e sample rate configuravel.
- [ ] Timing: NTSC/PAL, VBlank, line IRQ, Pause/NMI e prioridades.
- [ ] Validacao: traces comparados com emulador de referencia e ROMs homebrew permitidas.

### 2. Criar `EnhancementConfig`

- [x] Estrutura publica `EnhancementConfig` no runtime.
- [x] Modo padrao `accurate`, sem melhorias ativadas por acidente.
- [x] Conectar config ao host runtime.
- [x] Conectar config inicial ao `Console` e `Vdp`.
- [x] Conectar config inicial ao `Psg`.
- [x] Carregamento inicial de config TOML por alvo sem guardar caminhos locais.
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
- [x] Loop host headless de frame, audio e input.
- [x] Loop host de janela com video e input inicial no Windows.
- [x] Audio em tempo real inicial no host de janela via Win32 waveOut.
- [x] Audio em tempo real com pre-buffer inicial e contadores de underrun/drop.
- [x] Audio em tempo real com controle de latencia configuravel no host Win32.
- [x] Audio em tempo real com alvo adaptativo: sobe em underrun e retorna gradualmente apos estabilidade.
- [ ] Backend SDL2 opcional ou camada equivalente configuravel.
- [x] Sincronizacao inicial de frame, buffer de audio e input por frame.
- [x] Sincronizacao refinada do host Win32 com fila adaptativa, reserva de buffer atual e telemetria.
- [x] Save RAM em arquivo, save states iniciais e debug overlay.
- [x] Overlay debug simples com FPS, frame, PC, modo runtime e status de audio.
- [x] Overlay debug mostra mapper ativo e bancos principais.
- [x] Overlay debug mostra scanline/status/IRQ do VDP.
- [x] Controles de runtime no host: pausa, reset, mute e volume.
- [x] Load/save SRAM local no host Win32.
- [x] Modo de execucao interpretado, recompilado e hibrido no host, com executor gerado injetavel e modo recompilado estrito.

### 5. Audio FM opcional e perfis por jogo

- [x] `GameProfile` para configurar compatibilidade e enhancements por jogo.
- [x] Identificacao de jogo por hash sem incluir ROM/BIOS no repositorio.
- [x] Carregamento de perfis locais no host por `--profile`.
- [x] Perfil por hash pode selecionar mapper com a mesma nomenclatura da CLI e precedencia documentada.
- [x] Flag `enable_fm` em `EnhancementConfig`, CLI, host e perfis.
- [x] Portas FM iniciais: endereco `$F0`, dados `$F1` e controle/deteccao `$F2`.
- [x] Smoke runner com log CSV local de escritas FM.
- [ ] FM opcional fiel para jogos com suporte conhecido.
- [x] Substituir sintetizador FM aproximado pelo nucleo emu2413/OPLL com revisao e licenca fixadas.
- [x] YM2612 opcional nao historico via Nuked-OPN2: DLL LGPL substituivel, portas F4-F7, mixer, GUI/CLI/perfil e save state.
- [ ] Experimento de FM enhancement para PSG-only com perfis manuais.
- [x] Fallback PSG original permanece padrao mesmo com FM detectavel, ate software selecionar outro modo em `$F2`.
- [x] Limites de validacao por jogos do YM2413 e natureza nao historica do YM2612 documentados.

### 6. GUI amigavel para usuario final

- [x] GUI inicial disponivel apos estabilizacao do host runtime com janela/audio.
- [x] GUI inicia em estado ocioso, com tela preta e menus disponiveis, sem abrir seletores automaticamente.
- [x] Abrir ou trocar ROM na mesma janela e no mesmo processo, salvando a SRAM da sessao anterior com seguranca.
- [x] Seletor independente para carregar/remover BIOS antes da ROM; sem BIOS, iniciar diretamente pelo cartucho.
- [x] Boot com BIOS selecionada executa BIOS antes do jogo e preserva handoff pelo controle de memoria.
- [x] Soft reset reinicia somente a CPU/sessao ativa sem reabrir ROM nem forcar novo boot da BIOS.
- [x] GUI usa seletores nativos de ROM e BIOS sem copiar ou versionar arquivos.
- [x] Biblioteca local por hash/header com metadados, abertura na mesma janela e apelidos configurados pelo usuario.
- [x] Tela de configuracao simples para modo fiel/enhanced e toggles de enhancements.
- [x] GUI: SRAM automatica e quick-save/load por hash em `%LOCALAPPDATA%`, com validacao de estado.
- [x] GUI: escala inteira de janela entre 1x e 6x, configuravel e persistente.
- [x] GUI: remapeamento persistente de direcional, botoes e Pause/NMI, com troca de conflitos.
- [x] GUI: gerenciamento visual de save states, screenshots BMP e gravacoes WAV, com seletores nativos.
- [x] Perfis por hash editaveis pela GUI: mapper, NTSC/PAL, configuracao atual, aplicacao imediata e remocao.
- [x] Tela nao modal de status com FPS, backends, modelo, CPU/VDP, mapper, fallback/recompiler e avisos de compatibilidade.
- [x] Empacotamento Windows em ZIP portátil com checksum e guia de uso.

### 7. Engenharia reversa e enhanced ports por jogo

- [x] Roadmap de engenharia reversa por hash em `docs/ENHANCED_PORT_ROADMAP.md`.
- [x] Inventario local privado por hash, fora do Git.
- [x] Log CSV de I/O por porta no smoke runner.
- [x] Log CSV de writes RAM/cart/mapper no smoke runner.
- [x] Log CSV de writes VRAM/CRAM/registradores VDP no smoke runner.
- [x] Filtros por faixa/endereco e porta para logs de engenharia reversa.
- [x] Trace filtrado por PC via cobertura, memoria, VDP e I/O por CSV.
- [x] Dump legivel de tilemap e sprite table.
- [x] Replay headless deterministico combina input com logs de I/O/memoria/VDP e dumps finais de VRAM/CRAM/tilemap/sprites.
- [ ] Relatorio local por hash com camera, scroll, HUD, entidades e bancos.
- [x] Renderer enhanced separado do renderer fiel, com superficies distintas no VDP.
- [x] Experimento de viewport vertical 224/240 por perfil e CLI.
- [ ] Hooks por PC/banco para modo hybrid/recompiled.
- [x] Fallback obrigatorio para framebuffer fiel 256x192 quando enhanced esta inativo ou inseguro.

### 8. Suporte futuro a Game Gear

Game Gear e proximo o bastante do Master System para aproveitar o nucleo Z80, cartucho, PSG e parte do VDP, mas deve entrar como plataforma explicita futura para nao quebrar a fidelidade SMS/SG atual.

- [x] Detectar headers que indicam Game Gear e reportar plataforma no `--dump-analysis`.
- [x] Avisar no host quando uma imagem Game Gear for aberta no modelo SMS atual.
- [x] Adicionar `ConsoleModel::GameGear` sem alterar o comportamento padrao de SMS/SG-3000.
- [x] Viewport 160x144 com janela/letterbox correta no host.
- [x] Paleta Game Gear 12-bit/4096 cores separada da CRAM SMS 6-bit.
- [x] Portas/input especificos, incluindo botao Start e stereo PSG.
- [x] Ajustar VDP/render para diferencas GG mantendo fallback SMS.
- [x] Perfis por hash/header para selecionar Game Gear automaticamente quando seguro.
- [x] Testes sinteticos de header, paleta, viewport e input GG.
- [x] Documentar limites sem prometer compatibilidade total antes de traces e ROMs permitidas.

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
- [x] Analise estatica considera entry points de reset, IRQ IM1 (`0x0038`) e NMI/Pause (`0x0066`).
- [x] Emissao inicial de funcoes por basic block com dispatcher de compatibilidade por PC.
- [ ] Emissao por bloco sem dispatcher monolitico para caminhos completamente levantados.
- [x] Comentarios de disassembly no C++ gerado.
- [x] Relatorio de analise: blocos, sucessores, opcodes levantados e fallback usado.
- [x] Relatorio de analise: deteccao heuristica de tabelas de ponteiros little-endian.
- [x] Relatorio de analise: acessos estaticos a I/O e writes diretos de mapper.
- [x] Config TOML inicial via `--config` para modelo, mapper, limite estatico, video/audio e enhancements basicos.
- [x] Config TOML com alvo/runtime/run/perfil, paths relativos locais e schema estrito.

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
- [x] Prefixo `ED`: aliases documentados/nao documentados, block transfer/search nos dois sentidos e encodings indefinidos como NOP.
- [x] Runtime Z80: flags X/Y em ALU, INC/DEC, CB/BIT, BIT indexado, 16-bit, controle do acumulador e block transfer/search.
- [x] Recompilador direto: helpers e caminhos levantados preservam X/Y nas familias emitidas, com binario gerado executado em teste.
- [x] Prefixo `ED`: flags de block I/O com soma dependente da direcao, overflow H/C, N, X/Y e paridade.
- [x] Prefixo `ED`: ciclos exatos contra suite externa conhecida, incluindo block I/O repetido.
- [x] Prefixos `DD/FD` iniciais: IX/IY, stack, `jp (ix/iy)`, loads absolutos, deslocamento `ld/inc/dec/alu`, `DD/FD CB`, `IXH/IXL/IYH/IYL`.
- [x] Prefixos `DD/FD`: fallback de prefixo ignorado para opcodes nao afetados e varredura completa sem caminhos nao implementados.
- [x] Prefixos `DD/FD` nao-I/O: semantica rara, flags e ciclos validados por vetores SingleStepTests externos.
- [x] Interrupt mode: IM 0/1/2, `ei/di`, `reti/retn`.
- [x] Interrupts: IRQ maskable inicial, delayed `ei`, servico NMI inicial.
- [x] Interrupts: acknowledge IRQ/NMI com stack, IFF, saida de HALT, incremento R e ciclos IM1/IM2 validados.
- [x] Runtime Z80: registrador R preserva bit 7 e conta M1 em prefixos, cadeias DD/FD e ciclos de HALT.
- [x] Recompilador direto: contagem M1 do registrador R em base, ED/DD e HALT sem duplicar o fallback, validada no binario gerado.
- [x] Recompilador direto: atraso de EI promove IFF1/IFF2 antes da instrucao seguinte, inclusive NOP/HALT e fallback.
- [x] Ciclos condicionais: matriz local cobre `RET`/`JP`/`CALL` nas 8 condicoes, `JR`, `DJNZ` e block repeat/final.
- [x] Recompilador direto: `CALL` tomado/nao tomado validado no binario gerado com PC, SP, ciclos e registrador R.
- [x] Interrupts: NMI ligado ao Pause.
- [ ] Interrupts: prioridades e timing restantes contra trace externo.
- [x] Flags por sequencia validadas contra 1.604 vetores externos SingleStepTests, incluindo I/O, X/Y, DAA, CP, BIT e blocos.
- [x] Cycle counts por sequencia validados contra 1.604 vetores externos SingleStepTests.

## Memoria e Cartucho

- [x] Mapa Z80 de 64 KiB.
- [x] Slots de ROM de 16 KiB para SMS.
- [x] Escrita RAM em `0xC000-0xFFFF`.
- [x] Espelhamento correto de RAM de 8 KiB.
- [x] BIOS opcional local, carregada por `--bios` e ignorada pelo Git.
- [x] Replay headless registra BIOS/cartucho/RAM por frame e reporta o handoff BIOS -> jogo.
- [x] Portas espelhadas de VDP/PSG/counters normalizadas no barramento (`0x40-0x7f`, `0x80-0xbf`).
- [x] SMapper base: slots de ROM, janela fixa de 1 KiB, registradores espelhados em RAM e SRAM de cartucho em dois bancos.
- [x] SMapper: SRAM e autodeteccao de mapper respeitam o slot de cartucho habilitado pela porta `0x3E`.
- [x] Load/save/dump local de SRAM de cartucho.
- [x] Remocao automatica de header generico de 512 bytes.
- [x] Deteccao inicial de header `TMR SEGA` em offsets comuns no relatorio `--dump-analysis`.
- [x] API compartilhada de metadados de cartucho para CLI, host, perfis e futura GUI.
- [x] Checksum de header por tamanho declarado, reportado como diagnostico.
- [x] Selecao local de mapper por CLI/host: `auto`, `plain`, `smapper`, `cmapper`, `kmapper`, `k8k`.
- [x] Cartuchos lineares pequenos usam `plain` por padrao no modo `auto`.
- [x] CMapper inicial com registradores em `0x0000`, `0x4000`, `0x8000`.
- [x] KMapper inicial com registrador em `0xA000`.
- [x] K8K inicial com bancos de 8 KiB e registradores `0x0000-0x0003`.
- [x] Deteccao de header de ROM, validacao de tamanho e geracao/rebuild de checksum e regiao.
- [x] Heuristicas de modelo/regiao para BIOS sem header padrao, validadas com imagens permitidas.
- [ ] SMapper restante: variantes com write protect e EEPROM, quando identificadas por perfil/hardware.
- [x] Perfis locais por hash podem forcar mapper em ROMs maiores sem guardar caminhos privados.
- [x] Heuristica automatica trava na primeira familia de registradores comprovada e ignora writes com cartucho desconectado.
- [ ] Refinar heuristica automatica para imagens ambiguas sem writes de mapper conclusivos.
- [x] CMapper com janela SRAM de 8 KiB em `0xA000-0xBFFF`, dirty/load/save e state restore.
- [ ] CMapper EEPROM e edge cases adicionais quando identificados por perfil/hardware.
- [ ] KMapper/K8K completos usados por ROMs especificas.
- [x] SG-3000/TMS com RAM interna de 1 KiB espelhada em `0xC000-0xFFFF`.
- [ ] SG-3000 cartridge RAM e layouts especiais quando identificados por perfil/hardware.
- [x] Porta de controle de memoria `0x3E`: bits centrais de BIOS, cartucho e RAM de trabalho modelados, testados e expostos no overlay.
- [x] Modelar porta `0x3E`: expansao, cartucho, card, work RAM, BIOS, I/O e bits reservados.
- [ ] Modelar portas `0xDE-0xDF` e diferencas regionais/perifericos quando houver ROM local que dependa disso.

## VDP

- [x] VRAM, CRAM, registradores e portas iniciais.
- [x] Leitura VRAM bufferizada: command prefetch, incremento/wrap de endereco e latch persistido no save state v7.
- [x] TMS9918/SG-3000 Graphics I inicial: background tilemap 1bpp, color table e paleta TMS.
- [x] TMS9918/SG-3000 sprites iniciais: SAT, pattern table, cor, sentinel, overflow/collision basicos.
- [x] TMS9918/SG-3000 sprites 16x16 compostos por quatro patterns e zoom 32x32.
- [x] TMS9918/SG-3000 overflow reporta o indice do quinto sprite nos bits baixos do status.
- [x] TMS9918/SG-3000: modos padrao Graphics I, Graphics II, Text e Multicolor selecionados por M1/M2/M3.
- [x] SMS Mode 4 de 192 linhas: tiles 9-bit, paletas, flips, prioridade, scroll e mascaras.
- [x] SMS Mode 4 estendido de 224/240 linhas, VBlank, sprites, dumps e viewport no host.
- [x] Mode 4: hscroll/vscroll basicos, lock de topo, lock de direita por coluna de tela e coluna esquerda em branco.
- [x] Mode 4: sentido de R8 validado com wrap; origem usa `x - R8` e desloca o background para a direita.
- [x] Mode 4: scroll vertical em tela 192 linhas envolve na name table 32x28, a cada 224 pixels.
- [x] Mode 4: backdrop por registrador 7, mask de name table e blanking aplicado tambem sobre sprites.
- [x] Sprites 8x8 iniciais com transparencia.
- [x] Sprites: deslocamento horizontal, zoom inicial e wrap vertical.
- [x] Sprites: base de pattern via registrador 6 e 8x16 usando tile par/impar.
- [x] Sprites: colisao e overflow iniciais.
- [x] Sprites SMS/TMS: colisao preserva prioridade visual do menor indice da SAT em pixels sobrepostos.
- [x] Sprites SMS/TMS: enhancements nao geram colisao de hardware com sprites alem do limite original por scanline.
- [x] Sprites: prioridade inicial de background sobre sprite no Mode 4.
- [x] Sprites: limite por scanline, prioridade, colisao sob blanking e edge cases sinteticos.
- [x] Paleta CRAM inicial, backdrop/display disabled e framebuffer com background Mode 4 basico.
- [x] VBlank, line interrupts e status flags iniciais.
- [x] VDP: line IRQ separado dos bits de status, overflow/collision preservados e latch de controle resetado por acesso a dados/status.
- [x] Timing de VDP configuravel pelo HostRuntime, salvo em save state e exposto no overlay/debug snapshot.
- [x] Timing NTSC/PAL configuravel por HostRuntime, CLI headless, host Win32 e perfis locais.
- [x] VCounter usa remapeamento separado para frames NTSC/PAL em modo 192 linhas.
- [x] VCounter usa sequencias NTSC/PAL especificas para modos 224/240 linhas.
- [x] Pause/NMI do SMS conectado ao runtime.
- [x] Dump de tilemap/sprite table para engenharia reversa local.
- [x] Renderer enhanced experimental sem alterar framebuffer, status ou prioridade do passe fiel.

## PSG / Audio

- [x] Parser inicial do latch SN76489.
- [x] Gerar ondas dos 3 canais de tom.
- [x] Canal de ruido inicial.
- [x] Tabela de volume/log attenuation inicial.
- [x] Dump WAV local pelo smoke runner.
- [x] Dump VGM local de writes PSG pelo smoke runner.
- [x] Infraestrutura inicial YM2413/OPLL: portas, audio control, logging e mix no host.
- [x] YM2413/OPLL via emu2413: instrumentos, envelopes, rhythm mode, LFO, ruido e tabelas integrados.
- [x] Mixer estereo PSG+FM e sample rate configuravel no runtime/WAV.
- [x] Buffer de audio para host backend headless.
- [x] Saida Win32 waveOut inicial no host.
- [x] Contadores de fila, underrun e buffers descartados no host.
- [x] Volume, mute e latencia configuravel no host Win32.
- [x] Sample rate configuravel no HostRuntime, CLI headless, host Win32, WAV dumps e perfis locais.
- [x] Mixer estereo e sample rate configuravel no host Win32.

## Entrada

- [x] Leitura active-low do player 1.
- [x] Player 2.
- [x] Botao Pause do console.
- [ ] Light Phaser.
- [ ] Paddle/Sports Pad, se desejado.

## Host Runtime

- [x] Loop host de janela com video e input inicial.
- [x] Loop host de janela com audio em tempo real inicial.
- [x] Pre-buffer inicial e telemetria de underrun no host de janela.
- [x] Controle de latencia configuravel, pausa e volume no host de janela.
- [x] Pausa usa `waveOutPause/Restart`, preservando fila e chips sem flush; reset/state load limpam audio obsoleto.
- [ ] Backend SDL2 opcional.
- [x] Backend Win32 inicial para desenvolvimento no Windows.
- [x] HostRuntime headless com framebuffer, input por frame e buffer de audio.
- [x] Roteiro CSV deterministico de input por frame no host headless.
- [x] Log CSV por frame com hash do framebuffer, faixa de PC, mapper e audio nao-zero.
- [x] Save RAM em arquivo local pelo smoke runner.
- [x] Save RAM em arquivo local pelo host Win32.
- [x] Estado serializavel inicial para save states.
- [x] Save states com versao inicial e validacao de ROM hash/modelo.
- [x] Save states v14 com migracao de v1-v13, estados emu2413/Nuked-OPN2, Game Gear, MEMPTR/Q, framebuffer 240 e identidade.
- [x] Save state v7 preserva o read buffer do VDP e continua lendo formatos v1-v6.
- [x] Save state v8 preserva o lock da familia de mapper e continua lendo formatos v1-v7.
- [x] Debug overlay de PC, FPS, frame, modo runtime e audio.
- [x] Debug overlay com estado de pausa, volume e latencia.
- [x] Debug overlay com timing de scanline/frame do VDP.
- [x] Debug overlay com registradores completos de CPU, flags de interrupcao, cycles e halt.
- [x] Debug overlay com contadores interpretado, recompilado e fallback por frame em tempo real.

## GUI

- [x] GUI inicial usa componentes Win32 nativos junto ao backend existente.
- [x] Fluxo inicial abre ROM, permite BIOS local opcional e inicia o jogo sem terminal.
- [x] Persistir configuracoes em arquivo local ignoravel, sem caminhos sensiveis no repositorio.
- [x] Persistir SRAM e quick state anonimos por hash fora do repositorio.
- [x] Controles visuais para pausa, reset, overlay e enhancements de vídeo opcionais.
- [x] Lista de ate dez jogos recentes usando somente caminhos locais do usuario, com poda de arquivos ausentes.
- [x] Dialogo de compatibilidade explicando quando uma melhoria altera fidelidade.

## Enhancements Opcionais

- [x] `EnhancementConfig` integrado ao runtime inicial.
- [x] Modo fiel como padrao imutavel.
- [x] `disable_sprite_limit`.
- [x] `reduce_flicker`.
- [x] Perfis por jogo por hash local.
- [x] Audio FM opcional inicial por `enable_fm`.
- [x] Relatorio/debug mostrando enhancements ativos no smoke/host.
- [ ] Audio FM fiel validado contra referencias.

## Testes

- [x] Smoke test de programa Z80 minimo.
- [x] Varredura automatica de todas as entradas base, ED, DD e FD sem caminhos nao implementados.
- [x] Teste de conformidade com todas as 1.604 sequencias de SingleStepTests/z80 em revisao fixada e licenciada, incluindo I/O deterministico.
- [x] ROM sintetica integrada para mapper, VDP, PSG e input via CPU/HostRuntime.
- [ ] Comparacao de traces com emulador de referencia.
- [x] Testes do gerador C++ compilando o arquivo emitido.
- [x] Teste do gerador C++ linkando/executando o binario emitido e validando estado/flags.
- [x] Teste do relatorio de analise estatica do gerador.
- [x] Teste do dump WAV do smoke runner.
- [x] Teste do dump VGM PSG do smoke runner.
- [x] Teste do controle/log FM no runtime e na CLI.
- [x] Teste do log I/O e dumps tilemap/sprite no runtime e na CLI.
- [x] Teste de watchpoints RAM/VDP/I/O no runtime e na CLI.
- [x] Teste do save SRAM pelo host Win32 com ROM sintetica.
- [x] Teste de parser/hash de `GameProfile` e perfil aplicado no host.
- [ ] Testes de regressao com ROMs homebrew permitidas.
- [x] Smoke runner testado com ROMs locais privadas, sem imagens versionadas.

## Documentacao

- [x] README com build e uso.
- [x] Arquitetura inicial.
- [x] Status inicial do Z80.
- [x] Checklist de continuidade.
- [x] Roadmap de engenharia reversa/enhanced ports.
- [x] Guia de criacao de port por ROM.
- [x] Especificacao inicial de config TOML em `config/example.toml` e README.
- [x] Guia de build/toolchain.
- [x] Guia de contribuicao e estilo de codigo.
