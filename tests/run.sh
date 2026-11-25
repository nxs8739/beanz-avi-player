#!/bin/sh
set -eu
ulimit -c 0
cd "$(dirname "$0")/.."
TMP=$(mktemp -d);trap 'rm -rf "$TMP"' EXIT
CC=${CC:-cc}
$CC -std=gnu11 -Wall -Wextra -Werror -O2 -I source tests/test_queue.c -o "$TMP/queue"
$CC -std=gnu11 -Wall -Wextra -Werror -O2 -I source -I tests/mock tests/test_pcm.c source/pcm_audio.c -o "$TMP/pcm"
"$TMP/queue"
"$TMP/pcm"
python3 tests/integration.py "$TMP/integration.c"
$CC -D_GNU_SOURCE -DTHREADED_MOCK -std=gnu11 -Wall -Wextra -Werror -O2 -I source -I tests/mock "$TMP/integration.c" source/avi.c source/pcm_audio.c -pthread -ljpeg -Wl,--wrap=fread -o "$TMP/integration"
for q in 3 12; do
 ffmpeg -hide_banner -loglevel error -threads 1 -f lavfi -i 'testsrc2=size=320x240:rate=20:duration=2' -f lavfi -i 'sine=frequency=440:sample_rate=32000:duration=2' -c:v mjpeg -q:v "$q" -threads 1 -pix_fmt yuvj420p -c:a pcm_s16le -ac 2 -ar 32000 -y "$TMP/q$q.avi"
done
printf "file %s\nfile %s\n" "$TMP/q3.avi" "$TMP/q12.avi" > "$TMP/concat.txt"
ffmpeg -hide_banner -loglevel error -f concat -safe 0 -i "$TMP/concat.txt" -c copy -y "$TMP/mixed.avi"
"$TMP/integration" "$TMP/q3.avi" "$TMP/q12.avi" "$TMP/mixed.avi"
