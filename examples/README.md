# Example telegram sources

The JSON `*.input.json` and `*.output.json` files are request/response pairs
for the test-only fixture decoder. The names below refer to both files in each
pair. Public test keys are included only to exercise decryption; they are not
keys for production devices. Telegrams derived from rtl-wmbus sample
captures are not given source entries in this index.

## wmbusmeters examples

The upstream links are pinned to revision
`a523d014a17ca24aeb8fa26c285468175eb89fdd`. Separators in upstream telegrams
were removed for the test inputs.

| Example pair | Source |
|---|---|
| `test_real_telegrams-9` | [Wireless binary-values test](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/tests/test_hex_binary_values.sh) |
| `test_real_telegrams-21` | [Wired simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/simulation_mbus.txt) |
| `test_real_telegrams-39`, `test_real_telegrams-54` | [Wireless C1 simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/simulation_c1.txt) |
| `test_real_telegrams-65` | [Apator telegram in serial simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/serial_aes.msg); the key is supplied by the [local test](../tests/test_real_telegrams.sh) |
| `test_upstream_aes-56` | [Kamstrup telegram in serial simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/serial_aes.msg) |
| `upstream-waterstarm`, `upstream-waterstarm-wrong-key` | [Wireless T1 simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/simulation_t1.txt) and [meter configuration](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/tests/test_t1_meters.sh); the wrong-key case is a local variation |
| `upstream-aventies`, `upstream-aventies-no-key` | [Wireless T1 simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/simulation_t1.txt) and [meter configuration](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/tests/test_t1_meters.sh); the no-key case is a local variation |

## Other examples

| Example | Source |
|---|---|
| `test_cli-40`, [`rtl-wmbus.txt`](rtl-wmbus.txt) | An OMS example, with link CRCs removed. The stream metadata is illustrative. See the [OMS Group](https://oms-group.org/en/specification/). |
| `test_cli-65`, `test_cli-78` | Compact-profile records based on EN 13757-3 examples, wrapped in test wireless frames. See [M-Bus documentation](https://m-bus.com/documentation). |
| [`library_decode.c`](library_decode.c), `test_cli-6` | Local illustrative telegram used by the library and CLI tests; no external source is recorded. |
| [`manufacturer_decode.c`](manufacturer_decode.c) | Local illustrative manufacturer-specific formats and telegrams; they do not describe a real manufacturer's protocol. |
| `test_cli-104`, `test_cli-105`, `test_cli-106`, `test_cli-121` | Local invalid-input or unsupported-format test cases; no external telegram source is recorded. |
| `test_cli-17`, `test_cli-32`, `test_cli-56`, `test_cli-86` | Local decoder regression inputs; no external telegram source is recorded. |
| `test_cli-111`, `test_cli-118`, `test_cli-119`, `test_cli-120`, `test_cli-122` | Mode 5 test vectors and error variations from [test_cli.sh](../tests/test_cli.sh); no external telegram source is recorded. |
| `test_cli-130`, `test_cli-138`, `test_cli-144`, `test_cli-145`, `test_cli-146`, `test_cli-147`, `test_cli-148`, `test_cli-149` | Mode 7 and error test vectors from [test_cli.sh](../tests/test_cli.sh). The mode 7 KDF-A vectors were generated independently with OpenSSL; other inputs test rejection paths. |

The historical line numbers in fixture filenames identify where examples were
captured, not necessarily their current location in the test scripts.

## libmbus test vectors

The wired telegrams in [tests/fixtures/libmbus](../tests/fixtures/libmbus)
come from [rSCADA libmbus test-frames](https://github.com/rscada/libmbus/tree/057ab6314d4067b0f75f3f6f067d741262d2d3de/test/test-frames)
at revision `057ab6314d4067b0f75f3f6f067d741262d2d3de`. The associated
upstream `.norm.xml` files (and `filler.xml`) supply the reference readings
used by [test_libmbus.sh](../tests/test_libmbus.sh). Copyright belongs to
Raditex Control AB and contributors; see the fixture [license](../tests/fixtures/libmbus/LICENSE).
