# emu2413 import

Source: <https://github.com/digital-sound-antiques/emu2413>

Imported revision: `813cff619f5f01c47bd5c0588c1fd8435530b125`

The following upstream files were imported:

- `emu2413.c` — upstream SHA-256 `F19F3DB120AB958CB337F55DC60DFFBC74BBA02D3FAFB1CE51982B9C3ACA263C`;
  patched-tree SHA-256 `2F0616DC552D39FF78BDFD007AF70567C478CEAD5AC30A704CA42A26469AC3A8`
- `emu2413.h` — SHA-256 `34CEDFC475A2CC237BBDADD02A658C21FC9A0C9CA0AA4B756F568C1FD422D71A`
- `LICENSE` — SHA-256 `F0E33D030727F33CB366A332BDCC7799FA351CEAAED5416405FA6402C08644C9`
- `README.md`

License: MIT, Copyright (c) 2001-2019 Mitsutaka Okazaki.

## Local compatibility patch

`lookup_exp_table` multiplies its signed result by two instead of left-shifting it. The operations
are equivalent for the valid range, while multiplication avoids undefined C behavior when the
sample is negative and therefore works under Zig's checked debug runtime.

The compatibility no-op `OPLL_setQuality` explicitly consumes its two parameters so vendored code
also builds when the parent project enables warnings as errors.
