#!/bin/sh
set -eu
decoder=$1
jq=$2

request='{"format":"wireless","telegram":"16442D2C123456780107780202D20402152E16022A3423","keys":["00000000000000000000000000000000"]}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .format == "wireless" and .control == "44" and
  (.link_identity | .serial_no == 78563412 and .manufacturer == "KAM" and .version == "01" and .device_type == "07") and
  .transport_identity == null and
  (.groups | length == 1) and
  (.groups[0] | .storage == 0 and .tariff == 0 and .function == "instantaneous" and (.records | length == 3)) and
  .ci == "78" and
  ([.. | objects | select(has("value_hex"))] | length == 0) and
  (.groups[0].records[0] | .dif == "02" and .vif == "02" and .quantity == "energy" and .unit == "Wh" and .record_hex == "0202D204" and ((.value - 123.4) | fabs < 0.000001)) and
  (.groups[0].records[1] | .quantity == "volume" and .unit == "m3" and ((.value - 567.8) | fabs < 0.000001))
' >/dev/null

request='{"format":"wireless","telegram":"11442D2C1234567801077801830001010302"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  ([.groups[].records[]] | length == 2 and
    .[0].dif == "01" and .[0].vif == "83" and .[0].vife == "00" and .[0].vifes == ["00"] and
    (.[1] | (has("vife") or has("vifes")) | not))
' >/dev/null

request='{"format":"wireless","telegram":"21442D2C123456780107780003080305020000C03F05030000807F026CFFFF016F12"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  [.groups[].records[]] |
  length == 6 and
  (.[0] | .value == null and (has("valid") | not)) and
  (.[1] | .value == null and (has("valid") | not)) and
  (.[2] | ((.value - 0.15) | fabs < 0.000001) and (has("raw_value") | not)) and
  (.[3] | .value == null and .valid == false) and
  (.[4] | .value == null and .valid == false) and
  (.[5] | .status == "unsupported" and .value == null)
' >/dev/null

request='{"format":"wireless","telegram":"24442D2C12345678010778010301110302210303310304016C05110306C110030781500308"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  (.groups | length == 6) and
  (.groups[0] | .storage == 0 and .tariff == 0 and .function == "instantaneous" and
    (.records | length == 2) and .records[0].value == 1 and
    .records[1].status == "unsupported" and .records[1].record_hex == "016C05") and
  (.groups[1] | .storage == 0 and .tariff == 0 and .function == "maximum" and
    (.records | length == 2) and .records[0].value == 2 and .records[1].value == 6) and
  (.groups[2] | .storage == 0 and .tariff == 0 and .function == "minimum" and .records[0].value == 3) and
  (.groups[3] | .storage == 0 and .tariff == 0 and .function == "error" and .records[0].value == 4) and
  (.groups[4] | .storage == 1 and .tariff == 1 and .function == "instantaneous" and .records[0].value == 7) and
  (.groups[5] | .storage == 0 and .tariff == 1 and .function == "instantaneous" and .records[0].value == 8 and .records[0].subunit == 1) and
  (all(.groups[]; .function as $function | all(.records[]; .function == $function)))
' >/dev/null

request='{"format":"wireless","telegram":"19442D2C12345678010772876543213412020403040000010312"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  (.link_identity | .serial_no == 78563412 and .manufacturer == "KAM" and .version == "01" and .device_type == "07") and
  (.transport_identity | .serial_no == 21436587 and .manufacturer == "DQT" and .version == "02" and .device_type == "04") and
  .primary_identity == .transport_identity and
  (.groups[0].records[0].value == 18)
' >/dev/null

