# Áudio e fidelidade

O host mistura PSG, YM2413 opcional e a extensão YM2612 em PCM estéreo de 16 bits. O PSG
SN76489 permanece o caminho padrão: apenas ativar `enable_fm` torna o YM2413 detectável, mas
não silencia o PSG. O software emulado precisa escrever o controle de áudio `$F2`; os modos
`0` e `3` mantêm PSG, enquanto `1` solicita FM sem PSG conforme a interface modelada.

O YM2413 atual é um sintetizador aproximado para diagnóstico de portas, perfis, logs e mix.
Ele ainda não implementa instrumentos, envelopes, rhythm mode, LFO e tabelas com fidelidade
OPLL comprovada. Portanto, `enable_fm` não deve ser apresentado como reprodução histórica
fiel. O YM2612/Nuked-OPN2 é mais completo, mas continua sendo uma extensão não histórica do
SMS/SG-3000 e exige software preparado para as portas `$F4-$F7`.

## Fila do host Windows

`--audio-latency-ms` define a latência desejada. O controlador começa nesse valor, aumenta a
meta em passos de 10 ms após underruns e, depois de um período estável, reduz em passos de
5 ms até voltar ao valor solicitado. A telemetria mostra latência enfileirada, alvo solicitado,
alvo adaptativo, underruns e drops.

Pausar usa `waveOutPause` e retomar usa `waveOutRestart`: os buffers e o estado dos chips são
preservados. Reset, troca de sessão e carregamento de estado usam `waveOutReset`, pois nesses
casos o áudio antigo não pertence mais ao estado em execução.
