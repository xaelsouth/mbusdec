#!/bin/sh
set -eu
decoder=$1
jq=$2
tmp=$(mktemp -d)
trap 'rm -f "$tmp/keys.json" "$tmp/input" "$tmp/output" "$tmp/error" "$tmp/explicit"; rmdir "$tmp"' EXIT HUP INT TERM

frame=304493444433221155378C00757288776655934455080004100500DFE2A782146D1513581CD2F83F39040CFD1078563412
sync_frame=304493444433221155378C20757288776655934455080004100500DFE2A782146D1513581CD2F83F39040CFD1078563412
# Default input is the same streaming format as the explicit selector.
printf 'T1;1;1;time;1;1;11223344;0x16442D2C123456780107780202D20402152E16022A3423\n' > "$tmp/input"
"$decoder" < "$tmp/input" > "$tmp/output"
"$decoder" --rtl-wmbus < "$tmp/input" > "$tmp/explicit"
cmp "$tmp/output" "$tmp/explicit"
"$jq" -e '.format == "wireless" and .decode_status == {"code":0,"text":"ok"} and
    (has("datagram") | not) and
    .primary_identity == {"serial_no":78563412,"manufacturer":"KAM","version":"01","device_type":"07"} and
    .ci == "78" and
    ([.. | objects | select(has("value_hex"))] | length == 0) and
    .groups[0].records[0].raw_value == 1234' "$tmp/output" >/dev/null
if printf 'T1;1;1;time;1;1;11223344;0x0D442D2C5A341200010778010301\n' |
    "$decoder" > "$tmp/output"; then
    echo "Expected invalid BCD serial number to fail" >&2
    exit 1
fi
"$jq" -e '.code == "invalid" and .error == "invalid link serial number BCD" and
    .datagram == "0D442D2C5A341200010778010301"' "$tmp/output" >/dev/null
if printf 'T1;1;1;time;1;1;11223344;0X0d442d2c5a341200010778010301\n' |
    "$decoder" > "$tmp/output"; then
    echo "Expected invalid BCD serial number to fail" >&2
    exit 1
fi
"$jq" -e '.code == "invalid" and .datagram == "0d442d2c5a341200010778010301"' "$tmp/output" >/dev/null
printf 'T1;1;1;time;1;1;11223344;0x22442D2C12345678010778010301814003020103038180400304814003058180400306\n' > "$tmp/input"
"$decoder" < "$tmp/input" > "$tmp/output"
"$jq" -e '.decode_status.code == 0 and ([.groups[].subunit] == [0, 1, 2]) and
    ([.groups[0].records[].raw_value] == [1, 3]) and
    ([.groups[1].records[].raw_value] == [2, 5]) and
    ([.groups[2].records[].raw_value] == [4, 6])' "$tmp/output" >/dev/null
# RSSI is reported per telegram, including algorithm-prefixed lines and EOF.
printf 'T1;1;1;time;0;4294967295;11223344;0x16442D2C123456780107780202D20402152E16022A3423\nrla;C1;1;1;time;75;42;11223344;0x16442D2C123456780107780202D20402152E16022A3423' > "$tmp/input"
"$decoder" < "$tmp/input" > "$tmp/output"
"$jq" -Rse 'split("\n") | length == 3 and .[2] == "" and
    (.[0] | fromjson | .receiver == {"packet_rssi":0,"current_rssi":4294967295}) and
    (.[1] | fromjson | .receiver == {"packet_rssi":75,"current_rssi":42})' "$tmp/output" >/dev/null
printf 'T1;1;1;time;-1;1;11223344;0x16442D2C123456780107780202D20402152E16022A3423\nT1;1;1;time;1;4294967296;11223344;0x16442D2C123456780107780202D20402152E16022A3423\nT1;1;1;time;abc;1;11223344;0x16442D2C123456780107780202D20402152E16022A3423\nT1;1;1;time;1;1;11223344;0x16442D2C123456780107780202D20402152E16022A3423\n' > "$tmp/input"
if "$decoder" < "$tmp/input" > "$tmp/output"; then
    echo "Expected invalid RSSI values to fail" >&2
    exit 1
