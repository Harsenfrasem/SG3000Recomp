# SingleStepTests Z80 fixture

`tests/z80_external_vectors.inc` was generated from
[SingleStepTests/z80](https://github.com/SingleStepTests/z80), revision
`ebe1875d48f374bcfd4b505d8eb8ee751568b5f7`, with:

```text
python tools/import_z80_conformance.py <upstream>/v1 tests/z80_external_vectors.inc \
  --revision ebe1875d48f374bcfd4b505d8eb8ee751568b5f7
```

The compact fixture selects the first vector without external port transactions from each
opcode-sequence file: 1,574 cases. Thirty sequences whose every vector performs I/O are
excluded because this runner intentionally validates CPU registers, flat RAM, flags and total
cycles without mocking peripheral port values. The normal local tests cover I/O instruction
semantics separately.

The upstream data is MIT licensed; see `SINGLE_STEP_TESTS_LICENSE.txt`.
