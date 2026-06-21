# Áudio e fidelidade

O host mistura PSG, YM2413 opcional e a extensão YM2612 em PCM estéreo de 16 bits. O PSG
SN76489 permanece o caminho padrão: apenas ativar `enable_fm` torna o YM2413 detectável, mas
não silencia o PSG. O software emulado precisa escrever o controle de áudio `$F2`; os modos
`0` e `3` mantêm PSG, enquanto `1` solicita FM sem PSG conforme a interface modelada.

O YM2413 usa o núcleo emu2413 na revisão fixada `813cff6`, com instrumentos ROM do YM2413,
envelopes, AM/PM LFO, rhythm mode, ruído e tabelas OPLL. O runtime avança o núcleo na taxa interna
do chip (clock/72), preserva seu estado exato no save-state v14 e mantém logs/controle `$F0-$F2`
separados da síntese. Ainda falta uma matriz de regressão com jogos permitidos para declarar
compatibilidade de catálogo; isso não reduz a fidelidade do núcleo integrado.

Save states v1-v13 continuam carregáveis. Como eles pertencem ao sintetizador aproximado antigo,
seus registradores são reaplicados ao emu2413 e o envelope FM recomeça; estados v14 preservam a
continuidade exata do núcleo.

O YM2612/Nuked-OPN2 continua sendo uma extensão não histórica do SMS/SG-3000 e exige software
preparado para as portas `$F4-$F7`.

## Fila do host Windows

`--audio-latency-ms` define a latência desejada. O controlador começa nesse valor, aumenta a
meta em passos de 10 ms após underruns e, depois de um período estável, reduz em passos de
5 ms até voltar ao valor solicitado. A telemetria mostra latência enfileirada, alvo solicitado,
alvo adaptativo, underruns e drops.

Pausar usa `waveOutPause` e retomar usa `waveOutRestart`: os buffers e o estado dos chips são
preservados. Reset, troca de sessão e carregamento de estado usam `waveOutReset`, pois nesses
casos o áudio antigo não pertence mais ao estado em execução.
