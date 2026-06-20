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
- `Enter`: Pause/NMI do Master System.
- `Space`: pausa a emulação.
- `R`: soft reset.
- `M`: mute.
- `+` / `-`: volume.
- `F1`: overlay de diagnóstico.
- `F5` / `F9`: salvar e carregar rapidamente.

Os menus permitem voltar ao modo fiel ou ativar melhorias visuais com um aviso de
compatibilidade. SRAM, quick-save e preferências ficam somente em
`%LOCALAPPDATA%\SG3000Recomp`; nenhum caminho de ROM ou BIOS é gravado no pacote.

Em **Arquivo**, também é possível salvar/carregar estados `.sgstate`, capturar a tela em
BMP e gravar a saída de áudio em WAV. Cancelar o destino de uma gravação mantém o áudio na
memória para salvar depois; a opção de descartar o remove sem criar arquivos.

## Conteúdo

- `SG3000Recomp.exe`: aplicativo e runtime.
- `nuked_opn2.dll`: núcleo YM2612/YM3438 opcional e substituível, por Nuke.YKT.
- `README.md`: este guia.
- `THIRD_PARTY_NOTICES.md` e `NUKED-OPN2-LICENSE.txt`: crédito, procedência e LGPL-2.1-or-later.

O pacote não inclui jogos, firmware proprietário nem arquivos do usuário.
