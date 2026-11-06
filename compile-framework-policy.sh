#!/system/bin/sh
set -eu
S=/data/local/tmp/gsi17-graphics-20261105-01
RUN=$1
case "$RUN" in "$S"/attempts/*) ;; *) exit 2;; esac
test "$(readlink -f "$RUN")" = "$RUN"
test -s "$RUN/baseline.raw"
$S/magiskpolicy --load "$RUN/baseline.raw" --save "$RUN/framework.raw" --apply "$S/framework.rules"
