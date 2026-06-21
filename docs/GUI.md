# GUI Inicial para Testes

No Windows, abra `build\zig-debug\SG3000Recomp.exe` com duplo clique. `sgrecomp_host.exe` oferece o mesmo host com o nome técnico usado por scripts e testes. A janela abre ociosa, com tela preta, sem executar CPU e sem mostrar seletores automaticamente.

ROMs Game Gear (`.gg` ou headers Game Gear em `.bin/.rom`) selecionam automaticamente o modelo
portátil e redimensionam a área de jogo para 160x144. A ação remapeável `Pause/NMI` funciona como
Start nesse modelo. Consulte `GAME_GEAR.md` para os limites atuais.

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

**Arquivo > Biblioteca local** reúne até cinquenta jogos já abertos, identificados pelo hash
e pelos metadados do header (plataforma, região e código de produto quando disponíveis).
Escolher uma entrada abre o jogo na mesma janela. Para o jogo atual, é possível definir ou
remover um apelido; ele passa a ser o nome mostrado no menu sem renomear nem copiar a ROM.
O menu **Melhorias** também alterna explicitamente entre o modo fiel e o enhanced.
Antes da primeira ativação enhanced da sessão, a GUI explica que essas opções podem
alterar a aparência original e pede confirmação. Voltar ao modo fiel desliga as duas melhorias visuais.

Em **Arquivo > Perfil do jogo**, a configuração pode ser persistida para a ROM atual pelo
hash, sem guardar o caminho da ROM. O submenu permite escolher mapper e NTSC/PAL, salvar o
modelo, enhancements e parâmetros de áudio atualmente ativos, ou remover o perfil. Cada
alteração salva a SRAM e recarrega a mesma ROM imediatamente. Por padrão, esses perfis ficam
em `%LOCALAPPDATA%\SG3000Recomp\profiles.txt`; um arquivo indicado por `--profile` continua
sendo respeitado quando a GUI é iniciada pelo terminal.

**YM2612 experimental (portas F4-F7)** ativa o núcleo [Nuked-OPN2](https://github.com/nukeykt/Nuked-OPN2)
em modo compatível com YM2612. Essa extensão não pertence ao hardware SMS/SG-3000 e não
substitui automaticamente o PSG ou o YM2413: software preparado deve escrever endereço/dados
nos pares `F4/F5` (banco 0) e `F6/F7` (banco 1). Voltar ao modo fiel desativa a extensão.

Em **Exibição > Tamanho da tela**, a janela pode usar escala inteira de `1x` a `6x` sem
alterar a resolução interna do console. Em **Controles**, cada ação do jogador pode ser
remapeada: escolha a ação e pressione a nova tecla. Se a tecla já estiver em uso, os dois
vínculos são trocados para evitar conflitos. Há também uma opção para restaurar o padrão.

**Exibição > Status detalhado...** abre uma janela auxiliar não modal que permanece
atualizada durante o jogo. Ela mostra modelo e padrão de vídeo, backends Win32/waveOut,
registradores da CPU, estado do VDP e mapper, FPS, áudio, execução interpretada/recompilada,
fallback por frame e avisos quando o modo enhanced ou o YM2612 experimental estão ativos.
A janela pode permanecer aberta enquanto ROM, BIOS e configurações são trocadas.

No status de áudio, `target` é a latência solicitada e `adaptive` é o alvo atual da fila.
Underruns elevam o alvo adaptativo; após estabilidade ele retorna gradualmente ao valor
pedido. Pausar preserva a fila de áudio, enquanto reset e carregamento de estado descartam
som obsoleto. Os limites dos chips opcionais estão detalhados em `docs/AUDIO.md`.

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
o menu de recentes; `game-library.txt` mantém a biblioteca e os apelidos; `profiles.txt`
guarda configurações por hash. Caminhos de BIOS não são salvos. Esses arquivos permanecem
fora do repositório.

O quick-load valida modelo, ROM, BIOS e configuração de perfil antes de restaurar estados
novos. Estados antigos continuam legíveis com a identidade disponível em sua versão.
Os save states não guardam nem expõem caminhos locais.

## Arquivos criados pela GUI

Com um jogo aberto, o menu **Arquivo** permite salvar e carregar estados `.sgstate` com
seletor nativo, além do quick-save em `F5`/`F9`. O carregamento valida a identidade da ROM,
modelo, BIOS e perfil antes de alterar a sessão.

**Salvar screenshot...** exporta exatamente a área ativa do jogo em BMP de 24 bits. Em
**Gravação de áudio**, é possível iniciar uma captura, parar e salvar em WAV PCM estéreo de
16 bits, salvar novamente a última captura mantida na memória ou descartá-la. Cancelar o
seletor de destino preserva a gravação para uma tentativa posterior. A captura para
automaticamente após duas horas para limitar o uso de memória; trocar ROM ou BIOS também
interrompe a captura sem descartar o áudio já gravado.

Usuários avançados ainda podem iniciar o mesmo executável pelo terminal com ROM, BIOS, mapper, perfil, saves e enhancements explícitos.

Esta GUI já cobre o fluxo necessário para testes de jogabilidade e coleta de evidências sem reiniciar o emulador.
