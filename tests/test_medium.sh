#!/bin/sh
set -eu
decoder=$1
jq=$2

while read -r value name; do
    request="{\"format\":\"wireless\",\"telegram\":\"0A442D2C1234567801${value}78\"}"
    printf '%s' "$request" | "$decoder" |
        "$jq" -e --arg value "$value" --arg name "$name" '
          .transport_identity == null and
          .link_identity.device_type == $value and
          .link_identity.medium == $name and
          .primary_identity == .link_identity
        ' >/dev/null
done <<'EOF'
00 MEDIUM_OTHER
01 MEDIUM_OIL
02 MEDIUM_ELECTRICITY
03 MEDIUM_GAS
04 MEDIUM_HEAT_OUTLET
05 MEDIUM_STEAM
06 MEDIUM_WARM_WATER
07 MEDIUM_WATER
08 MEDIUM_HEAT_COST_ALLOCATOR
09 MEDIUM_COMPRESSED_AIR
0A MEDIUM_COOLING_OUTLET
0B MEDIUM_COOLING_INLET
0C MEDIUM_HEAT_INLET
0D MEDIUM_HEAT_AND_COOLING
0E MEDIUM_BUS_SYSTEM
0F MEDIUM_UNKNOWN
10 MEDIUM_IRRIGATION_WATER
11 MEDIUM_WATER_LOGGER
12 MEDIUM_GAS_LOGGER
13 MEDIUM_GAS_CONVERTER
14 MEDIUM_CALORIFIC_VALUE
15 MEDIUM_HOT_WATER
16 MEDIUM_COLD_WATER
17 MEDIUM_DUAL_WATER
18 MEDIUM_PRESSURE
19 MEDIUM_ADC
1A MEDIUM_SMOKE_DETECTOR
1B MEDIUM_AMBIENT_SENSOR
1C MEDIUM_GAS_DETECTOR
20 MEDIUM_ELECTRICITY_BREAKER
21 MEDIUM_GAS_OR_WATER_VALVE
25 MEDIUM_CUSTOMER_DISPLAY
28 MEDIUM_WASTE_WATER
29 MEDIUM_GARBAGE
30 MEDIUM_SERVICE_UNIT
36 MEDIUM_RADIO_CONVERTER_SYSTEM
37 MEDIUM_RADIO_CONVERTER_METER
EOF

for value in 1D FF; do
    request="{\"format\":\"wireless\",\"telegram\":\"0A442D2C1234567801${value}78\"}"
    printf '%s' "$request" | "$decoder" |
        "$jq" -e --arg value "$value" '
          .link_identity.device_type == $value and .link_identity.medium == null and
          .primary_identity.medium == null
        ' >/dev/null
done