fi
"$jq" -se 'length == 4 and all(.[0:3][]; .code == "invalid" and
    .datagram == "16442D2C123456780107780202D20402152E16022A3423") and
    .[3].receiver == {"packet_rssi":1,"current_rssi":1}' "$tmp/output" >/dev/null
if printf 'T1;0;1;time;not-a-number;also-invalid;11223344;0x1\n' |
    "$decoder" > "$tmp/output"; then
    echo "Expected CRC failure" >&2
    exit 1
fi
"$jq" -e '.decode_status == {"code":4,"text":"receiver reported CRC failure"} and
    .code == "crc_failed" and .datagram == "1" and (has("receiver") | not)' "$tmp/output" >/dev/null
printf '{"format":"wireless","telegram":"16442D2C123456780107780202D20402152E16022A3423"}\nT1;1;1;time;1;1;11223344;0x16442D2C123456780107780202D20402152E16022A3423\n' > "$tmp/input"
if "$decoder" < "$tmp/input" > "$tmp/output"; then
    echo "Expected former JSON input to be rejected" >&2
    exit 1
fi
"$jq" -se 'length == 2 and .[0].code == "invalid" and .[0].datagram == null and .[1].format == "wireless"' "$tmp/output" >/dev/null
if "$decoder" --rtl-wmbus --rtl-wmbus < /dev/null > "$tmp/output" 2> "$tmp/error"; then
    echo "Expected duplicate selector to fail" >&2
    exit 1
fi
test ! -s "$tmp/output"
test -s "$tmp/error"
printf 'T1;1;1;time;1;1;11223344;0x123\nT1;0;1;time;1;1;11223344;0x1\nT1;1;1;time;1;1;11223344;0x16442D2C123456780107780202D20402152E16022A3423\nT1;1;1;time;1;1;11223344;0x1' > "$tmp/input"
if "$decoder" --rtl-wmbus < "$tmp/input" > "$tmp/output"; then
    echo "Expected odd-length telegrams to fail" >&2
    exit 1
fi
"$jq" -Rse 'split("\n") | length == 5 and .[4] == "" and
    (.[0] | fromjson | .code == "invalid" and .decode_status.code == 1 and .datagram == "123" and (.error | contains("even number"))) and
    (.[1] | fromjson | .code == "crc_failed" and .decode_status.code == 4 and .datagram == "1") and
    (.[2] | fromjson | .format == "wireless" and .decode_status == {"code":0,"text":"ok"}) and
    (.[3] | fromjson | .code == "invalid" and .decode_status.code == 1 and .datagram == "1" and (.error | contains("even number")))' "$tmp/output" >/dev/null
# Malformed fields are escaped safely and long lines never expose partial datagrams.
if printf 'T1;1;1;time;1;1;11223344;0x"\\\001\377\n' |
    "$decoder" > "$tmp/output"; then
    echo "Expected malformed datagram to fail" >&2
    exit 1
fi
"$jq" -e '.code == "invalid" and .datagram == "\"\\\u0001\u00ff"' "$tmp/output" >/dev/null
"$jq" -nr '"T1;1;1;time;1;1;11223344;0x" + ("0" * 8192)' > "$tmp/input"
if "$decoder" < "$tmp/input" > "$tmp/output"; then
    echo "Expected oversized line to fail" >&2
    exit 1
fi
"$jq" -e '.code == "invalid" and .datagram == null' "$tmp/output" >/dev/null
# Object identities are independent of property order and match both layers.
cat > "$tmp/keys.json" <<'EOF'
[{"keys":[{"type":"asynchronous","key":"000102030405060708090A0B0C0D0E0F"}],
  "link_layer_address":{"version":"55","manufacturer":"QDS","serial_no":11223344,"device_type":"37"},
  "application_layer_address":{"device_type":"08","serial_no":"55667788","manufacturer":"QDS","version":"55"}}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":{"serialNo":55667788,"deviceType":"08","version":"55","manufacturer":"QDS"},
  "lla":{"serialNo":11223344,"deviceType":"37","version":"55","manufacturer":"QDS"},
  "keys":[{"type":"a","key":"000102030405060708090A0B0C0D0E0F"}]}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":{"serial_no":55667788,"device_type":"08","version":"55","manufacturer":"QDS"},
  "lla":{"serial_no":11223345,"device_type":"37","version":"55","manufacturer":"QDS"},
  "keys":[{"type":"a","key":"000102030405060708090A0B0C0D0E0F"}]}]
