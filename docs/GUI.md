# GUI Inicial para Testes

No Windows, abra `build\zig-debug\SG3000Recomp.exe` com duplo clique. `sgrecomp_host.exe` oferece o mesmo host com o nome técnico usado por scripts e testes. A janela abre ociosa, com tela preta, sem executar CPU e sem mostrar seletores automaticamente.

1. Ajuste tamanho da tela, controles e melhorias se desejar.
2. Opcionalmente use **Arquivo > Selecionar BIOS...**. A BIOS fica selecionada apenas para a sessão.
3. Use **Arquivo > Abrir ROM...** e escolha uma imagem local (`.sms`, `.sg`, `.bin` ou `.rom`).
4. Sem BIOS, o cartucho inicia diretamente. Com BIOS, ela roda primeiro e entrega o controle ao jogo.

O viewport acompanha automaticamente os modos SMS de 192, 224 e 240 linhas.

A barra de menus oferece pausa e reset, liga ou desliga o overlay e permite testar
`reduce_flicker` e `disable_sprite_limit` durante o jogo. As marcas ao lado de cada
opção mostram o estado ativo, inclusive quando ele veio da linha de comando ou de um perfil.
O menu **Arquivo** permite trocar de jogo na mesma janela, reabrir um dos dez jogos recentes,
selecionar outra BIOS ou remover a BIOS ativa. A GUI salva a SRAM antes de substituir a sessão
e remove automaticamente da lista os arquivos que deixaram de existir. Selecionar ou remover
a BIOS com um jogo aberto recarrega esse jogo imediatamente no modo escolhido.
O menu **Melhorias** também alterna explicitamente entre o modo fiel e o enhanced.
Antes da primeira ativação enhanced da sessão, a GUI explica que essas opções podem
alterar a aparência original e pede confirmação. Voltar ao modo fiel desliga as duas melhorias visuais.

**YM2612 experimental (portas F4-F7)** ativa o núcleo [Nuked-OPN2](https://github.com/nukeykt/Nuked-OPN2)
em modo compatível com YM2612. Essa extensão não pertence ao hardware SMS/SG-3000 e não
substitui automaticamente o PSG ou o YM2413: software preparado deve escrever endereço/dados
nos pares `F4/F5` (banco 0) e `F6/F7` (banco 1). Voltar ao modo fiel desativa a extensão.

Em **Exibição > Tamanho da tela**, a janela pode usar escala inteira de `1x` a `6x` sem
alterar a resolução interna do console. Em **Controles**, cada ação do jogador pode ser
remapeada: escolha a ação e pressione a nova tecla. Se a tecla já estiver em uso, os dois
vínculos são trocados para evitar conflitos. Há também uma opção para restaurar o padrão.

Nenhuma ROM ou BIOS é copiada para o projeto. Cancelar um seletor mantém a janela e a sessão atual sem alterações.

## Controles

- Setas: direcional padrão do jogador 1.
- `Z` / `X`: botões 1 e 2 por padrão.
- `Enter`: Pause/NMI do Master System por padrão.
- `Space`: pausa a emulação.
- `R`: soft reset da CPU, sem reabrir a ROM e sem forçar novo boot da BIOS.
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

O modo de execução, escala da janela, controles, overlay, melhorias de vídeo/áudio, mute e volume ficam em `settings.ini`, diretamente
na pasta `SG3000Recomp`. `recent-games.txt` guarda localmente até dez caminhos de ROM para alimentar
o menu de recentes; caminhos de BIOS não são salvos. Esses arquivos permanecem fora do repositório.

O quick-load valida modelo, ROM, BIOS e configuração de perfil antes de restaurar estados
novos. Estados antigos continuam legíveis com a identidade disponível em sua versão.
Os save states não guardam nem expõem caminhos locais.

Usuários avançados ainda podem iniciar o mesmo executável pelo terminal com ROM, BIOS, mapper, perfil, saves e enhancements explícitos.

Esta é a GUI mínima para testes de jogabilidade. Screenshots e gerenciamento visual dos saves continuam como evolução posterior.
