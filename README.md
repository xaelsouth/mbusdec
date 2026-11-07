# M-Bus decoder

A C11 library and JSON command-line decoder for a subset of EN 13757-3
full-frame application data, with EN 13757-2 wired and EN 13757-4 wireless
link framing and EN 13757-7 transport headers.

This project was written and tested by AI as an experiment to explore what
happens when AI is given the M-Bus EN 13757 specifications, the OMS
specifications, and publicly available test vectors such as those from
[wmbusmeters](https://github.com/wmbusmeters/wmbusmeters). It is not a
complete or certified implementation.

Specifications can be found through the [OMS Group](https://oms-group.org/en/specification/)
and [M-Bus documentation](https://m-bus.com/documentation). The latter describes
an older M-Bus specification and links to the current publication.

For a commercially available alternative, DeviceLab offers
[Payload Extractor](https://devicelab.dk/#payloadextractor), an on-premises
wireless M-Bus and OMS decoder with an HTTP interface and integrated key store.

The project is released under the Unlicense; see [LICENSE](LICENSE). Third-party
bundled dependencies retain their own applicable terms:
[tiny-AES-c](https://github.com/kokke/tiny-AES-c) supplies the default AES
backend, and [mjson](https://github.com/cesanta/mjson) supports CLI JSON
processing. Alternatively, select the external
[OpenSSL](https://www.openssl.org/) AES backend with
`-DMBUS_AES_BACKEND=openssl`.

Some OpenSSL FIPS modules have been validated, but selecting OpenSSL alone
does not make this project FIPS compliant or certified. This implementation
uses low-level OpenSSL AES APIs, which bypass the OpenSSL 3 FIPS provider; see
the [OpenSSL FIPS module guide](https://docs.openssl.org/3.0/man7/fips_module/).
Selecting OpenSSL likewise does not establish compliance with the
[EU Cyber Resilience Act](https://digital-strategy.ec.europa.eu/en/policies/cyber-resilience-act):
applicable requirements must be assessed for the product and its lifecycle.

## Command Line Interface (CLI): rtl-wmbus stream and key

Pipe [rtl-wmbus](https://github.com/xaelsouth/rtl-wmbus) output directly:

```sh
rtl_wmbus | ./build/mbusdec --keys keys.json
```

The rtl-wmbus stream is the default and only supported input format;
`--rtl-wmbus` explicitly selects the same format. The library supports both
wired and wireless frames.

The stream accepts the receiver's eight semicolon-separated fields, including
the `0x`-prefixed CRC-stripped datagram. The optional `rla;` or `t2a;`
algorithm prefix is accepted. A receiver CRC status of `0` produces a CRC
error without decoding the telegram. Only a CRC status of `1` is accepted
for decoding; the receiver's status is trusted because the CRC bytes have
already been removed. Failed 3-out-of-6 checks, invalid status
values, malformed lines, and decode failures emit a JSON error on their own
line and processing continues. Each decoded telegram or error is emitted as
one compact, newline-terminated JSON object, without pretty printing. Responses
are flushed as they are produced;
the process exits nonzero at EOF if any line failed, including CRC failures.
Each response contains `decode_status` with a numeric `code` and a `text`
message. Codes are 0 (ok), 1 (invalid input or telegram), 2 (unsupported),
3 (limit exceeded), and 4 (receiver-reported CRC failure). Errors also retain
the top-level `error` message and string `code` (`invalid`, `unsupported`,
`limit`, or `crc_failed`). The existing top-level `status`, when present, is
the meter's transport status and is separate from `decode_status`.
Error responses include `datagram`, the original final input field with any
leading `0x` or `0X` prefix removed, preserving its original letter case.
Even malformed or CRC-failed fields
are reported without decoding them. If the field cannot be identified safely
(for example, an incomplete, structurally malformed, or oversized input line),
`datagram` is `null`; it is never a silently truncated value. Non-ASCII bytes
in a malformed field are represented as JSON `\u00XX` escapes.

Responses include `receiver.packet_rssi` and `receiver.current_rssi` as
unsigned integers copied from the receiver's PACKET_RSSI and CURRENT_RSSI
fields when both are available and valid, including on failures. These are
rtl-wmbus signal readings, not calibrated dBm values. Both
must be decimal integers between 0 and 4294967295; malformed values cause an
`"invalid"` error. The top-level `timestamp` is copied verbatim from the
receiver when its field can be identified and is nonempty, including on
errors. No timezone is inferred or converted: rtl-wmbus uses local time on
Unix and UTC on Windows, without a timezone suffix. The printed serial is not
included; identities come from the telegram itself. A line is limited to 8191 bytes.
An odd-length datagram with a passing receiver CRC produces an `"invalid"`
error and processing continues. CRC failure takes precedence over datagram
parsing, so failed telegrams are never decoded.

The optional key file is a JSON array, read once at startup. An entry can
specify an application-layer address, an optional link-layer address, and a
nonempty `keys` array:

```json
[
  {"application_layer_address":{"serial_no":55667788,"device_type":"08","version":"55","manufacturer":"QDS"},
   "link_layer_address":{"serial_no":11223344,"device_type":"37","version":"55","manufacturer":"QDS"},
   "keys":[
    {"type":"asynchronous","key":"000102030405060708090A0B0C0D0E0F"},
    {"type":"synchronous","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"}
  ]},
  {"application_layer_address":{"serial_no":"00123456","device_type":"08","version":"55","manufacturer":"TCH"},
   "link_layer_address":"","keys":[
    {"key":"000102030405060708090A0B0C0D0E0F"}
  ]}
]
```

Both identity objects require all four fields. `serial_no` is a decimal integer
from 0 to 99999999, encoded as an eight-digit BCD serial number; an eight-digit
decimal string is also accepted to make leading zeroes explicit. `device_type`
and `version` are exactly two hexadecimal digits each, and `manufacturer` is
the uppercase three-letter M-Bus manufacturer code. These fields together
identify the transport (ALA) or link (LLA) address, not just the serial.
The older `serialNo` and `deviceType` names are accepted for compatibility;
do not specify both names for the same field.
The address names `ala` and `lla` are also accepted as legacy aliases for
`application_layer_address` and `link_layer_address`; do not specify both
names for the same address.
For compatibility, either identity may instead be an eight-byte hex string in
transmission order: manufacturer (two bytes), identification (four bytes),
version and device type.
An omitted or empty `link_layer_address` leaves an ALA entry unconstrained; a
nonempty link-layer address requires the telegram to contain and match both identities. An LLA-only entry
is also supported for devices without a known ALA. An entry must have at least
one nonempty identity. Each key is a 16-byte AES key in hex. When a fully
matching ALA entry exists, only its keys are tried; otherwise matching LLA-only
entries are tried. A mismatched LLA constraint never supplies its keys.
Multiple entries
for an identity are tried in file order. A matched but incorrect ALA key does
not trigger LLA fallback. At most 128 keys in the file, 32768 file bytes and 16 keys
per telegram are supported; exceeding a limit is an error. The optional `type`
is `synchronous` or `asynchronous` (legacy `s` and `a` are also accepted in
the `keys` array); omitting it keeps the key compatible with
either type. For wireless frames with an extended link header, its
synchronization bit takes precedence; otherwise, the bit in a mode 0 or mode 5
transport configuration is used. Matching typed keys are tried first, then
untyped keys, each in file order.
The identity is selected before filtering by type, so an ALA entry of the
other type still prevents LLA fallback. When the type cannot be determined
(including mode 7 without an extended link header, whose configuration bit 13
denotes counter size rather than synchronization; wired frames; and wireless
frames without either header), all matching keys are tried in file order.
Protect the file as a secret; the example keys above are public test values.
The older format with `key` and optional `type` (`synchronous` or
`asynchronous`) directly beside the identity remains supported; do not mix
that format with a `keys` array in the same entry.

Missing or invalid key files fail at startup. The
[example stream](examples/rtl-wmbus.txt) and [test key file](examples/rtl-keys.json)
can be used without receiver hardware.

To decode only selected meters, pass an address filter:

```sh
rtl_wmbus | ./build/mbusdec --filter filter.json --keys keys.json
```

The [example filter](examples/filter.json) is a JSON array with the same
address fields and per-entry matching rules as the key file. Entries need no `keys`:

```json
[
  {
    "application_layer_address": {
      "serial_no": 55667788, "device_type": "08",
      "version": "55", "manufacturer": "QDS"
    },
    "link_layer_address": {
      "serial_no": 11223344, "device_type": "37",
      "version": "55", "manufacturer": "QDS"
    }
  }
]
```

An application-layer address matches the transport identity. An omitted or
empty link-layer address imposes no link constraint; when present, both
addresses must match. A link-only entry matches the link identity. Any
matching entry admits the telegram; `[]` or no `--filter` admits all.
Unmatched telegrams are skipped without output and do not affect exit status.
Filtering happens before decryption and record decoding. Malformed input,
CRC failures, or headers that cannot be inspected still produce errors.
The filter is read at startup and has the same 32768-byte and 128-entry
limits as the key file. Invalid or missing filter files fail at startup.
A key file can also be used as a filter: `key`, `keys`, and `type` values
are ignored (but must be valid JSON) and are not used for decryption. Use
`--keys` separately to validate and use keys; both options may name the same file.

The [examples](examples/) directory contains historical JSON request and
response pairs for a test-only fixture decoder, including wired and wireless telegrams, mode 5
and 7 decryption, compact profiles, and error responses. Files named after a
test script and line number identify the original request; the upstream AES
examples are named for their meters. See the [example source index](examples/README.md)
for provenance and upstream links. Line numbers refer to the tests when the
examples were captured and may shift as tests change. The inputs intentionally include public
test keys, not real secrets. Duplicate JSON keys are retained in the
duplicate-key rejection example. These inputs are not accepted by `mbusdec`;
CTest uses the test-only fixture decoder to preserve coverage.

The response is one JSON object. The top-level `control` is the link-layer
C-field as a two-digit uppercase hexadecimal string without a prefix.
`groups` is an array keyed by `storage`,
`tariff`, `function`, and `subunit`; every matching record appears in the same
group, even when the records are not adjacent in the telegram. Groups include
the subunit, including zero, and records retain their own subunit field.
Function is one of
`instantaneous`, `maximum`, `minimum`, or `error`, and is also retained on
each record. Each record includes its quantity,
unit, function, subunit, raw DIF/VIF, decimal exponent and scaled `value`.
`dif`, `vif`, `vife` and each entry in `vifes` are two-digit uppercase
hexadecimal strings without a prefix.
Integer records also include `raw_value`. A meter-marked invalid value is
represented by `"value":null,"valid":false`; records without data use
`"value":null` without the invalid flag. Register-number encoding is marked
with `storage_is_register` on the record. Grouping uses the numeric storage
field even when it represents a register number.

Every record includes `record_hex`, the complete record in transmission order
from DIF through the data. For variable-length records, it includes the LVAR
length byte. For decrypted telegrams it shows plaintext, not ciphertext.
Unsupported records with safely determinable boundaries have
`"status":"unsupported"` and `"value":null`; they retain the hex fields,
grouping and header information, and decoding continues with the next record.
Manufacturer-specific VIF/VIFE records are likewise retained and skipped when
no decoder handles them, with `manufacturer_specific:true` and
`status:"undecoded"`. Manufacturer-specific CI and trailing DIF blocks are
reported in `manufacturer_data`; their `manufacturer_data_status` is
`"undecoded"` when no matching decoder handles them and `"decoded"` when one
does.
`manufacturer_data_manufacturer` uses the same three-letter code as the
identity, or `null` when the telegram has no manufacturer identity.

`link_identity` and `transport_identity` expose identities separately as
objects or `null` when not present. Wireless frames provide a link identity;
the long transport header (CI `72`) provides a transport identity. A short
header or a headerless payload does not provide a transport identity; it is
not inferred from the link identity. Wired frames have a primary `address`
but no full link identity. Each identity contains `serial_no` as a JSON number
decoded from the four little-endian BCD bytes, `manufacturer` as a three-letter
code, and `version` and `device_type` as two-digit uppercase hex strings.
Leading zeroes in the serial number are not retained. Invalid BCD serials or
manufacturer codes cause an invalid response containing the original `datagram`
and an explanatory error. Status, configuration,
manufacturer-specific trailing bytes and the continuation indication are
returned when present. Scaled numbers use floating-point arithmetic; use
`raw_value` and `exponent` if exact decimal arithmetic is needed.
`primary_identity` selects the transport identity when present, otherwise the
link identity; `primary_identity_layer` is `"application"`, `"link"` or
`null`. Both original identity fields remain available.
When a wireless frame has the two-byte short extended-link header (CI `8C`),
`extended_link` reports its CI, communication control and access number;
the top-level `ci` remains the following application/transport CI. Both CI
fields are two-digit uppercase hexadecimal strings without a prefix.

For security mode 5 the decoder tries matching keys in order, validates the two-byte
decryption check, and decodes the decrypted blocks and any following plaintext.
An incorrect key, malformed encrypted length, or unsupported security mode
returns an error. Successful decryption adds `"decrypted":true` to the response.
Security mode 7 is supported for KDF A, with a four-byte counter in the
transport header, optional key version, and optional transport padding.
`key_id`, `message_counter`, and, when present, `key_version` are reported.
The key list is tried in order; it is not mapped to key IDs or versions.
An AFL counter, AFL authentication/MAC, and fragmentation are not supported;
telegrams requiring them are rejected. Only meter-to-reader full frames are
supported.

**Modes 5 and 7 as implemented are not authenticated:** successful decryption
reports `"authenticated":false`. The two-byte check is not a MAC, so a wrong
key can pass by chance and modified data can go undetected. Do not treat
decoded values as authenticated, and apply replay protection in the consuming
system.

Supported orthogonal VIFEs expose their original bytes in `vifes`, and
semantic qualifiers in `modifiers` (for example `backward_flow`); multiplicative
correction factors are included in `exponent` and `value`. Unsupported
modifiers remain unsupported records. Manufacturer-specific VIFEs whose
boundaries cannot be safely determined cause an error.
Date and minute-resolution date-time records use ISO strings. For the
ambiguous two-digit year encoding, years 00–80 map to 2000–2080 and 81–99 to
1981–1999; a nonzero century in a date-time is honored.

Supported compact-profile records include a `profile` object with its direction,
control byte, interval, and values in transmitted order. Binary profile element
sizes of 1, 2, 3, 4, 6, and 8 bytes are supported, along with the existing
one-byte BCD increment form. Absolute values, positive and negative unsigned
increments, and signed differences are decoded. Interval zero is reported as
an array without a time interval; half-month and monthly intervals are reported
as `half_month` and `month`. `raw_value` is the encoded integer or increment, and `decoded_integer`
and `value` are the reconstructed unscaled and scaled readings. Unsigned profile
values are emitted as unsigned JSON integers. The library marks these with
`mbus_profile_value.is_unsigned` and provides the exact values in
`unsigned_raw_value` and `unsigned_integer`; signed aliases are populated when
they fit. `base_record_index`, when present, is the zero-based index in
transmission order, not an index into a JSON group.
For monthly profiles, `interval` is `1`, `interval_unit` is `"month"`, and
`interval_raw` retains the encoded byte (`254`). Half-month intervals use
`interval: 1`, `interval_unit: "half_month"`, and raw value `253`; interval
zero uses `interval_unit: "none"`. For other supported intervals the raw and
decoded counts are the same. The base time remains a separate record;
individual timestamps are not calculated. Invalid differential elements make
that and all subsequent readings invalid. A missing or ambiguous differential
base makes the profile unsupported.

Malformed input, unsupported frames, and records whose boundaries cannot be
determined produce a JSON error object and a nonzero exit status. Partial
results are not returned in those cases. The key-file parser accepts the
documented fields and rejects duplicate or unknown fields.

## Library

The CMake target `mbus` is the public static decoder library; `mbusdec` links
it through the private CLI support library. To use it from another CMake
project, add this project with `add_subdirectory` and link to `mbus`:

```cmake
add_subdirectory(path/to/mbusdec)
add_executable(my_decoder my_decoder.c)
target_link_libraries(my_decoder PRIVATE mbus)
```

The target propagates its include directory, C11 requirement, math library,
and optional OpenSSL dependency. Alternatively, with the bundled AES backend
on Linux, compile the standalone [library example](examples/library_decode.c)
against the archive directly:

```sh
cc -std=c11 -Iinclude examples/library_decode.c build/libmbus.a -lm -o library_decode
./library_decode
```

For the OpenSSL backend, add `-lcrypto` after the archive; static link
dependencies and archive filenames may differ on Windows.

Include `mbus.h` and call `mbus_decode` with the
telegram bytes, length, format, a caller-owned `mbus_result`, and an optional
error-message pointer. Inspect the result only when the return value is
`MBUS_OK`. Records are returned in transmission order; group them by their
`storage`, `tariff`, `function`, and `subunit` fields as needed. The `function` field
contains the DIF function bits: 0 instantaneous, 1 maximum, 2 minimum, and
3 error. The `unsupported` field marks records
that were skipped without decoding.

For scalar values, `value_type` tags the `raw_value` union: `MBUS_VALUE_SIGNED`
uses `signed_value`, `MBUS_VALUE_UNSIGNED` uses `unsigned_value`,
`MBUS_VALUE_REAL` uses `real_value`, and `MBUS_VALUE_DATETIME` uses `datetime`.
Numeric union members are unscaled; `value` is the scaled `double`. Check
`!unsupported && has_value` before reading the union or scaled value.
`invalid_value` identifies a meter-marked invalid value; no-data and compact
profile records use `MBUS_VALUE_NONE`. An invalid or unsupported record may
still have a type tag, but does not contain a usable scalar value.

`record_offset`/`record_length` and
`value_offset`/`value_length` refer to bytes in the result's owned `telegram`
copy, independent of the lifetime of the input buffer. Error strings and
unit/quantity names are static and must not be freed. Input and result storage
must not overlap.
Use `mbus_decode_with_keys` with caller-owned `mbus_key` entries for AES;
`mbus_decode` supplies no explicit keys and can use an installed key provider.
A decrypted result
stores plaintext in its `telegram` copy at the encrypted payload offsets;
the caller's input buffer remains unchanged.

VIF extensions are available as `record.vifes[0..vife_count-1]` in
transmission order; a zero count means there are none. Use `vife_count > 0`
and `vifes[0]` to access the first extension. The CLI's `vife` and
`vifes` JSON fields contain hexadecimal strings.

API summary:

* `MANUF_CODE("TCH")`: encode exactly three uppercase letters
  into a 15-bit manufacturer code (here `0x5068`). The macro calls
  `mbus_manufacturer_code` and evaluates its argument once. Invalid strings
  and `NULL` return `MBUS_MANUFACTURER_INVALID` (zero); check this before
  using untrusted input. The conversion is a runtime expression, not a C
  constant expression. For example, use it when registering a manufacturer
  decoder or comparing `mbus_identity.manufacturer`.
* `mbus_version`: return the library's CMake project version as a
  NUL-terminated string, for example `"0.1.0"`. The string has static
  lifetime, is read-only, and must not be freed.
* `mbus_decode`: decode using the installed key provider, if any.
* `mbus_decode_with_keys`: decode with up to `MBUS_MAX_KEYS` AES-128 keys,
  tried in order. A zero count permits a null key pointer. The two-byte
  verification is not authentication.
* `mbus_wireless_remove_crc`: validate a raw Format A or B wireless link
  frame and remove its CRCs without decoding its payload.
* `mbus_set_key_provider`: install, replace, or remove (with `NULL`) a
  process-global key retrieval callback and its caller-owned context.
  Decoding calls it once after successful header parsing when the explicit
  key count is zero, including plaintext and manufacturer-specific telegrams.
  It receives nullable application and link identities, a synchronization
  state, a key buffer of `MBUS_MAX_KEYS` capacity, and returns a key count
  and status. Return zero keys with `MBUS_OK` for no match; errors abort the
  decode. Provider keys are tried in returned order and cleared afterwards.
  Explicit keys take precedence: a failed explicit key does not invoke the
  provider. The CLI's key-file selection similarly passes only matching keys;
  its standalone executable does not install a provider. Embedding programs
  can register one in their own process.
  Synchronization follows the same ELL/transport precedence as key-file
  selection; `MBUS_SYNC_UNKNOWN` means no applicable bit was available.
  Registration and decoding with a provider are single-threaded; do not
  register, remove, or recursively decode from within the callback. Keep
  the callback context and any error strings alive as documented in the header.
  See [examples/key_provider.c](examples/key_provider.c) for a complete
  callback and registration example using a public test key.
* `mbus_register_manufacturer_decoder`: install or replace a process-global
  callback for one 16-bit manufacturer code. Up to
  `MBUS_MAX_MANUFACTURER_DECODERS` (32) distinct manufacturers may be
  registered. `mbus_unregister_manufacturer_decoder` removes one. This
  registry is single-threaded; do not register or unregister from inside a
  callback.
* `mbus_get_header_info`: inspect link and transport identities, extended-link
  control and transport configuration before decoding or selecting a key.
  `mbus_get_identities` is a convenience function for identities alone.
  Header inspection validates framing and supported headers, but does not
  validate or decrypt the payload.

### Key provider example

[examples/key_provider.c](examples/key_provider.c) installs a callback with
`mbus_set_key_provider`, matches the telegram's link identity and
synchronization state, returns a public test key, decodes a telegram, and
removes the callback. Its context remains alive until the provider is removed.
Replace the test lookup with your own key store or service; never hard-code
production keys.

After building the bundled AES backend, compile and run it from the project
directory:

```sh
cc -std=c11 -Iinclude examples/key_provider.c build/libmbus.a -lm -o key_provider
./key_provider
```

The output is `Decrypted telegram; profile values: 3`. For an OpenSSL build,
add `-lcrypto` after the archive, as in the library example above.

### Raw wireless Format A and B

For receivers that provide link CRC bytes, pass `MBUS_WIRELESS_FORMAT_A` or
`MBUS_WIRELESS_FORMAT_B` to `mbus_decode`, `mbus_decode_with_keys`,
`mbus_get_header_info`, or `mbus_get_identities`. Input starts at the L byte
and includes all link CRCs, but not a radio preamble, sync word or trailing
bytes. The format must be known by the caller; it is not autodetected.
Every link CRC and the exact frame length are validated before header parsing
or calling a key provider. A failure returns `MBUS_INVALID` with a diagnostic.

Alternatively, call `mbus_wireless_remove_crc` to validate and convert a frame
without decoding it:

```c
uint8_t normalized[256];
size_t normalized_length;
const char *error = NULL;
mbus_status status = mbus_wireless_remove_crc(
    raw_frame, raw_length, MBUS_WIRELESS_FORMAT_A,
    normalized, sizeof(normalized), &normalized_length, &error);
if (status == MBUS_OK) {
    status = mbus_decode(normalized, normalized_length, MBUS_WIRELESS,
                         &result, &error);
}
```

In this fragment `raw_frame`, `raw_length`, and `result` are supplied by the
caller. Check `status` and `error` after either operation. The converter
supports overlapping input and output buffers; its output remains unchanged
on failure and the returned length is zero. It validates link framing, not
the application payload. Format A has a CRC after its 10-byte first block and
after each subsequent block of up to 16 bytes. Format B has one CRC for up to
126 data bytes and, for longer frames, a second CRC for the remaining data.
Format B's L field includes CRC bytes; the converter adjusts it to exclude
them. The maximum raw input is `MBUS_MAX_RAW_WIRELESS` (290) bytes; normalized
wireless output is at most 256 bytes. For raw decoding, `result.format` is
`MBUS_WIRELESS`, and the result's telegram and offsets refer to normalized,
CRC-stripped bytes. CRCs detect transmission errors but do not authenticate
the sender or payload. This library API does not change rtl-wmbus CLI input,
which remains CRC-stripped.
The CRC lookup table is generated from the CRC-16/EN-13757 polynomial
`0x3D65`; its calculation uses an initial value of zero and a final XOR
of `0xFFFF`.

All functions return `MBUS_OK`, `MBUS_INVALID`, `MBUS_UNSUPPORTED`, or
`MBUS_LIMIT`. Their optional `const char **error` is cleared on entry and
receives a static diagnostic on failure. All other output pointers are
required; outputs must not be used on failure. `mbus_result` is sizeable:
callers with limited stack space can use static or caller-managed storage.
Each successful decode replaces the previous contents. Concurrent calls need
separate caller-owned inputs and outputs.

The result has separate `link_identity` and `transport_identity` structures,
with `has_link_identity` and `has_transport_identity` presence flags.

Manufacturer callbacks are selected by the telegram's transport manufacturer
when present, otherwise its link manufacturer. They receive the block kind,
manufacturer code, raw block bytes and length, and the block's offset in the
result's owned `telegram` buffer. For VIF/VIFE blocks the callback also gets
the record being decoded and must populate it as a supported record before
returning `MBUS_OK`; for CI/DIF blocks the record pointer is `NULL`, and the
callback may append decoded records to the result. Return `MBUS_UNSUPPORTED`
to decline a block, which is not a telegram error: the block remains
`undecoded` and parsing continues where its boundary is known. Other failure
statuses fail the decode. The callback context remains caller-owned until the
registration is replaced or removed. Callbacks must not modify the registry
while running.

See [manufacturer_decode.c](examples/manufacturer_decode.c) for a complete,
buildable example. It registers a handler, decodes a made-up one-byte VIF
value and a CI payload, leaves an unknown manufacturer record undecoded while
processing the following standard record, and unregisters the handler. Its
formats and manufacturer code are illustrative, not specifications for a
real meter. With the bundled AES backend on Linux, run it with:

```sh
cc -std=c11 -Iinclude examples/manufacturer_decode.c build/libmbus.a -lm -o manufacturer_decode
./manufacturer_decode
```

For VIF/VIFE callbacks, `data` and `length` describe the record's value bytes;
inspect `record->vif`, `record->vifes`, and `record->dif` to identify a format.
For CI/DIF callbacks, they describe the manufacturer payload after the CI or
DIF. `offset` identifies the same bytes in `result->telegram`. Set
`quantity`, `unit`, `value_type`, `raw_value`, and `value` appropriately when
handling a numeric VIF/VIFE record, clear `unsupported`, and do not change its
framing fields. CI/DIF callbacks may append records, respecting
`MBUS_MAX_RECORDS` and using offsets into the owned telegram. A handler that
does not recognize a block should return `MBUS_UNSUPPORTED` without changing
the record or result.

The implementation separates frame and transport parsing (`src/mbus.c`), record
decoding (`src/mbus_records.c`), compact profiles (`src/mbus_profile.c`), and
AES/security handling (`src/mbus_security.c`). Private headers are in
`include/private`; applications only need the public `include/mbus.h`.

Neither the library nor the CLI explicitly allocates memory dynamically. The
caller supplies the result storage; the CLI uses static buffers. The C runtime
and standard I/O implementation may allocate internally. Both AES backends
use fixed-size contexts per decode; the optional OpenSSL backend uses its
deprecated low-level AES API to avoid EVP context allocation. The bundled
table-based AES implementation is not designed to resist timing/cache
side-channel attacks; validate the backend and deployment if that matters. Limits are
`MBUS_MAX_TELEGRAM` (261 bytes), `MBUS_MAX_RECORDS` (64), and 8191 bytes per
rtl-wmbus input line, plus `MBUS_MAX_PROFILE_VALUES` (256) across a decoded telegram;
exceeding a limit fails rather than truncating.

## Build and test

Run `cmake -S . -B build` and `cmake --build build` to build `mbusdec` and
`libmbus.a`. A C11 compiler and the math library are required. AES uses the
bundled [tiny-AES-c](https://github.com/kokke/tiny-AES-c) implementation by
default; it requires no OpenSSL installation. To use OpenSSL instead, configure
with `-DMBUS_AES_BACKEND=openssl` and install the OpenSSL development library.
The CLI's private `mbus_cli` static library bundles
[mjson](https://github.com/cesanta/mjson) for JSON parsing and string emission;
the public decoder library itself does not depend on mjson.
Run
`ctest --test-dir build --output-on-failure` for library and CLI tests;
the CLI tests require a POSIX shell and `jq`. Configure with
`-DBUILD_TESTING=OFF` to build without the test dependencies.
On Windows, CMake and a C11 compiler such as MSVC or MinGW can build the
bundled backend. The library tests are portable; shell-based JSON tests are
registered only when both `sh` and `jq` are found.

## Binary package

After building, create a platform-specific `.tar.gz` archive with:

```sh
cmake --build build --target package
```

For multi-configuration generators, build and package with the same
configuration, for example `cmake --build build --config Release --target package`.
The archive contains the compiled `mbusdec` executable, the static decoder
library, `include/mbus.h`, and license files. It contains binaries for the
build target, not all platforms. System runtime libraries are not bundled;
the OpenSSL backend also requires a compatible OpenSSL runtime for the CLI
and OpenSSL development libraries when linking the static decoder library.
Alternatively, install these files with `cmake --install build --prefix <directory>`.

## Supported scope and limitations

* Wired long frames (`68 ... 16`), including length and checksum validation.
  Short, control and acknowledgement frames are not decoded.
* Wireless frames starting with the L field, either CRC-stripped or raw
  Format A/B including link CRCs via the library API. Link CRCs cannot be
  verified when already stripped. Radio preambles, fragmentation, and
  authentication are not supported. The unencrypted
  short extended-link header (CI `8C`) is supported; other extended-link
  variants, including ELL encryption, are not.
* Full-frame responses with CI `78`, `7A`, or `72`, plus manufacturer-specific
  CI values `A0`–`BF` which are retained as raw manufacturer data and offered
  to the registered callback; security modes 0, 5 and 7.
  Mode 7 support is restricted to the transport-counter form described above.
  Compact *frames* (such as CI `79`), other CI values, and other security modes
  are rejected.
* Primary numeric energy, volume, mass, power, flow, temperature, pressure,
  duration, heat-cost-allocation and fabrication-number VIFs; customer-location,
  dimensionless and error-flag extended VIFs. Fixed-width signed integers, BCD, IEEE binary32 real values,
  minute-resolution date/time, empty/readout selections, filler bytes, DIFE grouping, and manufacturer
  trailing data are supported. Invalid numeric sentinel values are flagged.
* Compact-profile **records** with a variable-length DIF and VIFE `13`, `1E`
  or `1F` are supported for absolute signed binary values and unsigned or
  signed differential binary values of 1, 2, 3, 4, 6 or 8 bytes, plus positive
  one-byte BCD increments.
  Positive and negative increments and signed differences are supported;
  inverse profiles reverse the differential direction. Intervals 0–250
  (excluding reserved values), plus the `FD` half-month and `FE` monthly
  encodings with the days unit, are supported. Base values must be uniquely
  identifiable and use the same storage, tariff, subunit, function, register
  flag, VIF, supported modifiers and exponent.
* Other date/time variants, variable-length values, other extended VIFs,
  unsupported VIF modifiers and custom units are not yet decoded, but appear as
  unsupported records when their boundaries are known. Manufacturer-specific
  VIF/VIFE records are offered to registered callbacks and otherwise marked
  undecoded. Unknown special DIFs, reserved LVAR lengths and profile/compact
  frames cannot be safely skipped and cause an explicit unsupported error.
  Unsupported compact profile variants with known boundaries are returned as
  unsupported records.

This is an initial implementation, not a complete or certified implementation
of the standards.

The real-telegram tests use example telegrams and selected readings from
[wmbusmeters](https://github.com/wmbusmeters/wmbusmeters), revision
`a523d014a17ca24aeb8fa26c285468175eb89fdd`: its
[wireless test](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/tests/test_hex_binary_values.sh)
and [wired simulation](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/simulation_mbus.txt),
plus a wireless heat cost allocator and sensor from
[simulation_c1.txt](https://github.com/wmbusmeters/wmbusmeters/blob/a523d014a17ca24aeb8fa26c285468175eb89fdd/simulations/simulation_c1.txt).
The tests also use four wired telegrams and selected expected readings from
[rSCADA libmbus](https://github.com/rscada/libmbus/tree/057ab6314d4067b0f75f3f6f067d741262d2d3de/test/test-frames),
revision `057ab6314d4067b0f75f3f6f067d741262d2d3de`; see the
[fixture provenance and license](examples/README.md#libmbus-test-vectors).
Expected fields are expressed in this decoder's JSON format and units.
The [upstream AES tests](tests/test_upstream_aes.sh) also check two encrypted
mode 5 telegrams against wmbusmeters' expected current and historical
readings, as well as wrong/missing keys. The upstream encrypted Kamstrup
example uses extended-link-layer AES-CTR and is explicitly tested as
unsupported; the encrypted Apator example has a proprietary payload and is
also unsupported.

Library tests include raw Format A/B CRC validation and conversion, wireless
telegrams with their link CRCs removed, compact-profile records, and unsupported
AFL authentication. See [examples/README.md](examples/README.md) for raw
vector provenance and distinctions between published and generated CRCs. A modified
telegram with its counter moved to TPL and its MAC removed tests the mode 7
KDF and decryption, not authentication.

The bundled mjson source is pinned to revision
`696969cd0d35399cc66075f5ec7a96e23ba4a89b` and is MIT-licensed; see
`thirdparty/mjson/LICENSE`. The CLI uses its callback parser and string
emitter without the allocating APIs. Key-file schema validation remains
strict, including duplicate fields, trailing commas and trailing data.

Bundled AES source is from tiny-AES-c revision
`23856752fbd139da0b8ca6e471a13d5bcc99a08d`; its public-domain dedication is in
`thirdparty/tiny-aes-c/unlicense.txt`. AES-128-CBC and AES-128 block encryption
for CMAC key derivation are enabled.
