# GUI Inicial para Testes

No Windows, abra `build\zig-debug\SG3000Recomp.exe` com duplo clique. `sgrecomp_host.exe` oferece o mesmo host com o nome técnico usado por scripts e testes.

1. Escolha uma ROM local (`.sms`, `.sg`, `.bin` ou `.rom`).
2. Escolha se deseja carregar uma BIOS local. **Sim** abre o seletor de BIOS; **Não** inicia diretamente pelo cartucho.
3. A janela do jogo abre com vídeo, teclado, áudio e overlay de diagnóstico.

A barra de menus oferece pausa e reset, liga ou desliga o overlay e permite testar
`reduce_flicker` e `disable_sprite_limit` durante o jogo. As marcas ao lado de cada
opção mostram o estado ativo, inclusive quando ele veio da linha de comando ou de um perfil.

Nenhuma ROM ou BIOS é copiada para o projeto. Cancelar qualquer seletor encerra o fluxo sem alterar arquivos.

## Controles

- Setas: direcional do jogador 1.
- `Z` / `X`: botões 1 e 2.
- `Enter`: Pause/NMI do Master System.
- `Space`: pausa a emulação.
- `R`: reset.
- `M`: mute.
- `+` / `-`: volume.
- `F1`: mostra ou oculta o overlay.
- `F5`: grava quick-save.
- `F9`: carrega o quick-save da mesma ROM.

## Dados locais

No fluxo gráfico, a SRAM é carregada automaticamente ao abrir a ROM e salva ao fechar a janela. SRAM e quick-save usam somente o hash da ROM e ficam em:

```text
%LOCALAPPDATA%\SG3000Recomp\saves\
```

O quick-load valida modelo e hash antes de restaurar o estado. Esses arquivos não guardam nem expõem o caminho da ROM.

Usuários avançados ainda podem iniciar o mesmo executável pelo terminal com ROM, BIOS, mapper, perfil, saves e enhancements explícitos.

Esta é a GUI mínima para testes de jogabilidade. Biblioteca local, configurações persistentes, screenshots e gerenciamento visual dos saves continuam como evolução posterior.
