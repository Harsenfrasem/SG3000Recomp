# Guia de criação de port por ROM

Este fluxo cria conhecimento e configuração local por jogo sem colocar a ROM, BIOS ou dados extraídos no Git.

## 1. Identificar o alvo

Mantenha a imagem em `local-roms/` e gere metadados locais:

```powershell
.\build\zig-debug\sgrecomp_host.exe .\local-roms\game.sms --print-hash
.\build\zig-debug\sgrecomp.exe .\local-roms\game.sms --dump-analysis .\out\game-analysis.txt
```

Registre apenas conclusões técnicas genéricas. Não publique nome de arquivo local, hash privado ou conteúdo da imagem.

## 2. Criar um perfil local

Copie `config/profiles.example.txt` para uma pasta ignorada, como `out/profiles/`, e configure somente o necessário: modelo, mapper, vídeo, áudio e enhancements. O perfil deve usar o hash produzido localmente e nunca um caminho de ROM.

Teste primeiro no modo fiel:

```powershell
.\build\zig-debug\sgrecomp_host.exe .\local-roms\game.sms --profile .\out\profiles\game.txt
```

Ative enhancements individualmente e compare sempre a mesma cena/input.

## 3. Capturar uma reprodução determinística

Crie um CSV de entrada conforme `docs/INPUT_SCRIPT.md` e rode o host headless:

```powershell
.\build\zig-debug\sgrecomp.exe .\local-roms\game.sms --run-host --frames 1200 `
  --input-script .\out\game-input.csv --profile .\out\profiles\game.txt `
  --dump-frame-log .\out\game-frames.csv --dump-coverage .\out\game-coverage.csv
```

Use os logs de memória, I/O e VDP descritos em `docs/ENHANCED_PORT_ROADMAP.md` para reduzir o problema a um comportamento de hardware reproduzível.

## 4. Transformar o achado em correção geral

1. Crie uma ROM/sequência sintética mínima em `tests/`.
2. Faça o teste falhar antes da correção.
3. Corrija runtime, recompiler ou host na camada responsável.
4. Rode os testes completos e repita o replay privado.
5. Documente o comportamento do hardware sem mencionar o jogo privado.

Não crie hacks condicionados a nome de arquivo ou caminho. Use perfil por hash apenas quando a diferença for realmente específica do cartucho; comportamento comum pertence ao runtime.

## 5. Gate de entrega

Um port só pode ser chamado de jogável quando satisfizer `docs/PLAYABLE_STATUS.md`: entrada em gameplay, cinco minutos estáveis, áudio, save-state e comparação faithful/enhanced. Se usar SRAM, valide também persistência após fechar e reabrir.

Antes do commit, confirme que `git status` não contém dumps, saves, imagens ou perfis privados.