EOF
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected mismatched structured LLA to be excluded" >&2
    exit 1
fi
"$jq" -e --arg datagram "$frame" '.code == "unsupported" and .decode_status.code == 2 and .datagram == $datagram and
    .decode_status.text == .error and (.error | contains("requires an AES key")) and
    .receiver == {"packet_rssi":1,"current_rssi":1}' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":{"serial_no":55667789,"device_type":"08","version":"55","manufacturer":"QDS"},
  "lla":{"serial_no":11223344,"device_type":"37","version":"55","manufacturer":"QDS"},
  "keys":[{"type":"a","key":"000102030405060708090A0B0C0D0E0F"}]}]
EOF
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected mismatched structured ALA to be excluded" >&2
    exit 1
fi
"$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"link_layer_address":{"serial_no":33225544,"device_type":"07","version":"99","manufacturer":"SEN"},
  "keys":[{"type":"asynchronous","key":"000102030405060708090A0B0C0D0E0F"}]}]
EOF
printf 'T1;1;1;time;1;1;33225544;0x2144AE4C4455223399077A55001005DF31C4C903C475049A49454F13B76D77010307\n' |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true and .primary_identity_layer == "link"' >/dev/null
leading_zero_frame=304493444433221155378C00757256341200934455080004100500DFE2A782146D1513581CD2F83F39040CFD1078563412
for serial in '123456' '"00123456"'; do
    printf '[{"ala":{"serial_no":%s,"device_type":"08","version":"55","manufacturer":"QDS"},"keys":[{"key":"000102030405060708090A0B0C0D0E0F"}]}]' "$serial" > "$tmp/keys.json"
    if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$leading_zero_frame" |
        "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
        echo "Expected altered identity to fail AES verification" >&2
        exit 1
    fi
    "$jq" -e '.code == "invalid" and (.error | contains("truncated record value"))' "$tmp/output" >/dev/null
done
for field in application_layer_address link_layer_address; do
    for identity in \
        '{}' \
        '{"serial_no":55667788,"device_type":"08","version":"55"}' \
        '{"serial_no":-1,"device_type":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":100000000,"device_type":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":1.0,"device_type":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":"123","device_type":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":"12A45678","device_type":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":55667788,"device_type":"0G","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":55667788,"device_type":"08","version":"555","manufacturer":"QDS"}' \
        '{"serial_no":55667788,"device_type":"08","version":"55","manufacturer":"qds"}' \
        '{"serial_no":55667788,"device_type":"08","version":"55","manufacturer":"QDS","serial_no":1}' \
        '{"serial_no":55667788,"serialNo":55667788,"device_type":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":55667788,"device_type":"08","deviceType":"08","version":"55","manufacturer":"QDS"}' \
        '{"serial_no":55667788,"device_type":"08","version":"55","manufacturer":"QDS",}'; do
        printf '[{"%s":%s,"keys":[{"key":"000102030405060708090A0B0C0D0E0F"}]}]' "$field" "$identity" > "$tmp/keys.json"
        if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
            echo "Expected invalid structured $field to fail: $identity" >&2
            exit 1
        fi
    done
done

for addresses in \
    '"application_layer_address":"9344887766555508","ala":"9344887766555508"' \
    '"link_layer_address":"9344443322115537","lla":"9344443322115537"'; do
    printf '[{%s,"keys":[{"key":"000102030405060708090A0B0C0D0E0F"}]}]' "$addresses" > "$tmp/keys.json"
    if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
        echo "Expected duplicate address aliases to fail" >&2
        exit 1
    fi
done