# OMS Vol. 2 Annex N.6.3, with link CRCs removed.
request='{"format":"wireless","telegram":"304493444433221155378C00757288776655934455080004100500DFE2A782146D1513581CD2F83F39040CFD1078563412","keys":["000102030405060708090A0B0C0D0E0F"]}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and .authenticated == false and
  .extended_link == {"ci":"8C","control":0,"access_number":117} and .ci == "72" and
  .link_identity.serial_no == 11223344 and
  .transport_identity.serial_no == 55667788 and
  ([.groups[] | select(.storage == 0 and .tariff == 0 and .function == "instantaneous") |
    .records[] | select(.quantity == "customer_location" and .value == 12345678 and
                         .record_hex == "0CFD1078563412")] | length == 1) and
  ([.groups[] | select(.storage == 1 and .tariff == 0 and .function == "instantaneous") |
    .records[] | select(.quantity == "date" and .value == "2007-04-30" and
                         .record_hex == "426CFE04")] | length == 1) and
  ([.groups[] | select(.storage == 1) | .records[] |
    select(.quantity == "heat_cost_allocation" and .value == 23456)] | length == 1)
' >/dev/null

request='{"format":"wireless","telegram":"17442D2C1234567801077801030141030281100303010304"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  (.groups | length == 3) and
  (.groups[0] | .storage == 0 and .tariff == 0 and (.records | length == 2)) and
  (.groups[1] | .storage == 1 and .tariff == 0) and
  (.groups[2] | .storage == 0 and .tariff == 1)
' >/dev/null

request='{"format":"wireless","telegram":"22442D2C12345678010778010301814003020103038180400304814003058180400306"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  (.groups | length == 3) and
  ([.groups[].subunit] == [0, 1, 2]) and
  (all(.groups[]; .storage == 0 and .tariff == 0 and .function == "instantaneous" and
    (.subunit as $subunit | all(.records[]; .subunit == $subunit)))) and
  ([.groups[0].records[].raw_value] == [1, 3]) and
  ([.groups[1].records[].raw_value] == [2, 5]) and
  ([.groups[2].records[].raw_value] == [4, 6])
' >/dev/null

# EN 13757-3:2018 Annex F.14, wrapped in a headerless wireless frame.
request='{"format":"wireless","telegram":"21442D2C1234567801077884046D002341118B04151630128D049513056901110203"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  ([.groups[] | select(.storage == 8 and .tariff == 0 and .function == "instantaneous") |
    .records[] | select(.profile.kind == "inverse")] | length == 1) and
  ([.groups[].records[] | select(.profile.kind == "inverse")][0] |
    .quantity == "volume" and .unit == "m3" and .record_hex == "8D049513056901110203" and
    ([.profile.values[] | has("value_hex")] | all(. == false)) and
    .profile.interval == 1 and
    .profile.interval_unit == "h" and .profile.base_record_index == 1 and
    ([.profile.values[].decoded_integer] == [123005,123003,123000]))
' >/dev/null

# EN 13757-3:2018 Annex F.10, monthly register profile.
request='{"format":"wireless","telegram":"24442D2C123456780107788490810003F04902008D908100831E0A34FEA0860100D0FB0100"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  ([.groups[].records[] | select(.profile.kind == "register")][0] |
    .storage_is_register == true and .profile.interval == 1 and
    .profile.interval_unit == "month" and .profile.interval_raw == 254 and
    ([.profile.values[].decoded_integer] == [100000,130000]))
' >/dev/null

request='{"format":"wireless","telegram":"1D442D2C12345678010778010301026F1234811003020D0302AABB010303"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  (.groups | length == 2) and
  (.groups[0].records | length == 4) and
  (.groups[0].records[1] | .status == "unsupported" and .value == null and .record_hex == "026F1234") and
  (.groups[0].records[2] | .status == "unsupported" and .record_hex == "0D0302AABB") and
  (.groups[0].records[3] | .value == 3 and .record_hex == "010303") and
  (.groups[1].records[0] | .value == 2 and .record_hex == "81100302")
' >/dev/null

expect_error() {
    if output=$(printf '%s' "$1" | "$decoder"); then
        echo "expected request to fail" >&2
        exit 1
    fi
    printf '%s' "$output" | "$jq" -e "$2" >/dev/null
}

