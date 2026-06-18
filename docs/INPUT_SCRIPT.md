# Deterministic Headless Input

Use `--input-script` with `--run-host` to apply controller state at exact frame boundaries. This is intended for repeatable title-screen progression, gameplay smoke tests, and compatibility regressions.

```powershell
.\build\zig-debug\sgrecomp.exe game.sms --run-host --frames 300 --input-script local-saves\title-input.csv
```

The file is CSV with four columns:

```csv
frame,player1,player2,pause
0,none,none,off
60,button1,none,off
61,none,none,off
120,right+button1,none,on
121,right,none,off
```

Each row replaces the complete input state at the beginning of that frame. The state remains active until another row replaces it. Frame numbers must be strictly increasing.

Controller values may combine `up`, `down`, `left`, `right`, `button1`/`b1`, and `button2`/`b2` with `+`. Use `none` or `-` for no buttons. Pause accepts `on`/`off`, `true`/`false`, `pressed`/`released`, or `1`/`0`.

Keep scripts tied to private ROMs under an ignored local directory. Do not commit ROM-specific filenames, hashes, routes, or input sequences without confirming redistribution is appropriate.
