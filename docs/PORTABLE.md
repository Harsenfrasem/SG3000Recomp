# SG3000Recomp portátil

Este pacote contém a versão Windows pronta para teste, sem ROMs, BIOS ou saves.

## Iniciar

1. Extraia todo o ZIP para uma pasta gravável.
2. Abra `SG3000Recomp.exe` com duplo clique; a janela preta abre sem iniciar emulação.
3. Configure tela/controles e, se necessário, selecione uma BIOS no menu **Arquivo**.
4. Use **Arquivo > Abrir ROM...**. Quando a BIOS terminar e liberar o cartucho, o jogo inicia normalmente.

Cancelar um seletor mantém a janela e a sessão atual sem copiar o arquivo selecionado.

## Controles

- Setas: direcional.
- `Z` / `X`: botões 1 e 2.
- `Enter`: Pause/NMI do Master System ou Start no Game Gear.
- `Space`: pausa a emulação.
- `R`: soft reset.
- `M`: mute.
- `+` / `-`: volume.
- `F1`: overlay de diagnóstico.
- `F5` / `F9`: salvar e carregar rapidamente.

Os menus permitem voltar ao modo fiel ou ativar melhorias visuais com um aviso de
compatibilidade. SRAM, quick-save e preferências ficam somente em
`%LOCALAPPDATA%\SG3000Recomp`; nenhum caminho de ROM ou BIOS é gravado no pacote.

Em **Controles > Configurar teclado/controles USB...**, o host lista controles detectados
pela API joystick do Windows. Para remapear, escolha uma ação no menu **Controles** e pressione
uma tecla, botão, eixo analógico ou POV/direcional no popup; os vínculos USB ficam salvos junto
das preferências locais.

Um perfil SMS enhanced pode solicitar viewport vertical 224/240. Essa saída usa um framebuffer
separado; voltar ao modo fiel restaura imediatamente 256x192 sem reutilizar pixels aprimorados.

Em **Exibição**, escolha escala limpa/pixel perfect, ajuste proporcional à janela ou aspect
ratio corrigido 4:3 para SMS/SG-3000. Quando a imagem não ocupa todo o cliente, as barras
pretas exibem uma legenda discreta com resolução, escala e estado enhanced.

ROMs Game Gear com header válido são reconhecidas automaticamente e usam a janela LCD
160x144, paleta de 12 bits e áudio PSG estéreo. Link cable e compatibilidade integral do
catálogo ainda não são prometidos.

A fila waveOut ajusta automaticamente sua meta após underruns e volta gradualmente à
latência configurada quando estabiliza. Pausar preserva a fila; reset e load-state descartam
som pertencente ao estado anterior. O YM2413 usa o núcleo emu2413 com instrumentos, envelopes,
LFO e rhythm mode; o YM2612 é uma extensão opcional não histórica.

**Exibição > Status detalhado...** mantém uma janela auxiliar atualizada com FPS, CPU,
VDP, mapper, áudio, backends e uso do executor recompilado/fallback, sem pausar o jogo.

**Arquivo > Perfil do jogo** salva por hash o mapper, NTSC/PAL e a configuração ativa para
a ROM atual. O perfil pode ser alterado ou removido sem reiniciar o aplicativo e fica somente
em `%LOCALAPPDATA%\SG3000Recomp\profiles.txt`.

**Arquivo > Biblioteca local** lista jogos já abertos por hash/header e permite configurar
um apelido para o jogo atual. A biblioteca apenas referencia os arquivos originais e remove
automaticamente entradas cujo arquivo deixou de existir.

Em **Arquivo**, também é possível salvar/carregar estados `.sgstate`, capturar a tela em
BMP e gravar a saída de áudio em WAV. Cancelar o destino de uma gravação mantém o áudio na
memória para salvar depois; a opção de descartar o remove sem criar arquivos.

## Conteúdo

- `SG3000Recomp.exe`: aplicativo e runtime.
- `nuked_opn2.dll`: núcleo YM2612/YM3438 opcional e substituível, por Nuke.YKT.
- `EMU2413-LICENSE.txt`: licença MIT do núcleo YM2413/OPLL integrado.
- `README.md`: este guia.
- `THIRD_PARTY_NOTICES.md` e `NUKED-OPN2-LICENSE.txt`: créditos, procedência e licença LGPL do
  núcleo YM2612; a licença MIT do emu2413 está no arquivo listado acima.

O pacote não inclui jogos, firmware proprietário nem arquivos do usuário.