request='{"format":"wireless","telegram":"0D442D2C56341200010778010301"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .link_identity == {"serial_no":123456,"manufacturer":"KAM","version":"01","device_type":"07"}
' >/dev/null
request='{"format":"wireless","telegram":"0D44685056341200ABCD78010301"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .link_identity == {"serial_no":123456,"manufacturer":"TCH","version":"AB","device_type":"CD"}
' >/dev/null
request='{"format":"wired","telegram":"680404680801780F9016"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .manufacturer_data_status == "undecoded" and .manufacturer_data_manufacturer == null
' >/dev/null
request='{"format":"wireless","telegram":"0C442D2C563412000107780FAA"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .manufacturer_data_status == "undecoded" and .manufacturer_data_manufacturer == "KAM"
' >/dev/null
expect_error '{"format":"wireless","telegram":"0D442D2C5A341200010778010301"}' \
  '.code == "invalid" and .error == "invalid link serial number BCD" and .datagram == "0D442D2C5A341200010778010301"'
expect_error '{"format":"wireless","telegram":"0D44000056341200010778010301"}' \
  '.code == "invalid" and .error == "invalid link manufacturer code" and .datagram == "0D44000056341200010778010301"'
expect_error '{"format":"wireless","telegram":"0D4468D056341200010778010301"}' \
  '.code == "invalid" and .error == "invalid link manufacturer code" and .datagram == "0D4468D056341200010778010301"'
expect_error '{"format":"wireless","telegram":"19442D2C123456780107728A6543213412020403040000010312"}' \
  '.code == "invalid" and .error == "invalid transport serial number BCD" and .datagram == "19442D2C123456780107728A6543213412020403040000010312"'
expect_error '{"format":"wireless","telegram":"19442D2C12345678010772876543210000020403040000010312"}' \
  '.code == "invalid" and .error == "invalid transport manufacturer code" and .datagram == "19442D2C12345678010772876543210000020403040000010312"'

expect_error '{"format":"wireless","telegram":"0E442D2C1234567801077A01000005"}' '.code == "unsupported" and (.error | contains("security"))'
expect_error '{"format":"wired","telegram":"00","keys":["BAD"]}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"A"}' '.code == "invalid" and (.error | contains("even number"))'
expect_error '{"format":"wireless","telegram":"16442D2C123456780107780202D20402152E16022A342"}' '.code == "invalid" and (.error | contains("even number"))'
expect_error '{"format":"wired","telegram":"\u0030\u0030\u0031"}' '.code == "invalid" and (.error | contains("even number"))'
expect_error '{"format":"wired","telegram":"ZZ"}' '.error | contains("invalid JSON")'
long_odd=$("$jq" -nr '["0" * 523] | .[0]')
expect_error "{\"format\":\"wired\",\"telegram\":\"$long_odd\"}" '.code == "invalid" and (.error | contains("even number"))'
expect_error '{"format":"wired","telegram":"00","telegram":"00"}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00",}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00"} garbage' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":[,]}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":["00000000000000000000000000000000",]}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":{}}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","unknown":"x"}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","form\u0061t":"wired"}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":["\u00ZZ"]}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":["\u0000"]}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":["\u0080"]}' '.error | contains("invalid JSON")'
expect_error '{"format":"wired","telegram":"00","keys":["\q"]}' '.error | contains("invalid JSON")'
request='{"form\u0061t":"wireless","telegram":"16442D2C123456780107780202D20402152E16022A3423","keys":[]}'
printf '%s' "$request" | "$decoder" | "$jq" -e \
    '.format == "wireless" and .groups[0].records[0].raw_value == 1234' >/dev/null

# Security mode 5, with a second unencrypted record following the encrypted block.
request='{"format":"wireless","telegram":"2144AE4C4455223399077A55001005DF31C4C903C475049A49454F13B76D77010307","keys":["00000000000000000000000000000000","000102030405060708090A0B0C0D0E0F"]}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and
  (.groups[0].records | length == 2) and
  (.groups[0].records[0] | .record_hex == "041389E20100" and ((.value - 123.529) | fabs < 0.000001)) and
  (.groups[0].records[1] | .record_hex == "010307" and .value == 7)
