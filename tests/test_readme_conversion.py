#!/usr/bin/env python3
"""Integration tests: actual FFmpeg conversions, byte-for-byte stream checks."""
import json
import os
import shlex
from pathlib import Path
import subprocess as sp
import tempfile

README = Path(__file__).resolve().parents[1] / 'README.md'
BLOCK = README.read_text().split('### Recommended FFmpeg conversion', 1)[1].split('```bash\n', 1)[1].split('\n```', 1)[0]

def convert(source, output, check=True):
    original = 'bash -s -- "input_video.mp4" "output.avi"'
    assert BLOCK.count(original) == 1
    command = BLOCK.replace(original, 'bash -s -- '+shlex.quote(str(source))+' '+shlex.quote(str(output)))
    return sp.run(['bash'], input=command.encode(), check=check, stdout=sp.PIPE, stderr=sp.PIPE)
VF = 'fps=20,scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,setsar=1'

def run(*args):
    return sp.run([str(x) for x in args], check=True, stdout=sp.PIPE, stderr=sp.PIPE).stdout

def ff(*args):
    return run('ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y', *args)

def probe(path):
    return json.loads(run('ffprobe', '-v', 'error', '-show_streams', '-of', 'json', path))['streams']

def video(path):
    return ff('-i', path, '-map', '0:v:0', '-c:v', 'copy', '-f', 'mjpeg', '-')

def pcm(path):
    return ff('-i', path, '-map', '0:a:0', '-c:a', 'pcm_s16le', '-f', 's16le', '-')

def check_case(root, name, vd, ad, size='320x240', rate=20, sr=32000, channels=2, ac='pcm_s16le', container='mkv'):
    source = root / (name + '.' + container)
    base = root / (name + ' baseline.avi')
    output = root / (name + ' final.avi')
    args = ['-f', 'lavfi', '-i', f'testsrc2=size={size}:rate={rate}:duration={vd}']
    if ad is not None:
        args += ['-f', 'lavfi', '-i', f'sine=frequency=440:sample_rate={sr}:duration={ad}']
    args += ['-c:v', 'mpeg4' if container == 'mp4' else 'ffv1']
    if ad is not None:
        args += ['-c:a', ac, '-ac', str(channels)]
    ff(*args, source)
    # Independently produce the original README profile for byte comparisons.
    ff('-i', source, '-map', '0:V:0', '-map', '0:a:0?', '-vf', VF, '-c:v', 'mjpeg',
       '-q:v', '7', '-pix_fmt', 'yuvj420p', '-c:a', 'pcm_s16le', '-ar', '32000', '-ac', '2', base)
    convert(source, output)
    before, after = probe(base), probe(output)
    assert len(after) == len(before) == (1 if ad is None else 2)
    v = after[0]
    assert (v['codec_name'], v['codec_tag_string'], v['width'], v['height'], v['r_frame_rate'], v['sample_aspect_ratio']) == ('mjpeg','MJPG',320,240,'20/1','1:1')
    assert v['pix_fmt'] == 'yuvj420p'
    frames = int(v['nb_frames'])
    assert frames == int(before[0]['nb_frames']) and frames > 0
    assert video(base) == video(output), 'MJPEG packet bytes changed'
    # Independently decode every frame, including final frame, and count decoded frames.
    decoded = ff('-xerror', '-i', output, '-map', '0:v:0', '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-')
    assert len(decoded) == frames * 320 * 240 * 3 // 2
    if ad is not None:
        a = after[1]
        assert (a['codec_name'], int(a['sample_rate']), a['channels'], a['bits_per_sample']) == ('pcm_s16le',32000,2,16)
        old, new = pcm(base), pcm(output)
        assert len(old)%4 == len(new)%4 == 0
        original = len(old)//4
        expected = max(original, frames*1600+64000)
        assert len(new)//4 == expected
        assert int(a['nb_frames']) == expected
        assert new[:len(old)] == old, 'Existing PCM changed'
        assert not any(new[len(old):]), 'Padding is not silent'
        print(f'PASS {name}: {frames} frames; initial margin {(original-frames*1600)/32000:+.6f}s; added {(expected-original)/32000:.6f}s; final margin {(expected-frames*1600)/32000:.6f}s; video and original PCM identical', flush=True)
    else:
        print(f'PASS {name}: {frames} frames, video-only preserved; MJPEG identical', flush=True)
    return output

def rejected(*args):
    result = convert(*args, check=False)
    assert result.returncode != 0

def main():
    print(run('ffmpeg','-version').decode().splitlines()[0], flush=True)
    with tempfile.TemporaryDirectory(prefix='beanz converter test ') as tmp:
        root = Path(tmp)
        cases = [
            ('Balto-like 208ms deficit',3,2.792),
            ('five second deficit',6,1),
            ('equal duration',2,2),
            ('half second surplus',2,2.5),
            ('one second surplus',2,3),
            ('exact two second surplus',2,4),
            ('three second surplus',2,5),
            ('nonaligned PCM boundary',2,1.9999375),
            ('very short clip',0.1,0.03),
            ('widescreen AAC mono resampling',2.13,1.87,'640x360',24,44100,1,'aac'),
            ('48k stereo source 30fps',2.17,2.04,'480x360',30,48000,2),
            ('MP4 MPEG4 AAC',2.5,2.1,'640x360',25,44100,2,'aac','mp4'),
            ('no audio stream',2,None),
        ]
        for c in cases:
            output=check_case(root,*c)
        # Negative cases: no silent overwrite or partial published file.
        original = output.read_bytes()
        rejected(root/'no audio stream.mkv',output)
        assert output.read_bytes() == original
        print('PASS refuse overwrite; existing file intact')
        rejected(root/'missing.mp4',root/'missing.avi')
        assert not (root/'missing.avi').exists()
        print('PASS missing input')
        (root/'invalid.mkv').write_bytes(b'not a movie')
        rejected(root/'invalid.mkv',root/'invalid.avi')
        assert not (root/'invalid.avi').exists()
        print('PASS malformed input; no published partial output')
        audio_only = root/'audio.wav'
        ff('-f','lavfi','-i','sine=duration=1',audio_only)
        rejected(audio_only,root/'audio.avi')
        assert not (root/'audio.avi').exists()
        print('PASS audio-only input rejected')
        rejected(root/'no audio stream.mkv',root/'wrong.mp4')
        assert not (root/'wrong.mp4').exists()
        print('PASS wrong output extension rejected')
        assert not list(root.glob('.beanz.*'))
        print('PASS all temporary conversion directories cleaned')
    print('PASS 13 media cases and 6 safety checks')

if __name__ == '__main__':
    main()
