# Roadmap de Engenharia Reversa e Enhanced Ports

Este documento descreve como o projeto deve evoluir para melhorias por jogo, incluindo expansao de viewport, ajustes de HUD, reducao de flicker, perfis e hooks do recompilador.

ROMs, BIOS, saves, screenshots proprietarias, dumps de memoria privados e caminhos locais nao devem ser versionados. Todo trabalho por jogo deve ser identificado por hash local e documentado em termos tecnicos genericos.

## Inventario Local Privado

Estado atual observado localmente:

- 3 ROMs privadas disponiveis em `local-roms/`.
- Formatos presentes: `.sms` e `.bin`.
- Tamanhos presentes: 128 KiB e 256 KiB.
- BIOS privadas disponiveis em `local-bios/`.

O repositorio deve guardar somente ferramentas, formatos, exemplos neutros e perfis demonstrativos sem hash real de acervo privado.

## Objetivo

Manter dois caminhos claros:

- **Modo fiel:** emulacao/recompilacao com limites originais, comportamento previsivel e compatibilidade como prioridade.
- **Modo enhanced:** melhorias opcionais por perfil, sem alterar o modo fiel por acidente.

Melhorias profundas como viewport maior, cenario expandido, HUD reposicionado e patches de camera devem ser tratadas como enhanced ports por jogo, nao como comportamento padrao do emulador.

## Fases De Trabalho

### 1. Inventario por hash

- [ ] Gerar hash local com `sgrecomp_host --print-hash`.
- [ ] Criar perfil local ignorado pelo Git para cada ROM privada.
- [ ] Registrar modelo, necessidade de BIOS, mapper suspeito e status visual/audio.
- [ ] Nunca salvar nome de arquivo local, caminho absoluto, ROM ou BIOS no repositorio.

### 2. Baseline fiel

- [ ] Rodar cada ROM no host em modo `accurate`.
- [ ] Capturar frame BMP, audio WAV/VGM e cobertura de PC em `out/`.
- [ ] Registrar localmente se chega em tela, se toca audio e se depende de BIOS.
- [ ] Comparar o comportamento com o smoke runner para separar bugs do host e bugs do runtime.

### 3. Mapas tecnicos por jogo

- [ ] Identificar bancos de ROM acessados durante boot e gameplay.
- [ ] Mapear regioes de RAM que parecem camera, scroll, estado de fase, jogador, entidades e HUD.
- [ ] Mapear escritas de VRAM/CRAM e tabelas de sprites.
- [ ] Detectar rotinas quentes por cobertura de PC e blocos basicos.
- [ ] Exportar relatorios locais por hash, sem copiar dados da ROM para docs versionadas.

### 4. Instrumentacao necessaria

- [ ] Watchpoints de RAM, VRAM, CRAM e I/O por faixa.
- [ ] Trace filtrado por PC, banco, porta e tipo de acesso.
- [x] Dump de tilemap/sprite table em formato local legivel, inclusive no replay deterministico com input.
- [ ] Overlay debug com registradores completos, banco ativo, fallback count e scanline.
- [x] Marcadores de frame para correlacionar input, PC, mapper, audio e hash do framebuffer.

### 5. Enhanced renderer generico

- [ ] Separar renderer fiel e renderer enhanced.
- [ ] Reconstruir tilemap visivel a partir de VRAM sem alterar o estado original.
- [ ] Permitir canvas maior que 256x192 como superficie do host.
- [ ] Manter fallback para framebuffer fiel quando o jogo nao permitir extrapolacao segura.
- [ ] Adicionar modos de escala, aspect ratio e bordas informativas.

### 6. Viewport expansion por jogo

- [ ] Descobrir variaveis de camera/scroll por perfil.
- [ ] Validar se o jogo mantem dados de cenario alem da tela original.
- [ ] Criar `ViewportExpansion` por perfil com limites claros.
- [ ] Reposicionar HUD quando necessario.
- [ ] Bloquear expansion quando causar tiles invalidos, entidades invisiveis, colisao errada ou leitura fora do mapa conhecido.

### 7. Hooks e patches do recompilador

- [ ] Criar pontos de hook por PC/banco no modo hybrid/recompiled.
- [ ] Permitir patches pequenos e declarativos por perfil.
- [ ] Registrar cada patch com motivo, risco e modo afetado.
- [ ] Exigir toggle para patches que mudam gameplay, camera, timing ou colisao.
- [ ] Garantir que o modo `accurate` ignore todos os patches enhanced.

### 8. Validacao

- [ ] Comparar modo fiel e enhanced no mesmo trecho de jogo.
- [ ] Verificar que saves, input e audio continuam funcionando.
- [ ] Medir FPS, underruns, fallback count e estabilidade de frame.
- [ ] Guardar artefatos privados somente em `out/` ou `local-*`.
- [ ] Promover apenas ferramentas genericas e testes sinteticos para o repositorio.

## Tipos De Enhancement

### Genericos

- Escala limpa.
- Controle de latencia de audio.
- Overlay/debug.
- Reducao de flicker por limite de sprites.
- Perfis por hash.

### Por hardware

- Renderer enhanced com mais sprites.
- Renderizacao em superficie maior.
- Dumps de tilemap/sprites.
- FM opcional quando houver suporte tecnico.

### Por jogo

- Camera/viewport expandida.
- HUD reposicionado.
- Patches de scroll.
- Patches de limite de sprites especificos.
- Perfis de audio e latencia.

## Criterios Para Marcar Um Enhanced Port Como Viavel

- O modo fiel roda de forma estavel no trecho alvo.
- O perfil identifica o jogo por hash local.
- A camera/scroll foi localizada com watchpoints ou trace.
- O renderer consegue desenhar area extra sem inventar dados incorretos.
- HUD e sprites principais permanecem legiveis.
- Existe fallback imediato para 256x192 fiel.
- Nenhum dado proprietario foi versionado.

## Ordem Recomendada

1. Completar ferramentas de trace/watchpoint.
2. Melhorar VDP fiel e dumps de tilemap/sprites.
3. Criar formato local de relatorio por hash.
4. Escolher uma ROM local privada como primeiro estudo, sem documentar nome no Git.
5. Localizar camera/scroll/HUD.
6. Implementar renderer enhanced experimental.
7. Promover apenas a infraestrutura generica para o repositorio.
