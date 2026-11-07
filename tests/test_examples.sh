#!/bin/sh
set -eu
decoder=$1
jq=$2
examples=$3

for input in "$examples"/*.input.json; do
    expected=${input%.input.json}.output.json
    if output=$("$decoder" < "$input"); then
        status=0
    else
        status=$?
    fi
    if ! printf '%s\n' "$output" | "$jq" -e --slurpfile expected "$expected" \
        '. == $expected[0]' >/dev/null; then
        echo "Output differs for $input" >&2
        exit 1
    fi
    if "$jq" -e 'has("error")' "$expected" >/dev/null; then
        if [ "$status" -eq 0 ]; then
            echo "Expected failure for $input" >&2
            exit 1
        fi
    elif [ "$status" -ne 0 ]; then
        echo "Unexpected exit status $status for $input" >&2
        exit 1
    fi
done
echo "Example JSON tests passed"
