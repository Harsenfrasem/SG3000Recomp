# GUI Inicial para Testes

No Windows, abra `build\zig-debug\SG3000Recomp.exe` com duplo clique. `sgrecomp_host.exe` oferece o mesmo host com o nome técnico usado por scripts e testes.

1. Escolha uma ROM local (`.sms`, `.sg`, `.bin` ou `.rom`).
2. Escolha se deseja carregar uma BIOS local. **Sim** abre o seletor de BIOS; **Não** inicia diretamente pelo cartucho.
3. A janela do jogo abre com vídeo, teclado, áudio e overlay de diagnóstico.

A barra de menus oferece pausa e reset, liga ou desliga o overlay e permite testar
`reduce_flicker` e `disable_sprite_limit` durante o jogo. As marcas ao lado de cada
opção mostram o estado ativo, inclusive quando ele veio da linha de comando ou de um perfil.
O menu **Melhorias** também alterna explicitamente entre o modo fiel e o enhanced.
Antes da primeira ativação enhanced da sessão, a GUI explica que essas opções podem
alterar a aparência original e pede confirmação. Voltar ao modo fiel desliga as duas melhorias visuais.

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

O modo de execução, overlay, melhorias de vídeo, mute e volume ficam em `settings.ini`, diretamente
na pasta `SG3000Recomp`. O arquivo contém apenas opções; caminhos de ROM e BIOS não são salvos.

O quick-load valida modelo, ROM, BIOS e configuração de perfil antes de restaurar estados
novos. Estados antigos continuam legíveis com a identidade disponível em sua versão.
Esses arquivos não guardam nem expõem caminhos locais.

Usuários avançados ainda podem iniciar o mesmo executável pelo terminal com ROM, BIOS, mapper, perfil, saves e enhancements explícitos.

Esta é a GUI mínima para testes de jogabilidade. Biblioteca local, screenshots e gerenciamento visual dos saves continuam como evolução posterior.
