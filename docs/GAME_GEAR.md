# Game Gear

O Game Gear é um modelo explícito do runtime, separado de Master System e SG-3000. ROMs com
header `TMR SEGA` marcado para Game Gear selecionam esse modelo automaticamente; um perfil local
por hash também pode usar `model = "gamegear"`. Na linha de comando, `--model gamegear` força o
modelo e uma seleção explícita sempre prevalece sobre a detecção por header.

O host apresenta somente a janela LCD central de 160x144 pixels, recortada da superfície Mode 4
de 256x192 nas coordenadas `(48, 24)`. Screenshots e dumps de frame usam o mesmo recorte. A CRAM
é independente da CRAM de SMS: são 32 cores de 12 bits em 64 bytes, no formato de dois bytes
`----BBBB GGGGRRRR`. O dump de CRAM de uma sessão Game Gear tem, portanto, 64 bytes.

O controle interno continua usando as direções e botões 1/2 configuráveis da GUI. A ação
`Pause/NMI` (Enter por padrão) funciona como Start no Game Gear e pode ser remapeada. A porta
`$00` expõe Start ativo em nível baixo e a porta `$06` controla o roteamento estéreo dos quatro
canais PSG; bits 4-7 habilitam o canal esquerdo e bits 0-3 o direito.

## Limites atuais

- O núcleo reutiliza o VDP Mode 4, mapper Sega, RAM e PSG validados pelo caminho SMS, com palette,
  viewport e portas específicas do Game Gear isoladas por modelo.
- A região reportada na porta `$00` permanece no perfil export/NTSC; seleção regional elétrica
  ainda não é configurável.
- Portas seriais/link cable, acessórios e multiplayer conectado não são emulados.
- Os testes cobrem header, seleção de modelo, viewport, palette, Start, estéreo, save state e dumps,
  mas ainda não constituem uma certificação de compatibilidade para todo o catálogo comercial.

Esses limites são deliberadamente explícitos: o projeto oferece um caminho Game Gear funcional
e testável, sem prometer compatibilidade total antes de regressões com imagens permitidas e traces
de referência.
