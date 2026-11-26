# BEANZ AVI Player v1.3

**Simple by design. Reliable by implementation.**

A lightweight, standalone AVI/MJPEG movie player for the Nintendo GameCube. Version 1.3 improves playback reliability while preserving the focused interface, embedded wallpaper, controller controls and supported media profile.

## Features

- MJPEG at 320 × 240, 20 FPS (recommended profile); 32 kHz 16-bit PCM stereo or mono duplicated to stereo.
- One-second decoded video queue: 20 GX-ready frames, 3,072,000 bytes of texture storage, continuously replenished by a dedicated preparation thread.
- PCM and video prebuffering before playback; corrected movie-audio clock excludes fallback silence and final DMA padding.
- Independent audio read-ahead, bounded per-cursor cache, and sequential OpenDML AVIX continuation.
- Natural EOF handling for final partial PCM blocks; emergency frame skipping retained.
- Video-only playback through the existing single-threaded path.
- FAT storage via SD2SP2 or SD Gecko; simple browser; embedded wallpaper; playback diagnostics retained on errors.
- Standalone DOL; no external artwork or movie files required or supplied.

## Sample AVI File

A pre-converted sample AVI is provided for testing BEANZ AVI Player on real Nintendo GameCube hardware.

This file has been encoded using the recommended video and audio specifications, allowing users to verify playback without needing to convert their own media first.

If the sample plays correctly but your own AVI files do not, verify your media conversion settings. If the sample AVI also fails to play correctly, open a GitHub issue and include a screenshot of the complete playback diagnostics, your GameCube video mode, and the storage adapter and slot being used.

The sample is provided separately from the standalone DOL and is not required to run the application.

**Original footage credit:** NASA's Goddard Space Flight Center.

