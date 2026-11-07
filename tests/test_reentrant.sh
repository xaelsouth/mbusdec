#!/bin/sh
# The library must use only reentrant C library functions and no mutable
# static state. Fail on any undefined libc symbol outside the allowlist and on
# writable data other than const pointer tables (which the linker places in
# .data for position-independent builds).
set -eu
archive=$1

command -v nm >/dev/null 2>&1 || exit 77

allowed='memcmp memcpy memmove memset pow snprintf __stack_chk_fail'
status=0
for symbol in $(nm -u "$archive" | awk '$1 == "U" {print $2}' | sort -u); do
    case $symbol in
        mbus_*|AES_*) continue ;;
    esac
    case " $allowed " in
        *" $symbol "*) ;;
        *) echo "unexpected library function: $symbol" >&2; status=1 ;;
    esac
done

# Writable data/bss symbols must be the known const pointer tables only.
for symbol in $(nm "$archive" | awk '$2 ~ /^[bBdD]$/ {print $3}'); do
    case $symbol in
        names.*|times.*) ;;
        *) echo "unexpected writable static: $symbol" >&2; status=1 ;;
    esac
done
exit $status
