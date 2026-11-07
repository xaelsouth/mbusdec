#!/bin/sh
set -eu
decoder=$1
jq=$2
fixtures=$3

decode() {
    telegram=$(tr -d '[:space:]' < "$fixtures/$1.hex")
    "$jq" -nc --arg telegram "$telegram" '{format:"wired", telegram:$telegram}' | "$decoder"
}

decode filler | "$jq" -e '
  (.transport_identity | .serial_no == 17677731 and .manufacturer == "KAM") and
  ([.groups[].records[] | select(.record_hex == "04833B88130000" and .quantity == "energy" and .unit == "Wh" and .value == 5000)] | length == 1) and
  ([.groups[].records[]] | length == 1)
' >/dev/null

decode kamstrup_multical_601 | "$jq" -e '
  (.transport_identity | .serial_no == 6855817 and .manufacturer == "KAM") and
  ([.groups[] | select(.storage == 0 and .tariff == 0 and .function == "instantaneous") | .records[] |
     select(.record_hex == "0406E7910000" and .unit == "Wh" and .value == 37351000)] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "04142CDB0000" and .unit == "m3" and ((.value - 561.08) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | select(.function == "maximum") | .records[] | select(.record_hex == "142DC0010000" and .value == 44800)] | length == 1) and
  ([.groups[] | select(.storage == 0 and .tariff == 1) | .records[] | select(.record_hex == "84100600000000" and .value == 0)] | length == 1) and
  ([.groups[] | select(.storage == 1) | .records[] | select(.record_hex == "440651820000" and .value == 33361000)] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "046D1A2F6511" and .value == "2011-01-05T15:26")] | length == 1)
' >/dev/null

decode landis+gyr_ultraheat_t230 | "$jq" -e '
  (.transport_identity | .serial_no == 66660205 and .manufacturer == "LUG") and
  ([.groups[].records[] | select(.record_hex == "097404" and .quantity == "actuality_duration" and .unit == "s" and .value == 4)] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "0B5A950100" and .quantity == "flow_temperature" and ((.value - 19.5) | fabs < 0.000001))] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "0B5E970100" and .quantity == "return_temperature" and ((.value - 19.7) | fabs < 0.000001))] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "0B620200F0" and .quantity == "temperature_difference" and ((.value + 0.2) | fabs < 0.000001))] | length == 1) and
  ([.groups[] | select(.storage == 0 and .tariff == 1) | .records[] | select(.record_hex == "89107107" and .value == 7 and .unit == "min")] | length == 1)
' >/dev/null

decode example_bcd_lvar | "$jq" -e '
  (.transport_identity | .serial_no == 427605 and .manufacturer == "AIR") and
  ([.groups[].records[] | select(.record_hex == "0E04131278650100" and .quantity == "energy" and .unit == "Wh" and .value == 1657812130)] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "0DFD0EC83130312135323344" and .status == "unsupported")] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "07FD174000000000000000" and .quantity == "error_flags" and .value == 64)] | length == 1) and
  ([.groups[].records[] | select(.record_hex == "01FF182F" and .status == "undecoded")] | length == 1)
' >/dev/null

echo "libmbus telegram tests passed"