' >/dev/null
expect_error '{"format":"wireless","telegram":"1E44AE4C4455223399077A55001005DF31C4C903C475049A49454F13B76D77","keys":["00000000000000000000000000000000"]}' '.code == "invalid" and (.error | contains("verification"))'
expect_error '{"format":"wireless","telegram":"1E44AE4C4455223399077A55001005DF31C4C903C475049A49454F13B76D77"}' '.code == "unsupported" and (.error | contains("requires an AES key"))'
expect_error '{"format":"wireless","telegram":"1E44AE4C4455223399077A55002005DF31C4C903C475049A49454F13B76D77","keys":["000102030405060708090A0B0C0D0E0F"]}' '.code == "invalid" and (.error | contains("block count"))'
expect_error '{"format":"wireless","telegram":"0F442D2C123456780107780D93130100"}' '.code == "unsupported" and (.error | contains("compact profile"))'
request='{"format":"wireless","telegram":"26442D2C12345678010772876543213412020403041005CC1AC0B7CC5177A8516BBAC379E33C09","keys":["000102030405060708090A0B0C0D0E0F"]}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and .link_identity.serial_no == 78563412 and
  .transport_identity.serial_no == 21436587 and
  ((.groups[0].records[0].value - 123.529) | fabs < 0.000001)
' >/dev/null

# Mode 7 KDF-A vectors generated independently with OpenSSL CMAC and AES-CBC.
request='{"format":"wireless","telegram":"2644AE4C4455223399077A5500182710010203041C4D6A81CB500683D39E114E1A4F4143010307","keys":["00000000000000000000000000000000","000102030405060708090A0B0C0D0E0F"]}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and .authenticated == false and .key_id == 0 and
  .message_counter == 67305985 and
  (.groups[0].records | length == 2) and
  (.groups[0].records[0] | .record_hex == "041389E20100" and .raw_value == 123529) and
  (.groups[0].records[1] | .record_hex == "010307" and .value == 7)
' >/dev/null
request='{"format":"wireless","telegram":"2C442D2C12345678010772876543213412020403041027500201020304922C9405E989AC275A4D3EB089B1634A","keys":["000102030405060708090A0B0C0D0E0F"]}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .decrypted == true and .authenticated == false and .key_version == 2 and
  .transport_identity.serial_no == 21436587 and
  .groups[0].records[0].raw_value == 123529
' >/dev/null
expect_error '{"format":"wireless","telegram":"2344AE4C4455223399077A5500182710010203046CFC0E86F0C710F43E26DEDDD147505E","keys":["000102030405060708090A0B0C0D0E0F"]}' '.code == "invalid" and (.error | contains("padding"))'
expect_error '{"format":"wireless","telegram":"2344AE4C4455223399077A5500182710010203041C4D6A81CB500683D39E114E1A4F4143","keys":["00000000000000000000000000000000"]}' '.code == "invalid" and (.error | contains("verification"))'
expect_error '{"format":"wireless","telegram":"2344AE4C4455223399077A5500180710010203041C4D6A81CB500683D39E114E1A4F4143","keys":["000102030405060708090A0B0C0D0E0F"]}' '.code == "unsupported" and (.error | contains("AFL"))'
expect_error '{"format":"wireless","telegram":"0A44AE4C44552233990790"}' '.code == "unsupported" and (.error | contains("AFL"))'
expect_error '{"format":"wireless","telegram":"1344AE4C4455223399077A550018271001020304","keys":["000102030405060708090A0B0C0D0E0F"]}' '.code == "invalid" and (.error | contains("block count"))'
expect_error '{"format":"wireless","telegram":"0F44AE4C4455223399077A5500182710","keys":["000102030405060708090A0B0C0D0E0F"]}' '.code == "invalid" and (.error | contains("message counter"))'
echo "CLI tests passed"
