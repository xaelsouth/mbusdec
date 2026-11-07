#!/bin/sh
set -eu
decoder=$1
jq=$2

# Telegram and readings from wmbusmeters test_hex_binary_values.sh,
# revision a523d014a17ca24aeb8fa26c285468175eb89fdd. The upstream
# separator between header and payload is not part of the frame.
request='{"format":"wireless","telegram":"1844AE4C4455223399077A55000000041389E20100023B0000"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .format == "wireless" and .transport_identity == null and
  (.link_identity | .serial_no == 33225544 and .manufacturer == "SEN" and .version == "99" and .device_type == "07") and
  (.groups | length == 1) and
  (.groups[0] | .storage == 0 and .tariff == 0 and (.records | length == 2)) and
  (.groups[0].records[0] | .quantity == "volume" and .unit == "m3" and .record_hex == "041389E20100" and ((.value - 123.529) | fabs < 0.000001)) and
  (.groups[0].records[1] | .quantity == "volume_flow" and .unit == "m3/h" and .value == 0)
' >/dev/null

# Wired telegram and readings from wmbusmeters simulations/simulation_mbus.txt,
# same revision. The source # marks a boundary and is not part of the frame.
request='{"format":"wired","telegram":"6893936808007275404810C51400043127000004786BF99F00046D040F632B0415E02000004415E0200000840115E02000000406A02100004406A0210000840106A021000084100600000000C410060000000084110600000000426C5F2C026C7F2C043B00000000143B08010000042B00000000142B39240000025B1800025F17000461380000000223810C01FD17000490280B0000005616"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  .format == "wired" and .control == "08" and .address == 0 and .link_identity == null and
  (.transport_identity | .serial_no == 10484075 and .manufacturer == "EFE" and .version == "00" and .device_type == "04") and
  ([.groups[] | select(.storage == 0 and .tariff == 0) | .records[] |
    select(.record_hex == "0415E0200000" and .quantity == "volume" and .unit == "m3" and ((.value - 841.6) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | select(.storage == 1 and .tariff == 0) | .records[] |
    select(.record_hex == "4406A0210000" and .quantity == "energy" and .unit == "Wh" and .value == 8608000)] | length == 1) and
  ([.groups[] | select(.storage == 2 and .tariff == 0) | .records[] |
    select(.record_hex == "840106A0210000" and .value == 8608000)] | length == 1) and
  ([.groups[] | select(.storage == 0 and .tariff == 1) | .records[] |
    select(.record_hex == "84100600000000" and .value == 0)] | length == 1) and
  ([.groups[] | select(.storage == 0 and .tariff == 0 and .function == "maximum") | .records[] |
    select(.record_hex == "143B08010000" and .function == "maximum" and .unit == "m3/h" and ((.value - 0.264) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "046D040F632B" and .value == "2019-11-03T15:04")] | length == 1)
' >/dev/null

# QCaloric full-frame example and readings from simulations/simulation_c1.txt.
request='{"format":"wireless","telegram":"314493441234567835087A740000200B6E2701004B6E450100426C5F2CCB086E790000C2086C7F21326CFFFF046D200B7422"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  ([.groups[] | select(.storage == 0 and .function == "instantaneous") | .records[] |
     select(.quantity == "heat_cost_allocation" and .value == 127)] | length == 1) and
  ([.groups[] | select(.storage == 1 and .function == "instantaneous") | .records[] |
     select(.quantity == "heat_cost_allocation" and .value == 145)] | length == 1) and
  ([.groups[] | select(.storage == 17 and .function == "instantaneous") | .records[] |
     select(.quantity == "heat_cost_allocation" and .value == 79)] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "426C5F2C" and .value == "2018-12-31")] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "C2086C7F21" and .value == "2019-01-31")] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "326CFFFF" and .valid == false and .value == null)] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "046D200B7422" and .value == "2019-02-20T11:32")] | length == 1)
' >/dev/null

# CMa12w sensor from the same upstream wireless simulation.
request='{"format":"wireless","telegram":"2744961566666666201B7AF90000202F2F02651E094265180902FD1B30030DFD0F05302E302E340F"}'
printf '%s' "$request" | "$decoder" | "$jq" -e '
  ([.groups[] | select(.storage == 0 and .tariff == 0 and .function == "instantaneous") | .records[] |
    select(.record_hex == "02651E09" and .unit == "degC" and ((.value - 23.34) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | select(.storage == 1 and .tariff == 0 and .function == "instantaneous") | .records[] |
    select(.record_hex == "42651809" and .unit == "degC" and ((.value - 23.28) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | .records[] | select(.record_hex == "0DFD0F05302E302E34" and .status == "unsupported")] | length == 1)
' >/dev/null

# The encrypted upstream Apator telegram decrypts with its published test key,
# but its proprietary data structure is not a supported generic M-Bus record.
request='{"format":"wireless","telegram":"6E4401068888888805077A85006085BC2630713819512EB4CD87FBA554FB43F67CF9654A68EE8E194088160DF752E716238292E8AF1AC20986202EE561D743602466915E42F1105D9C6782A54504E4F099E65A7656B930C73A30775122D2FDF074B5035CFAA7E0050BF32FAAE03A77","keys":["00000000000000000000000000000000"]}'
if output=$(printf '%s' "$request" | "$decoder"); then
    echo "expected unsupported proprietary Apator payload" >&2
    exit 1
fi
printf '%s' "$output" | "$jq" -e '.code == "unsupported" and (.error | contains("special DIF"))' >/dev/null
echo "real telegram tests passed"
