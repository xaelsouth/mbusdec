#!/bin/sh
set -eu
decoder=$1
jq=$2

# Exmbus tests record 02 FD 17 04 80 as error flags. The wireless
# header is synthetic; it is not an upstream telegram.
printf '%s' '{"format":"wireless","telegram":"1344AE4C4455223301077A5500000002FD170480"}' |
    "$decoder" | "$jq" -e '
      [.groups[].records[]] as $records |
      ($records | length) == 1 and
      ($records[0] | .record_hex == "02FD170480" and
        .quantity == "error_flags" and .value == 32772)
    ' >/dev/null

# Exmbus recognises VIFE 18 as a record error. mbusdec preserves it
# as unsupported rather than interpreting the BCD value as valid.
printf '%s' '{"format":"wireless","telegram":"1344AE4C4455223301077A550000000AA6180000"}' |
    "$decoder" | "$jq" -e '
      [.groups[].records[]] as $records |
      ($records | length) == 1 and
      ($records[0] | .record_hex == "0AA6180000" and
        .vifes == ["18"] and .status == "unsupported" and .value == null)
    ' >/dev/null
