#!/bin/sh
set -eu
decoder=$1
jq=$2

# Public test telegrams and keys from wmbusmeters simulation_t1.txt and
# test_t1_meters.sh, revision a523d014a17ca24aeb8fa26c285468175eb89fdd.
water='3944FA122162092002067A3600202567C94D48D00DC47B11213E23383DB51968A705AAFA60C60E263D50CD259D7C9A03FD0C08000002FD0B0011'
key='BEDB81B52C29B5C143388CBB0D15A051'
request=$(printf '{"format":"wireless","telegram":"%s","keys":["%s"]}' "$water" "$key")
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and .ci == "7A" and
  .link_identity.serial_no == 20096221 and .transport_identity == null and
  ([.groups[] | .records[] | select(.record_hex == "04136A000000" and
     .quantity == "volume" and .unit == "m3" and .raw_value == 106 and
     ((.value - 0.106) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "04933C00000000" and
     .quantity == "volume" and .value == 0 and
     .modifiers == ["backward_flow"])] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "046D282A9E27" and
     .value == "2020-07-30T10:40")] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "03FD0C080000" and
     .status == "unsupported")] | length == 1)
' >/dev/null

aventies='76442104710007612507727100076121042507B5006005E2E95A3C2A1279A5415E6732679B43369FD5FDDDD783EEEBB48236D34E7C94AF0A18A5FDA5F7D64111EB42D4D891622139F2952F9D12A20088DFA4CF8123871123EE1F6C1DCEA414879DDB4E05E508F1826D7EFBA6964DF804C9261EA23BBF03'
request=$(printf '{"format":"wireless","telegram":"%s","keys":["A004EB23329A477F1DD2D7820B56EB3D"]}' "$aventies")
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and .ci == "72" and
  .link_identity.serial_no == 61070071 and
  .transport_identity.serial_no == 61070071 and
  ([.groups[] | select(.tariff == 0 and .function == "instantaneous") |
    .storage as $storage | .records[] |
    select(.quantity == "volume" and .unit == "m3") |
    {storage: $storage, raw_value, exponent}] | sort_by(.storage)) ==
  ([466472,46596,45888,44965,44235,43107,42398,41523,40903,40079,39320,38863,37926,37126,35784] |
    to_entries | map({storage: .key, raw_value: .value, exponent: (if .key == 0 then -3 else -2 end)})) and
  ([.groups[] | .records[] | select(.quantity == "error_flags" and .raw_value == 0)] | length == 1)
' >/dev/null

expect_error() {
    if output=$(printf '%s' "$1" | "$decoder"); then
        echo "expected upstream AES request to fail" >&2
        exit 1
    fi
    printf '%s' "$output" | "$jq" -e "$2" >/dev/null
}

# Wrong/missing keys must never yield a successful result.
request=$(printf '{"format":"wireless","telegram":"%s","keys":["00000000000000000000000000000000"]}' "$water")
expect_error "$request" '.code == "invalid" and (.error | contains("AES verification"))'
request=$(printf '{"format":"wireless","telegram":"%s"}' "$aventies")
expect_error "$request" '.code == "unsupported" and (.error | contains("requires an AES key"))'

# The upstream Kamstrup example uses ELL AES-CTR, which is not yet supported.
request='{"format":"wireless","telegram":"2A442D2C998734761B168D2091D37CAC21E1D68CDAFFCD3DC452BD802913FF7B1706CA9E355D6C2701CC24","keys":["28F64A24988064A079AA2C807D6102AE"]}'
expect_error "$request" '.code == "unsupported" and (.error | contains("CI"))'

echo "upstream AES tests passed"
