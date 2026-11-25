# Extract actual playback implementation and decoder; replace only platform/UI glue.
from pathlib import Path
s=Path('source/main.c').read_text()
a=s[s.index('static void swizzle_to('):s.index('static int isavi(')]
b=s[s.index('static int video_chunk('):s.index('\nint main(void)')]
Path(__import__('sys').argv[1]).write_text(Path('tests/integration_prefix.c').read_text()+a+b+Path('tests/integration_suffix.c').read_text())