# Group keys under one identity, allowing the identity field after the array.
cat > "$tmp/keys.json" <<'EOF'
[
  {"keys":[
    {"type":"asynchronous","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"},
    {"key":"000102030405060708090A0B0C0D0E0F","type":"asynchronous"},
    {"type":"synchronous","key":"000102030405060708090A0B0C0D0E0F"}
  ],"link_layer_address":"9344443322115537","application_layer_address":"9344887766555508"}
]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\nT1;1;1;time;1;1;11223344;0x%s\n' "$frame" "$sync_frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"
"$jq" -se 'length == 2 and all(.[]; .decrypted == true)' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","lla":"","keys":[
  {"type":"a","key":"000102030405060708090A0B0C0D0E0F"}
]}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
for addresses in \
    '"ala":"9344887766555508","lla":"9344443322115538"' \
    '"ala":"9344887766555509","lla":"9344443322115537"'; do
    printf '[{%s,"keys":[{"type":"a","key":"000102030405060708090A0B0C0D0E0F"}]}]' "$addresses" > "$tmp/keys.json"
    if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
        "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
        echo "Expected mismatched identity pair to be excluded" >&2
        exit 1
    fi
    "$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null
done
# A mismatched constrained ALA entry is not eligible; independent LLA-only
# entries may still match, without using keys from the constrained entry.
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","lla":"9344443322115538","keys":[
   {"key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"}]},
 {"lla":"9344443322115537","keys":[
   {"key":"000102030405060708090A0B0C0D0E0F"}]}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","keys":[
  {"type":"s","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"},
  {"key":"000102030405060708090A0B0C0D0E0F"}
]}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","keys":[
  {"type":"s","key":"000102030405060708090A0B0C0D0E0F"}
]}]
EOF
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected opposite grouped key type to be excluded" >&2
    exit 1
fi
"$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null

cat > "$tmp/keys.json" <<'EOF'
[
  {"lla":"9344443322115537","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"},
  {"ala":"9344887766555508","key":"000102030405060708090A0B0C0D0E0F"}
]
EOF
printf 'T1;1;1;2023-12-01T10:00:00;100;90;11223344;0x%s\n' "$frame" > "$tmp/input"
printf 'C1;0;1;2023-12-01T10:00:00;100;90;11223344;0x%s\n' "$frame" >> "$tmp/input"
printf 't2a;C1;1;1;2023-12-01T10:00:00;100;90;11223344;0x%s' "$frame" >> "$tmp/input"
if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < "$tmp/input" > "$tmp/output" 2> "$tmp/error"; then
    echo "Expected CRC failure to affect exit status" >&2
    exit 1
fi
test ! -s "$tmp/error"
"$jq" -Rse '
  split("\n") | length == 4 and .[3] == "" and
  (.[0] | fromjson | .decrypted == true) and
  (.[1] | fromjson | .decode_status.code == 4) and
  (.[2] | fromjson | .decrypted == true)
' "$tmp/output" >/dev/null
"$jq" -se '
  length == 3 and
  (.[0] | .decrypted == true and .decode_status == {"code":0,"text":"ok"} and .primary_identity_layer == "application" and
    .primary_identity.serial_no == 55667788 and
    .link_identity.serial_no == 11223344 and
    .receiver == {"packet_rssi":100,"current_rssi":90}) and
  (.[1] | .decode_status == {"code":4,"text":"receiver reported CRC failure"} and .code == "crc_failed" and
    .receiver == {"packet_rssi":100,"current_rssi":90} and (has("groups") | not)) and
  (.[2] | .decrypted == true and .primary_identity == .transport_identity and
    .receiver == {"packet_rssi":100,"current_rssi":90})
' "$tmp/output" >/dev/null

# CRC failures are reported even at EOF and never reach telegram decoding.
if printf 'rla;T1;0;0;time;1;1;11223344;not-hex\nC1;0;1;time;1;1;11223344;0x%s' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output" 2> "$tmp/error"; then
    echo "Expected CRC failures to affect exit status" >&2
    exit 1
fi
"$jq" -se --arg datagram "$frame" 'length == 2 and
    .[0].datagram == "not-hex" and .[1].datagram == $datagram and
    all(.[]; .decode_status.code == 4 and .code == "crc_failed" and (has("groups") | not))' "$tmp/output" >/dev/null
