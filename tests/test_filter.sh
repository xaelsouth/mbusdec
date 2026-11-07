#!/bin/sh
set -eu
decoder=$1
jq=$2
examples=$3
tmp=$(mktemp -d)
trap 'rm -f "$tmp/filter.json" "$tmp/input" "$tmp/output" "$tmp/error"; rmdir "$tmp"' EXIT HUP INT TERM

frame=304493444433221155378C00757288776655934455080004100500DFE2A782146D1513581CD2F83F39040CFD1078563412
other=16442D2C123456780107780202D20402152E16022A3423
printf 'T1;1;1;time;1;1;id;0x%s\nT1;1;1;time;1;1;id;0x%s\n' "$frame" "$other" > "$tmp/input"

"$decoder" --filter "$examples/filter.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].decrypted == true and .[0].primary_identity.serial_no == 55667788' "$tmp/output" >/dev/null

printf '[]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 2 and .[0].decrypted == true and .[1].primary_identity.serial_no == 78563412' "$tmp/output" >/dev/null

printf '[{"application_layer_address":"9344887766555508"}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].decrypted == true' "$tmp/output" >/dev/null
printf '[{"application_layer_address":"9344887766555508","link_layer_address":""}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].decrypted == true' "$tmp/output" >/dev/null
printf '[{"application_layer_address":"9344887766555508","link_layer_address":"9344443322115538"}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" < "$tmp/input" > "$tmp/output"
test ! -s "$tmp/output"

printf '[{"link_layer_address":"9344443322115537"}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].decrypted == true' "$tmp/output" >/dev/null

printf '[{"ala":{"serialNo":"55667788","deviceType":"08","version":"55","manufacturer":"QDS"},"lla":""}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].decrypted == true' "$tmp/output" >/dev/null

printf '[{"application_layer_address":"9344887766555508","link_layer_address":"9344443322115538"},{"link_layer_address":"2D2C123456780107"}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].primary_identity.serial_no == 78563412' "$tmp/output" >/dev/null

# Key files are valid filter files too; keys do not select decryption keys.
"$decoder" --filter "$examples/rtl-keys.json" --keys "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].decrypted == true' "$tmp/output" >/dev/null
if "$decoder" --filter "$examples/rtl-keys.json" < "$tmp/input" > "$tmp/output"; then
    echo "Expected missing decryption key to fail" >&2
    exit 1
fi
"$jq" -se 'length == 1 and .[0].code == "unsupported"' "$tmp/output" >/dev/null

for content in '{}' '[{}]' '[{"link_layer_address":""}]' \
    '[{"application_layer_address":"9344887766555508","ala":"9344887766555508"}]' \
    '[{"application_layer_address":"9344887766555508","unknown":1}]' \
    '[{"application_layer_address":"9344887766555508","keys":[]}]' \
    '[{"application_layer_address":"9344887766555508","type":"synchronous"}]' \
    '[{"application_layer_address":"9344887766555508"},]'; do
    printf '%s' "$content" > "$tmp/filter.json"
    if "$decoder" --filter "$tmp/filter.json" < /dev/null > "$tmp/output" 2> "$tmp/error"; then
        echo "Expected invalid filter to fail: $content" >&2
        exit 1
    fi
    test -s "$tmp/error"
done
if "$decoder" --filter "$tmp/missing.json" < /dev/null > "$tmp/output" 2> "$tmp/error"; then
    echo "Expected missing filter to fail" >&2
    exit 1
fi
test -s "$tmp/error"

"$jq" -n '[range(0; 128) | {"lla":"2D2C123456780107"}]' > "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" < "$tmp/input" > "$tmp/output"
"$jq" -se 'length == 1 and .[0].primary_identity.serial_no == 78563412' "$tmp/output" >/dev/null
"$jq" -n '[range(0; 129) | {"lla":"2D2C123456780107"}]' > "$tmp/filter.json"
if "$decoder" --filter "$tmp/filter.json" < /dev/null > "$tmp/output" 2> "$tmp/error"; then
    echo "Expected filter entry limit to fail" >&2
    exit 1
fi
test -s "$tmp/error"
printf '[]' > "$tmp/filter.json"
dd if=/dev/zero bs=32766 count=1 2>/dev/null | tr '\000' ' ' >> "$tmp/filter.json"
"$decoder" --filter "$tmp/filter.json" < /dev/null
printf ' ' >> "$tmp/filter.json"
if "$decoder" --filter "$tmp/filter.json" < /dev/null > "$tmp/output" 2> "$tmp/error"; then
    echo "Expected filter size limit to fail" >&2
    exit 1
fi
test -s "$tmp/error"

printf '[{"link_layer_address":"2D2C123456780107"}]' > "$tmp/filter.json"
printf 'T1;0;1;time;1;1;id;0x%s\nT1;1;1;time;1;1;id;0x123\nT1;1;1;time;1;1;id;0x0A442D2C12345678010790\n' "$other" > "$tmp/input"
if "$decoder" --filter "$tmp/filter.json" < "$tmp/input" > "$tmp/output"; then
    echo "Expected CRC, malformed telegram and unsupported header errors" >&2
    exit 1
fi
"$jq" -se 'length == 3 and .[0].code == "crc_failed" and .[1].code == "invalid" and .[2].code == "unsupported"' "$tmp/output" >/dev/null
echo "filter tests passed"
