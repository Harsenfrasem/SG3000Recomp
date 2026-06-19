# Contribuindo com o SG3000Recomp

## Antes de começar

- Trabalhe em uma branch curta e mantenha cada commit focado em um comportamento.
- Nunca adicione ROMs, BIOS, saves, screenshots proprietárias, hashes privados ou caminhos locais.
- Use ROMs sintéticas nos testes. Material privado deve ficar apenas nas pastas ignoradas descritas em `TESTE_LOCAL.txt`.
- Não marque itens de `docs/CHECKLIST.md` sem um teste ou outra evidência reproduzível.

## Build e validação

No Windows com o preset Zig:

```powershell
$env:PATH="$env:APPDATA\Python\Python314\Scripts;$env:PATH"
cmake --preset zig-debug
cmake --build --preset zig-debug
ctest --preset zig-debug --output-on-failure
```

Antes de enviar uma mudança de C++:

```powershell
cmake --preset lint
cmake --build --preset lint --target format-check
cmake --build --preset lint
ctest --preset lint --output-on-failure
git diff --check
```

A CI repete build, warnings-as-errors, formatação e testes em Windows, Linux e macOS.

## Estilo

- C++20, quatro espaços, sem tabs; `.clang-format` é a fonte de verdade mecânica.
- Preserve as fronteiras entre runtime, host, ferramentas e testes.
- Prefira tipos explícitos do projeto (`u8`, `u16`, `u32`, `u64`) para estado de hardware.
- Explique constantes de hardware e diferenças entre comportamento fiel e enhancement.
- O modo `accurate` não pode ativar melhorias silenciosamente.
- Toda alteração de serialização precisa manter leitura das versões anteriores ou documentar claramente a migração.

## Testes esperados

- CPU: estado final, flags e ciclos quando conhecidos.
- VDP/PSG/mapper/input: ROM ou sequência sintética mínima que reproduza o caso.
- Host/CLI: teste de integração sem abrir material privado.
- Correções descobertas com jogo comercial devem virar regressões sintéticas, sem copiar dados proprietários.

## Git e privacidade

Antes do commit:

```powershell
git status --short --ignored
git ls-files local-roms local-bios local-saves out '*.sms' '*.sg' '*.gg' '*.rom' '*.bin' '*.sav' '*.zip'
```

O segundo comando não deve listar arquivos. Evite `git add .`; adicione somente os caminhos do marco atual.