test ! -s "$tmp/error"

# Other receiver failures and invalid status flags remain errors; processing continues.
printf 'T1;1;0;time;1;1;11223344;0x%s\nT1;2;1;time;1;1;11223344;0x%s\nT1;1;1;time;1;1;11223344;0x%s\n' "$frame" "$frame" "$frame" > "$tmp/input"
if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < "$tmp/input" > "$tmp/output"; then
    echo "Expected nonzero status after invalid receiver checks" >&2
    exit 1
fi
"$jq" -Rse '
  split("\n") | length == 4 and .[3] == "" and
  (.[0] | fromjson | .code == "invalid") and
  (.[1] | fromjson | .code == "invalid") and
  (.[2] | fromjson | .decrypted == true)
' "$tmp/output" >/dev/null
"$jq" -se 'length == 3 and .[0].code == "invalid" and .[1].code == "invalid" and .[2].decrypted == true' "$tmp/output" >/dev/null

# Typed keys are selected by the ELL S bit; matching keys and untyped keys can
# contain multiple entries for the same identity.
cat > "$tmp/keys.json" <<'EOF'
[
  {"ala":"9344887766555508","type":"asynchronous","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"},
  {"ala":"9344887766555508","type":"asynchronous","key":"000102030405060708090A0B0C0D0E0F"},
  {"ala":"9344887766555508","type":"synchronous","key":"000102030405060708090A0B0C0D0E0F"},
  {"ala":"9344887766555508","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"}
]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\nT1;1;1;time;1;1;11223344;0x%s\n' "$frame" "$sync_frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"
"$jq" -se 'length == 2 and all(.[]; .decrypted == true)' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","type":"synchronous","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected opposite-type key to be excluded" >&2
    exit 1
fi
"$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","type":"asynchronous","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$sync_frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected opposite-type key to be excluded for synchronous frame" >&2
    exit 1
