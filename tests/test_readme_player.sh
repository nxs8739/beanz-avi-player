#!/usr/bin/env bash
# Integration check using the README command and final v1.3 source with host hardware mocks.
set -euo pipefail
player=$(cd "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cd "$player"
python3 tests/integration.py "$work/play.c"
# Keep the original extracted C playback implementation, change only the host test entry point.
python3 - "$work/play.c" <<'PY'
import sys
from pathlib import Path
p=Path(sys.argv[1]); s=p.read_text(); s=s[:s.index('int main(int argc,char **argv)')]
p.write_text(s+'''
int main(int argc,char **argv){
 linear=malloc(VIDEO_FRAME_BYTES); tiled=malloc(VIDEO_FRAME_BYTES); jpegdata=malloc(MAXJPEG);
 assert(linear && tiled && jpegdata && argc==5);
 for(int i=1;i<argc;i+=2){
  unsigned expected=(unsigned)atoi(argv[i+1]);
  test_start=gettime(); stop_after_ms=0; stall_enabled=0;
  play_video(argv[i]);
  printf("%s\\n%s\\n%s",argv[i],status,debug_status); fflush(stdout);
  char prefix[32]; snprintf(prefix,sizeof(prefix),"%u fr |",expected);
  assert(frames==expected && !strncmp(status,prefix,strlen(prefix)));
  assert(strstr(status,"audio gap 0 skip 0") && strstr(debug_status,"EOF 1"));
  puts("PASS final v1.3 playback: every frame, no warning, no gaps or skips");
 }
 free(linear); free(tiled); free(jpegdata); return 0;
}
''')
PY
${CC:-cc} -D_GNU_SOURCE -DTHREADED_MOCK -std=gnu11 -Wall -Wextra -Werror -O2 \
    -I source -I tests/mock "$work/play.c" source/avi.c source/pcm_audio.c \
    -pthread -ljpeg -Wl,--wrap=fread -o "$work/play"
for spec in 'balto-like 3 2.792' 'five-second-deficit 6 1'; do
    read -r name vd ad <<< "$spec"
    ffmpeg -v error -nostdin -f lavfi -i "testsrc2=size=320x240:rate=20:duration=$vd" \
        -f lavfi -i "sine=frequency=440:sample_rate=32000:duration=$ad" \
        -c:v ffv1 -c:a pcm_s16le -ac 2 "$work/$name.mkv"
    python3 - "$work/$name.mkv" "$work/$name.avi" <<'PYCONVERT'
import sys
sys.path.insert(0, 'tests')
from test_readme_conversion import convert
print(convert(sys.argv[1], sys.argv[2]).stdout.decode(), end='')
PYCONVERT
done
timeout 40 "$work/play" "$work/balto-like.avi" 60 "$work/five-second-deficit.avi" 120
