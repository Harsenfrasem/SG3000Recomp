# SingleStepTests Z80 fixture

`tests/z80_external_vectors.inc` was generated from
[SingleStepTests/z80](https://github.com/SingleStepTests/z80), revision
`ebe1875d48f374bcfd4b505d8eb8ee751568b5f7`, with:

```text
python tools/import_z80_conformance.py <upstream>/v1 tests/z80_external_vectors.inc \
  --revision ebe1875d48f374bcfd4b505d8eb8ee751568b5f7
```

The compact fixture selects the first vector from every opcode-sequence file: 1,604 cases.
The conformance runner provides deterministic flat RAM and peripheral-port input, records port
output, and validates CPU registers, flags, RAM, port writes and total cycles. This includes all
30 instruction sequences whose vectors perform I/O.

The upstream data is MIT licensed; see `SINGLE_STEP_TESTS_LICENSE.txt`.