[Original NASA footage — GOLD Resources: Sarah Jones Mission Scientist](https://svs.gsfc.nasa.gov/12825)

## Hardware validation

On November 24, 2026, the Trace 03 engine completed the original Cyber Chase AVI on real NTSC GameCube hardware via SD Gecko Slot B. The developer reported smooth synchronized playback and successful pause/resume. Additional movies stopped partway through reported zero skips and gaps.

| Observation | Trace 02 (19-minute run) | Trace 03 (full movie) |
| --- | ---: | ---: |
| Displayed frames | 19,756 | 87,948 |
| Skipped frames | 3,106 | 0 |
| Audio gaps | 0 | 0 |
| Maximum SD read time | 152 ms | 153 ms |
| Maximum reported VI lag | 200 ms | 33 ms |

Trace 03 also reported normal EOF and zero empty-queue presentation events. These are different-duration runs, not a controlled matched benchmark or a guarantee for every file. PAL hardware, SD Gecko Slot A and v1.3-specific SD2SP2 testing are not established in this record; SD2SP2 was tested in earlier releases.

On November 25, 2026, the developer confirmed full playback of Balto using SD Gecko Slot B on the original v1.3 engine. The original file produced `Video extends beyond PCM EOF`, with 97,860 of 97,864 video frames displayed. The manually padded copy completed with all 97,864 frames displayed, zero reported audio gaps, skipped frames and empty-queue events, and no EOF warning. FFprobe reported 156,607,744 PCM sample frames for the padded file (792 ms beyond the video duration). This confirms the padding technique for this movie, not every file or the exact automatic command on physical hardware.

## Supported media profile

| Setting | Compatible / expected value |
| --- | --- |
| Container | `.avi` / RIFF AVI, with supported sequential OpenDML `AVIX` continuation |
| Video codec | MJPEG (`MJPG`) |
| Tested video size | **320 × 240** |
| Video rate | **20 FPS** (exactly) |
| Audio codec | Uncompressed integer PCM (`pcm_s16le`, AVI format tag 1) |
| Audio sample rate | **32,000 Hz** |
| Audio bit depth | **16-bit** |
| Audio channels | Stereo (tested) or mono (supported in code; duplicated to stereo) |
| No audio stream | Supported by the video-only playback path |

This is **not** a general-purpose AVI player. H.264, MPEG-4 Part 2, HEVC, AAC, MP3, AC-3, compressed audio, subtitles, and arbitrary resolutions or frame rates are outside the supported profile. File extensions alone do not establish compatibility.

### Recommended FFmpeg conversion command for a single file (Linux / Bash)

Install **FFmpeg and FFprobe** (normally distributed together), Bash 4 or newer, and standard Linux command-line utilities (`awk`, `mktemp`, `mv`, `rm`, `dirname`). On Ubuntu/Debian, FFmpeg and FFprobe are provided by the `ffmpeg` package. Run the following command in its entirety (the whole thing) from the directory containing the video, replacing the two quoted filenames on the `bash -s` line. Paste the entire block, including the final `BEANZ` delimiter. Input can be any supported container/codec that your FFmpeg build can decode. The output must be a new `.avi` file in an existing directory.

```bash
# BEANZ AVI Player - Automatic Video Conversion
# Requires FFmpeg, FFprobe, Bash, and standard Linux command-line utilities.
#
# Replace "input_video.mp4" with your desired input filename.
# Supported inputs include MP4, MKV, AVI, MOV, WMV, WebM, MPEG, and FLV.
# Replace "output.avi" with your desired output filename.
# The output filename must end in .avi.
#
# Automatically compensates for audio/video duration differences
# and ensures a minimum two-second PCM audio safety margin.

bash -s -- "input_video.mp4" "output.avi" <<'BEANZ'
set -euo pipefail
export LC_ALL=C
input=$1
output=$2
[[ "${output,,}" == *.avi && ! -e "$output" && ! -L "$output" ]] || { echo "Choose a new .avi output filename" >&2; exit 1; }
work=$(mktemp -d "$(dirname -- "$output")/.beanz.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

measure() {
  ffprobe -v error -show_packets -show_entries packet=stream_index,size \
    -of csv=p=0 "$1" | awk -F, '
    NF != 2 || $1 !~ /^[01]$/ || $2 !~ /^[0-9]+$/ {bad=1; next}
    $1==0 {if ($2==0) bad=1; else v++}
    $1==1 {if ($2%4) bad=1; else a+=$2/4}
    END {if (bad || !v) exit 1; printf "%.0f %.0f\n",v,a}'
}

ffmpeg -nostdin -xerror -n -i "$input" \
  -map 0:V:0 -map '0:a:0?' \
  -vf "fps=20,scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,setsar=1" \
  -c:v mjpeg -q:v 7 -pix_fmt yuvj420p \
  -c:a pcm_s16le -ar 32000 -ac 2 -f avi "$work/converted.avi"

counts=$(measure "$work/converted.avi")
read -r frames samples <<< "$counts"
result="$work/converted.avi"
audio=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name \
  -of csv=p=0 "$result")

if [[ -n "$audio" ]]; then
  target=$((frames * 1600 + 64000))
  expected=$samples
  if (( samples < target )); then
    ffmpeg -nostdin -xerror -n -i "$result" \
      -map 0:v:0 -map 0:a:0 -c:v copy -c:a pcm_s16le \
      -ar 32000 -ac 2 -af "apad=whole_len=$target" -f avi "$work/padded.avi"
    result="$work/padded.avi"
    expected=$target
  fi
  counts=$(measure "$result")
  read -r final_frames final_samples <<< "$counts"
  (( final_frames == frames && final_samples == expected && final_samples >= target )) \
    || { echo "Final stream verification failed" >&2; exit 1; }
  awk -v v="$frames" -v a="$final_samples" 'BEGIN {printf "Verified: audio margin %.6f seconds\n", (a-v*1600)/32000}'
else
  echo "Video-only input: no PCM clock required."
fi

mv -n -- "$result" "$output"
[[ ! -e "$result" ]] || { echo "Output already exists" >&2; exit 1; }
printf 'Saved: %s\n' "$output"
BEANZ
```

The conversion preserves the recommended 320 × 240, 20 FPS, MJPEG quality 7, square-pixel, 32 kHz signed 16-bit stereo PCM profile. Widescreen sources with square pixels are letterboxed rather than stretched; other aspect ratios are fitted into the output frame. Anamorphic inputs may need separate aspect-ratio normalization. The first non-attached video stream and first audio stream are selected; subtitles and additional streams are omitted. Files without audio retain the video-only path.

For audio-containing files, the command measures actual AVI packet sizes and frame counts after conversion, calculates a target of 1,600 PCM sample frames per video frame plus 64,000 extra sample frames, and adds only the silence needed for a **minimum two-second audio margin**. If sufficient audio already exists, no padding pass is needed. The second pass copies the MJPEG video without re-encoding. It verifies the final sample and frame counts before saving the output. Do not add `-shortest`. Existing output files are protected; temporary files are cleaned up. Allow enough free space for two AVI files during the padding pass, and check the filesystem's file size limit (FAT32: less than 4 GiB per file).

The PCM margin is recommended to avoid premature audio-clock exhaustion when video extends beyond PCM EOF in the existing engine. It is not a guarantee against unrelated playback issues. The automatic command is host-tested; the manual audio-padding technique was separately validated on real hardware using Balto. See [release verification](docs/FINAL-V1.3-VERIFICATION.txt). The experimental native EOF fallback is not included.

### Batch conversion (Linux / Bash, optional)

Use this alternative to the single-file command when converting an entire directory of movies. Install the same dependencies listed above (including Bash 4 or newer), plus the standard `mkdir` utility. Open a terminal in the directory containing your original movies and paste the entire code block below, including the final `BEANZ` delimiter. It is essential you copy the whole command! No filenames or paths need to be entered manually.

The command automatically creates a directory named `Converted for GameCube` inside the current working directory and saves each successfully converted AVI there. Original movies remain untouched. For example:

```text
Movies/
  Balto.mkv
  Cyber Chase.mp4
  Die Hard.mov
  Converted for GameCube/
    Balto.avi
    Cyber Chase.avi
    Die Hard.avi
```

The command recognizes the following input extensions, regardless of capitalization: `.mp4`, `.mkv`, `.avi`, `.mov`, `.wmv`, `.webm`, `.mpeg`, `.mpg`, `.flv`, `.m4v`, `.ts`, `.m2ts`, `.ogv`, and `.vob`. Actual decoding depends on the installed FFmpeg build. Only regular files directly inside the current directory are considered, including hidden files with recognized extensions. Subdirectories, symbolic links and unrelated file extensions are ignored. The output directory is not scanned.

```bash
# BEANZ AVI Player - Batch Video Conversion
# Requires FFmpeg, FFprobe, Bash, AWK, and standard Linux utilities.
# Open a terminal in the folder containing your movies and paste this entire block.
# Converted files go into "Converted for GameCube"; originals are untouched.
# Existing output files are skipped. Subdirectories are not scanned.
bash -s <<'BEANZ'
set -euo pipefail
export LC_ALL=C
for tool in ffmpeg ffprobe awk mktemp mv rm dirname mkdir; do
  command -v "$tool" >/dev/null || { printf 'Missing required tool: %s\n' "$tool" >&2; exit 1; }
done
outdir='./Converted for GameCube'
[[ ! -L "$outdir" ]] || { echo 'Output directory must not be a symbolic link.' >&2; exit 1; }
mkdir -p -- "$outdir"
shopt -s nullglob dotglob
found=0; converted=0; skipped=0; failed=0
trap 'exit 130' INT
trap 'exit 143' TERM

for input in ./*; do
  [[ -f "$input" && ! -L "$input" ]] || continue
  case "${input,,}" in
    *.mp4|*.mkv|*.avi|*.mov|*.wmv|*.webm|*.mpeg|*.mpg|*.flv|*.m4v|*.ts|*.m2ts|*.ogv|*.vob) ;;
    *) continue ;;
  esac
  found=$((found+1))
  name=${input##*/}
  output="$outdir/${name%.*}.avi"
  if [[ -e "$output" || -L "$output" ]]; then
    printf 'Skipped (output exists or name collision): %s\n' "$input"
    skipped=$((skipped+1))
    continue
  fi
  printf '\nConverting: %s\n' "$input"
  # Separate Bash process keeps conversion failures isolated to this movie.
  if bash -s -- "$input" "$output" <<'CONVERT'
set -euo pipefail
export LC_ALL=C
input=$1
output=$2
[[ "${output,,}" == *.avi && ! -e "$output" && ! -L "$output" ]] || { echo "Choose a new .avi output filename" >&2; exit 1; }
work=$(mktemp -d "$(dirname -- "$output")/.beanz.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

measure() {
  ffprobe -v error -show_packets -show_entries packet=stream_index,size \
    -of csv=p=0 "$1" | awk -F, '
    NF != 2 || $1 !~ /^[01]$/ || $2 !~ /^[0-9]+$/ {bad=1; next}
    $1==0 {if ($2==0) bad=1; else v++}
    $1==1 {if ($2%4) bad=1; else a+=$2/4}
    END {if (bad || !v) exit 1; printf "%.0f %.0f\n",v,a}'
}

ffmpeg -nostdin -xerror -n -i "$input" \
  -map 0:V:0 -map '0:a:0?' \
  -vf "fps=20,scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,setsar=1" \
  -c:v mjpeg -q:v 7 -pix_fmt yuvj420p \
  -c:a pcm_s16le -ar 32000 -ac 2 -f avi "$work/converted.avi"

counts=$(measure "$work/converted.avi")
read -r frames samples <<< "$counts"
result="$work/converted.avi"
audio=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name \
  -of csv=p=0 "$result")

if [[ -n "$audio" ]]; then
  target=$((frames * 1600 + 64000))
  expected=$samples
  if (( samples < target )); then
    ffmpeg -nostdin -xerror -n -i "$result" \
      -map 0:v:0 -map 0:a:0 -c:v copy -c:a pcm_s16le \
      -ar 32000 -ac 2 -af "apad=whole_len=$target" -f avi "$work/padded.avi"
    result="$work/padded.avi"
    expected=$target
  fi
  counts=$(measure "$result")
  read -r final_frames final_samples <<< "$counts"
  (( final_frames == frames && final_samples == expected && final_samples >= target )) \
    || { echo "Final stream verification failed" >&2; exit 1; }
  awk -v v="$frames" -v a="$final_samples" 'BEGIN {printf "Verified: audio margin %.6f seconds\n", (a-v*1600)/32000}'
else
  echo "Video-only input: no PCM clock required."
fi

mv -n -- "$result" "$output"
[[ ! -e "$result" ]] || { echo "Output already exists" >&2; exit 1; }
printf 'Saved: %s\n' "$output"
CONVERT
  then
    converted=$((converted+1))
  else
    status=$?
    # Do not continue converting movies if interrupted.
    if (( status == 130 || status == 143 )); then exit "$status"; fi
    printf 'Failed: %s (exit %s)\n' "$input" "$status" >&2
    failed=$((failed+1))
  fi
done
printf '\nBEANZ batch conversion summary\nMovies found: %s\nConverted: %s\nSkipped: %s\nFailed: %s\nOutput directory: %s/\n' \
  "$found" "$converted" "$skipped" "$failed" "$outdir"
(( failed == 0 ))
BEANZ
```

#### How batch conversion works

1. **Dependency and directory checks:** The command checks for the required tools, creates the output directory if necessary, and rejects a symbolic link at the output-directory path.
2. **Sequential processing:** Each recognized input file is converted separately, using its original basename with the extension replaced by `.avi`. Filenames containing spaces and special characters are handled without manual escaping. If the destination filename already exists, the input is skipped. If two inputs have the same basename (for example, `Balto.mp4` and `Balto.mkv`), the first successful conversion occupies that output name; later inputs targeting that name are skipped. Rename one of the originals before running the command if both versions are wanted.
3. **Initial conversion:** FFmpeg uses the same encoding profile as the single-file command: 320 × 240, exactly 20 FPS, MJPEG quality 7, `yuvj420p`, square pixels, and 32 kHz signed 16-bit stereo PCM when audio is present. Video is fitted into the output frame with black padding as needed; the anamorphic-input caveat above also applies. Only the first non-attached video stream and first audio stream are selected. Extra streams and subtitles are omitted.
4. **Automatic PCM length correction:** FFprobe reports the actual AVI packet sizes and stream indices. AWK counts video packets and divides the PCM byte count by four (one 16-bit stereo sample frame). The command calculates the minimum PCM target as `video_frames * 1600 + 64000`: 1,600 sample frames per video frame at 20 FPS and 32 kHz, plus two seconds of audio. If the converted audio falls short of that target, FFmpeg adds only the necessary silence at the end using `apad=whole_len`. If sufficient audio already exists, the padding pass is skipped. Existing longer audio is not trimmed. The second pass copies the MJPEG stream without re-encoding it. Video-only inputs remain video-only.
5. **Verification and saving:** For audio-containing files, the final AVI is measured again to confirm that the video frame count is unchanged, the PCM sample count matches the expected result and the minimum two-second PCM margin is present. Video-only files undergo the initial packet-count check without an audio-margin requirement. The finished AVI is moved into the output directory without intentionally overwriting an existing file. These checks verify stream counts, not every possible media defect or pre-existing synchronization problem.
6. **Failure isolation and cleanup:** Each movie runs in a separate Bash process. A conversion or verification failure is reported and the remaining inputs continue processing. Temporary working directories are removed on normal exit or handled interruption; abrupt termination or power loss can leave temporary files behind. Pressing Ctrl+C interrupts the batch. Originals and previously completed outputs are retained.

At the end, the command prints a summary, for example:

```text
BEANZ batch conversion summary
Movies found: 10
Converted: 8
Skipped: 1
Failed: 1
Output directory: ./Converted for GameCube/
```

`Movies found` counts recognized regular input files, not necessarily valid movies. `Converted` counts successfully saved outputs; `Skipped` counts existing output names or basename collisions; `Failed` counts unsuccessful conversions. A batch containing failures exits with a nonzero status after processing the remaining files. Running the command again skips existing outputs; it does not revalidate those files. To retry a previously converted movie, move or rename its existing output first.

Allow enough free space for all finished movies plus up to two temporary AVI files for the movie currently undergoing padding. The command does not split large files or enforce the destination SD card's file size limit. Check that each AVI is smaller than 4 GiB before copying it to FAT32 storage. Do not add `-shortest`.

**Batch conversion validation:** The batch command was host-tested using short synthetic non-AVI inputs, including MP4, MKV, MOV, WebM, WMV, MPG and FLV, with shorter, equal and longer audio durations, video-only input, filename collisions, existing outputs and a deliberately corrupt input. These tests do not establish full-length batch conversion or physical GameCube playback of the batch-generated fixtures. The same PCM padding technique was separately validated on real hardware as described above. This optional command requires no changes to the v1.3 DOL.

**Media rights:** Use movies you own and are legally entitled to convert and play. No movies are distributed in this repository.

**Sample AVI exception:** The separately provided NASA sample described above is intended for playback testing; no copyrighted commercial movies are distributed.

## Controls

| Context | Button | Action |
| --- | --- | --- |
| File browser | D-pad Up / Down | Move selection |
| File browser | A | Open a directory or play the selected AVI |
| File browser | B | Go to parent directory; remain within mounted volume |
| File browser | START | Exit the application / return control to the loader |
| Playback | A | Pause / resume |
| Playback | B | Stop playback and return to the file browser |

Controller **Port 1** is used. START is handled in the browser; to exit while a movie is playing, press **B**, then START.

## Storage detection

At startup, the player tries to mount the first available FAT device in this order:

1. **SD2SP2** (`sd:/`)
2. **SD Gecko Slot B** (`cardb:/`)
3. **SD Gecko Slot A** (`carda:/`)

Only the **first successfully mounted device** is browsed. There is no in-app device switcher or hot-swap flow. FAT media must be accessible through the installed toolchain's libfat / GameCube SD drivers. SD2SP2 and SD Gecko Slot B have been tested successfully on real hardware; the Slot A branch remains unverified.

## Build from source

### Requirements

- A GameCube-targeting **devkitPPC** toolchain (development used **devkitPPC r51**).
- **Original libogc 3.2.1** and matching GameCube headers/libraries (**not libogc2**).
- Compatible **libfat**, including the GameCube SD interfaces used in `source/main.c`.
- A PowerPC/GameCube **libjpeg** port with `jpeglib.h` and the `-ljpeg` library.
- Standard `make` and devkitPro environment variables `DEVKITPRO` and `DEVKITPPC`.

These libraries and development tools are **external dependencies**. Their headers, libraries, and installers are not bundled. The existing Makefile expects them in the usual `$(DEVKITPRO)/libogc` and `$(DEVKITPRO)/portlibs/ppc` locations. Newer toolchain/library combinations might require adjustments and have not been validated as exact substitutes.

On Linux or another environment with the matching toolchain, open a terminal in the repository's root directory (the one containing `Makefile`):

```bash
make clean && make
```

Expected result:

```text
beanz_avi_player.dol
```

Copy the DOL to an SD card readable by your GameCube loader (such as Swiss), launch it, and browse the SD card's `.avi` files. Only the DOL must be copied; the wallpaper is compiled into the executable.

**Reproducibility note:** The eight build inputs retain the tested Trace 03 playback logic. The resulting binary does not require PNG assets. Byte-for-byte identical DOLs across computers are **not** guaranteed without matching compiler, libraries, linker configuration, and environment. A GameCube cross-compiler was not available in the packaging environment, so this release package was not cross-compiled here. The final package retains the same playback code, with only two documentation lines and the previously approved blank-line adjustment in the browser relative to the original v1.3 source. See [final verification](docs/FINAL-V1.3-VERIFICATION.txt).

## Known limitations and diagnostics

- The queue absorbs temporary latency, not indefinitely insufficient throughput. The player can still skip late frames or insert fallback audio silence under adverse conditions.
- For audio-containing movies, use the conversion command above to ensure the PCM clock outlasts the video. Files with shorter PCM audio can still trigger `Video extends beyond PCM EOF`.
- AVI parsing is intentionally narrow: sequential supported AVIX continuation, not indexed seeking. Unusual layouts, malformed chunks and extra streams are not guaranteed.
- Video must be 320 pixels wide, even height 2–240 and exactly 20 FPS. PCM must be 32 kHz, 16-bit mono or stereo.
- No seek, playlists, subtitles, network streaming, file editing or persistent settings. The browser collects at most 128 entries per directory. One mounted device per launch.
- Startup preloads audio and up to 20 video frames; unsupported or poorly interleaved files may fail. Video-only playback retains its original path.
- PAL, unusual storage devices and other loaders are not exhaustively tested. Console layout can vary with display modes.

### Understanding the post-playback diagnostics

Version 1.3 deliberately retains the expanded diagnostic information used during development. After a movie finishes or is stopped, the file browser displays a summary of the playback session. If an error occurs, its message appears above the counters. The additional `TRACE` lines are available for PCM playback after audio DMA has started; video-only playback reports the basic summary instead. These are session totals and measurements, not error codes in themselves.

#### Basic playback statistics

| Displayed field | Meaning |
| --- | --- |
| `fr` | Number of video frames presented by the player. At 20 FPS, a complete movie should normally display its full frame count when no frames are skipped. |
| `avg SD/JPEG/SW` | Average preparation time per prepared frame, in milliseconds: AVI chunk scanning and reading (`SD`), JPEG decoding (`JPEG`), and conversion/reordering of decoded pixels into the GameCube GX texture layout (`SW`, swizzling). The SD measurement includes parser/cache work, not just physical SD reads. |
| `max` (after `avg SD/JPEG/SW`) | Longest observed time for each of those three phases, in the same order, in milliseconds. The three maxima need not come from the same frame. |
| `slow` | Number of prepared frames for which the combined read, decode and swizzle interval exceeded 50 ms (one frame period at 20 FPS). This is **not** a skipped-frame count. Buffered frames can absorb temporary delays. |
| `fill` | Number of AVI read-cache refill operations. For PCM playback, the counts from both the video reader and the independent audio reader are combined. |
| `max` (after `fill`) | Longest measured cache refill, including seeking and reading, in milliseconds, across both readers when PCM is active. |
| `pf` | Successful explicit AVI cache prefetch operations. Normally zero in threaded PCM playback, which uses the independent preparation worker instead of the video-only idle-time prefetch path. Zero does not mean buffering is disabled. |
| `audio gap` | Number of nonterminal fallback-silence DMA blocks queued because the next audio block was not ready. Each full block is 128 ms at 32 kHz. This equals the `TRACE queued` counter; a queued block is not necessarily consumed before playback stops. |
| `skip` | Total frames deliberately dropped to recover video timing: frames discarded before preparation plus overdue prepared frames discarded by the presenter. Zero is desirable. Frames left in the queue by an early stop or error are not necessarily counted as skips. |

#### Expanded PCM synchronization trace (new in v1.3)

| Displayed field | Meaning |
| --- | --- |
| `TRACE queued/start` | Two counts: nonterminal fallback-silence blocks queued, and nonterminal fallback-silence blocks that actually started DMA playback. Both should ideally be zero. Normal terminal silence is excluded from these two counts. |
| `silence ... ms` | Estimated milliseconds of substitute silence consumed by DMA, including terminal fallback silence if consumed. Does not include silent samples stored in the AVI itself or the separate final-block `pad` counter. |
| `IRQ` | Number of audio DMA callback interrupts handled while active and unpaused, including transfer-start/restart interrupts. Not a count of video frames or errors. |
| `Gap starts ms` | Raw DMA clock positions, in milliseconds from the audio timeline's start, at which the first three nonterminal fallback-silence blocks began. `-1` means no corresponding gap start was recorded. Only the first three are displayed. |
| `Raw/wall/media ms` | Three elapsed-time measurements, explained separately below. |
| `VI lag ms first/last/min/max` | First, last, minimum and maximum measured video presentation lag, in milliseconds, relative to the intended frame timestamp on the PCM media clock. Sampled after the GX draw/copy and vertical-retrace wait return. Includes presentation scheduling/retrace effects; it is not a direct measurement of TV latency or audible lip-sync error. |
| `PCM in ... fr` | Total valid PCM sample frames read into the audio FIFO, including any silence deliberately encoded into the AVI by FFmpeg. One stereo sample frame contains both channels (4 bytes at 16-bit stereo); at 32 kHz, divide this count by 32,000 to obtain the input audio duration in seconds. This counts input, not necessarily samples consumed by DMA if playback is stopped early. |
| `pre` | Raw source PCM bytes read during initial audio preloading, before playback starts. For the recommended stereo profile, 131,072 bytes (128 KiB) represents 32,768 sample frames, or 1.024 seconds of audio. Mono uses two source bytes per sample frame and is expanded to stereo internally. |
| `EOF` | Audio reader end-of-file flag: `1` means the PCM reader reached the end of its input; `0` means it had not reached EOF. `EOF 1` is normal after a complete movie and does not by itself indicate an error or prove all queued PCM was played. |
| `Q prep` | Number of video frames successfully decoded and converted to GX-ready textures by the preparation thread. Compare with `fr` and `skip` when investigating playback completion. |
| `peak` | Highest number of ready frames in the video queue; the capacity is 20 frames (one second at 20 FPS). |
| `min` | Lowest number of ready frames observed during the presentation loop, before taking the next due frame. Zero can occur normally after the last video frame; a low minimum alone does not prove starvation. |
| `empty VI` | Number of presentation-loop iterations where no ready frame was available and video EOF had not yet been reached. Each such iteration proceeds to a vertical-retrace wait. A zero count is desirable; this is not a count of skipped frames or all empty-queue moments. |
| `pad ... fr` | Sample frames of internal zero padding consumed at the end of a partially filled DMA block. This alignment padding is normal and is excluded from the media clock. It is **not** the two-second PCM safety padding added to the AVI by the conversion command. |

The three clock values in `Raw/wall/media ms` are:

- **Raw:** estimated total elapsed DMA sample time, including consumed fallback silence and internal DMA padding.
- **Wall:** hardware timebase elapsed from audio playback startup until cleanup begins; includes any user pause time, but excludes initial prebuffering and the subsequent worker join/cleanup.
- **Media:** estimated DMA-consumed valid movie PCM time, with fallback silence and internal DMA padding subtracted. This includes silence encoded into the AVI and is the timeline used for video scheduling during PCM playback. Integer millisecond rounding and the DMA countdown/interrupt snapshot affect these measurements; differences are not necessarily accumulated A/V drift.

#### How to interpret the results and report a problem

For a complete PCM movie, the ideal result is the expected video frame count displayed, `skip 0`, `audio gap 0`, `TRACE queued/start 0/0`, `Gap starts ms: -1 / -1 / -1`, `empty VI 0`, and no error message. `peak 20`, `min 0` at completion, nonzero `slow`/`fill`/`IRQ` counts, and a nonzero final `pad` can all be normal. Do not assume that a slow read or a nonzero VI lag is a playback defect. If playback was manually stopped, incomplete frame counts and `EOF 0` may be expected.

`Video extends beyond PCM EOF` means the PCM media clock has exhausted its valid input while a video frame remains queued. Use the recommended conversion command to ensure a sufficient audio tail. The separate `PCM DMA clock stalled` error indicates that the movie PCM clock did not advance within the two-second watchdog interval during unpaused playback. Neither message alone identifies the underlying cause of a problem.

##### Video extends beyond PCM EOF

Some AVI files, particularly those converted using older conversion commands without PCM audio padding, may contain a video stream that is slightly longer than their audio stream. When the PCM media clock has exhausted its valid input while video frames remain queued, the player may report:

`Video extends beyond PCM EOF`

**This message does not necessarily indicate a playback malfunction.** A movie can play smoothly and remain synchronized throughout, with only a few trailing video frames left undisplayed. If the message appears after the movie or credits have finished, especially when the screen is already black, it can generally be safely ignored. There is no need to reconvert an existing AVI solely because of this message if playback was otherwise satisfactory.

For new conversions, use the recommended single-file or batch FFmpeg command in this README. Both automatically verify a minimum two-second PCM audio margin for files containing audio to help prevent this condition. If playback ends significantly earlier than expected or meaningful video content is cut off, verify the conversion settings and audio/video stream durations.

If playback fails, stutters or loses synchronization, please open an issue at [github.com/nxs8739/beanz-avi-player/issues](https://github.com/nxs8739/beanz-avi-player/issues). Include a clear screenshot of the entire post-playback diagnostic area (including any error message and all `TRACE` lines), the player version, console/video mode (NTSC, PAL50 or PAL60), storage adapter and slot, conversion command or media profile, and whether playback ended naturally or was stopped manually. Describe where the issue occurred and whether it is repeatable. The counters help distinguish media duration mismatches, storage latency, audio underruns and video scheduling problems. They are diagnostic estimates, not a direct measurement of analog audio output or display latency.

The source may produce compiler warnings; no warning-driven refactoring was performed in this release.

## Project structure and host tests

- `source/`: browser and playback (`main.c`), AVI reader (`avi.c/.h`), PCM/DMA (`pcm_audio.c/.h`), frame queue (`frame_queue.h`), embedded wallpaper (`beanz_wallpaper.c`).
- `Makefile`: GameCube build; `tests/`: host regression harness; `docs/`: historical traces and verification results.
- `CHANGELOG.md`, `LICENSE`, `THIRD_PARTY.md`: release history and licensing.

Run `sh tests/run.sh` on Linux with GCC, pthreads, libjpeg development headers, FFmpeg, Python 3 and GNU linker tools. Use single-CPU affinity when comparing timing. The harness exercises actual playback coordination with host mocks, PCM tails, queue ownership and synthetic long-duration timing. It is not cycle-accurate GameCube emulation. Historical candidate notes predate the subsequent hardware validation; see [test limitations](docs/SYNC-TRACE-03.txt) and [historical results](docs/TEST-RESULTS.txt).

The README conversion block can be tested directly using `python3 tests/test_readme_conversion.py`. It creates temporary synthetic media and checks output properties, stream data integrity, and safety behavior. For this harness, Python 3 and the listed FFmpeg/Bash dependencies are required. `bash tests/test_readme_player.sh` additionally checks two automatically converted fixtures against the host-mocked playback engine.

## Credits and licensing

- **BEANZ:** project concept, requirements, scope and release decisions, GameCube hardware testing, and final approval.
- **AI-assisted development:** implementation and debugging were carried out collaboratively using AI assistance, with behavior verified and decisions made by BEANZ on real hardware.
- **devkitPro / libogc / libfat / JPEG library contributors:** independent upstream software used to build and run this application; see [`THIRD_PARTY.md`](THIRD_PARTY.md).

Original project source/documentation is released under the **MIT License**; see [`LICENSE`](LICENSE). External dependencies remain under their own licenses. This project is **unofficial** and is **not affiliated with, endorsed by, or sponsored by Nintendo**. Nintendo and GameCube names/logos are trademarks of their respective owners; the presence of themed artwork does not grant trademark rights. No proprietary Nintendo SDK or copyrighted movies are included.

**Release approach:** frozen, small, source-available. New features are not part of this release's scope.