fi
"$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","type":"asynchronous","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"},
 {"ala":"9344887766555508","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null

# If no ALA entry matches, use the LLA; an ALA match takes precedence even if wrong.
cat > "$tmp/keys.json" <<'EOF'
[{"lla":"9344443322115537","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
printf 'rla;T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"lla":"9344443322115537","key":"000102030405060708090A0B0C0D0E0F"},
 {"ala":"9344887766555508","key":"FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF"}]
EOF
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected wrong ALA key to fail" >&2
    exit 1
fi
"$jq" -e --arg datagram "$frame" '.code == "invalid" and .datagram == $datagram and (.error | contains("verification"))' "$tmp/output" >/dev/null

cat > "$tmp/keys.json" <<'EOF'
[{"ala":"9344887766555508","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --keys "$tmp/keys.json" > "$tmp/output"
"$jq" -Rse 'split("\n") | length == 2 and .[1] == "" and
  (.[0] | fromjson | .decrypted == true and .primary_identity_layer == "application")' "$tmp/output" >/dev/null

# A short transport header has no ALA, so the complete link address is used.
cat > "$tmp/keys.json" <<'EOF'
[{"lla":"AE4C445522339907","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
printf 'T1;1;1;time;1;1;33225544;0x2144AE4C4455223399077A55001005DF31C4C903C475049A49454F13B76D77010307\n' |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true and .primary_identity_layer == "link" and .transport_identity == null' >/dev/null

# Mode 7 bit 13 is the counter flag, not the synchronization bit.
cat > "$tmp/keys.json" <<'EOF'
[{"lla":"AE4C445522339907","type":"asynchronous","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
mode7=2644AE4C4455223399077A5500182710010203041C4D6A81CB500683D39E114E1A4F4143010307
printf 'T1;1;1;time;1;1;33225544;0x%s\n' "$mode7" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null
# An ELL supplies the synchronization bit even for mode 7.
mode7_ell=2944AE4C4455223399078C20017A5500182710010203041C4D6A81CB500683D39E114E1A4F4143010307
if printf 'C1;1;1;time;1;1;33225544;0x%s\n' "$mode7_ell" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected asynchronous key to be excluded by mode 7 ELL S bit" >&2
    exit 1
fi
"$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null
cat > "$tmp/keys.json" <<'EOF'
[{"lla":"AE4C445522339907","type":"synchronous","key":"000102030405060708090A0B0C0D0E0F"}]
EOF
printf 'C1;1;1;time;1;1;33225544;0x%s\n' "$mode7_ell" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" |
    "$jq" -e '.decrypted == true' >/dev/null

printf '[{"ala":"9344887766555508","key":"000102030405060708090A0B0C0D0E0F",}]' > "$tmp/keys.json"
if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
    echo "Expected malformed key file to fail" >&2
    exit 1
fi
for bad_addresses in \
    '"lla":""' \
    '"ala":"","lla":"9344443322115537"' \
    '"ala":"9344887766555508","lla":"12"' \
    '"ala":"9344887766555508","lla":null' \
    '"ala":"9344887766555508","lla":"9344443322115537","lla":""' \
    '"ala":"9344887766555508","ala":"9344887766555508"'; do
    printf '[{%s,"key":"000102030405060708090A0B0C0D0E0F"}]' "$bad_addresses" > "$tmp/keys.json"
    if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
        echo "Expected invalid identity fields to fail" >&2
        exit 1
    fi
done
for bad_type in '"sync"' 'null' '"synchronous","type":"asynchronous"'; do
    printf '[{"ala":"9344887766555508","type":%s,"key":"000102030405060708090A0B0C0D0E0F"}]' "$bad_type" > "$tmp/keys.json"
    if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
        echo "Expected invalid key type to fail" >&2
        exit 1
    fi
done
for bad_keys in \
    '[]' \
    '[{}]' \
    '[{"type":"s"}]' \
    '[{"key":"000102030405060708090A0B0C0D0E0F","type":"sync"}]' \
    '[{"key":"000102030405060708090A0B0C0D0E0F","type":"x"}]' \
    '[{"key":"000102030405060708090A0B0C0D0E0F","type":"a","type":"s"}]' \
    '[{"key":"000102030405060708090A0B0C0D0E0F","unknown":1}]' \
    '[{"key":"000102030405060708090A0B0C0D0E0F"},]' \
    '[{"key":"000102030405060708090A0B0C0D0E0F"}],"key":"000102030405060708090A0B0C0D0E0F"'; do
    printf '[{"ala":"9344887766555508","keys":%s}]' "$bad_keys" > "$tmp/keys.json"
    if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
        echo "Expected invalid grouped keys to fail: $bad_keys" >&2
        exit 1
    fi
done
"$jq" -n '[{"lla":"AE4C445522339907","keys":
  [range(0;128) | {"type":"a","key":"000102030405060708090A0B0C0D0E0F"}]}]' > "$tmp/keys.json"
"$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output"
if printf 'T1;1;1;time;1;1;33225544;0x2144AE4C4455223399077A55001005DF31C4C903C475049A49454F13B76D77010307\n' |
    "$decoder" --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected too many matching keys to fail" >&2
    exit 1
fi
"$jq" -e '.code == "limit" and .decode_status.code == 3 and
    .decode_status.text == .error and (.error | contains("too many matching keys"))' "$tmp/output" >/dev/null
"$jq" -n '[{"lla":"AE4C445522339907","keys":
  [range(0;129) | {"type":"a","key":"000102030405060708090A0B0C0D0E0F"}]}]' > "$tmp/keys.json"
if "$decoder" --rtl-wmbus --keys "$tmp/keys.json" < /dev/null > "$tmp/output" 2>/dev/null; then
    echo "Expected grouped key count limit to fail" >&2
    exit 1
fi
printf '[]' > "$tmp/keys.json"
if printf 'T1;1;1;time;1;1;11223344;0x%s\n' "$frame" |
    "$decoder" --rtl-wmbus --keys "$tmp/keys.json" > "$tmp/output"; then
    echo "Expected missing key to fail" >&2
    exit 1
fi
"$jq" -e '.code == "unsupported" and (.error | contains("requires an AES key"))' "$tmp/output" >/dev/null
echo "rtl-wmbus tests passed"
