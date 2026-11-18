
# BEANZ AVI Player

A lightweight, standalone **AVI/MJPEG movie player for the Nintendo GameCube**.

Built for a focused purpose: play compatible AVI movies from an SD adapter on original GameCube hardware, with a simple controller-operated file browser, 32 kHz PCM audio, sequential AVIX/OpenDML continuation, and an embedded custom wallpaper. No external artwork, configuration files, or media-player assets are needed to launch the compiled DOL.

**Final release source — November 17, 2026.** This is the frozen version of the player, including the corrected wallpaper/menu framebuffer rendering. The project is considered feature-complete; no additional features are planned.


## Features

- AVI playback with **MJPEG video at 320 × 240, 20 frames/second** (the tested encoding profile).
- **32,000 Hz, 16-bit uncompressed PCM** audio; stereo is supported, and mono audio is duplicated to both output channels.
- Video-only AVI playback when no audio stream is present.
- Sequential playback across supported **OpenDML `AVIX` RIFF segments**, allowing compatible long AVI files to continue past the initial RIFF's movie data.
- Audio DMA used as the presentation clock for movies with PCM audio; on-time playback is prioritized over rendering every late frame.
- Bounded SD read-ahead, audio preroll, and buffered PCM playback.
- FAT SD media access via **SD2SP2** or **SD Gecko**.
- Simple file browser: directories and `.avi` files only.
- Purple GameCube-themed menu wallpaper embedded in `source/beanz_wallpaper.c`; there is **no runtime PNG dependency**.
- On-menu playback statistics after a movie ends or is stopped.
- Single `.dol` application; no movie files or copyrighted videos are supplied.

## Tested hardware and status

The final wallpaper/framebuffer build was compiled and visually approved on a **real Nintendo GameCube** on November 17, 2026. Full-length playback of compatible AVIX movies, including return to the browser at end of file, was verified during development. Multiple movies were successfully opened in succession. Both **SD2SP2** and **SD Gecko (Slot B)** have been used successfully on real hardware.

- **PAL GameCube compatibility:** **Not tested.** BEANZ AVI Player v1.0 has been verified on NTSC GameCube hardware only. Video output, display positioning, audio/video synchronization, and playback performance on PAL consoles remain unverified. PAL users are welcome to report their results.
- **SD Gecko Slot A:** mounting is implemented in the source but has **not been separately confirmed by the project's hardware tests**.
- **Other consoles or loaders:** not claimed as tested. The intended target is the original Nintendo GameCube with a DOL loader such as Swiss.
- **Different AVI encoders, unusual container layouts, SD card adapters and speed grades:** compatibility is not guaranteed.

The on-screen heading still says **`EXPERIMENTAL VIDEO TEST`**, a historical label intentionally retained in the frozen source. It does not mean this repository is a separate experimental version.

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

### Suggested FFmpeg conversion

With FFmpeg installed on a computer, a starting point for creating a compatible movie is:

```bash
ffmpeg -i input_video.mp4 \
  -vf "fps=20,scale=320:240" \
  -c:v mjpeg -q:v 7 -c:a pcm_s16le -ar 32000 -ac 2 \
  -f avi output.avi
```

`-q:v 7` is a quality setting that performed well in the project's real-hardware testing; it is **not** a universal guarantee. The conversion changes the source to a fixed 4:3 picture size (which may distort non-4:3 inputs), reduces frame rate to 20 FPS, and produces large files because the audio is uncompressed. If an input has no audio, the resulting AVI may be video-only. Test any converted file on your own hardware.

**Media rights:** Use movies you own and are legally entitled to convert and play. No movies are distributed in this repository.

## Controls

| Context | Button | Action |
| --- | --- | --- |
| File browser | D-pad Up / Down | Move selection |
| File browser | A | Open a directory or play the selected AVI |
| File browser | B | Go to parent directory (see known SD Gecko root issue below) |
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

**Reproducibility note:** The seven build inputs are frozen and sufficient for this build with the required toolchain. The resulting binary does not require PNG assets. Byte-for-byte identical DOLs across computers are **not** guaranteed without matching compiler, libraries, linker configuration, and environment. A GameCube cross-compiler was not available in the packaging environment, so this GitHub-ready ZIP itself was not rebuilt here; it preserves the exact user-tested source files.

## Known issues and limitations

The following points are documented deliberately rather than hidden. **They are not a request to change the frozen release.** Some are confirmed by real-hardware tests; others are identifiable source-code constraints or observed edge cases that remain unproven on every device.

### Known issues

| Issue | What to expect / workaround | Evidence |
| --- | --- | --- |
| **B at SD Gecko's root may switch to `sd:/`** | On `cardb:/` (and potentially `carda:/`), pressing B at the top-level directory can leave the browser pointed at `sd:/`, even if no SD2SP2 is mounted. Avoid pressing B at the root; relaunch if it happens. The `go_up()` path guard was written around `sd:/`. | Confirmed on Slot B and visible in the frozen source |
| **A pause/resume press can be missed** | The playback audio-wait loop scans input but acts on B, not A. A brief press during that loop can be consumed without toggling pause. Press A again if the first press does nothing. | Source-code control-path limitation / reproduced in test modeling |
| **Occasional PCM audio gaps / pops** | The player can substitute a **128 ms silence buffer** when a ready DMA audio block is unavailable. `audio gap` counts these underruns; several tests showed nonzero counters despite clean-sounding playback, while some encodes had faint pops. Exact causes of every audible artifact were **not conclusively established**. Slower SD access or demanding files can make this more likely. | Observed on real hardware; mechanism confirmed in the DMA code |
| **Variable video pacing / skipped frames** | MJPEG decoding and synchronous SD reads cost time. The player can skip late video frames to follow the audio clock. A nonzero `slow` counter is not by itself evidence of skipped frames or an audible defect; `skip` is the skipped-frame counter. Re-encoding at the tested profile and a moderate quality setting may help. | Code behavior and real-hardware statistics |

### Design limitations / compatibility boundaries

- **PAL video modes:** Compatibility with PAL GameCube hardware and 50 Hz display modes has not been tested or verified.
- **Strict codecs and timing:** MJPEG with exactly 20 FPS, 320-pixel width and an even video height no greater than 240. The known-good, recommended output is **320 × 240**. If PCM audio exists, it must be **32 kHz, 16-bit, mono or stereo**.
- **AVI parser is intentionally narrow:** Sequential AVI and supported AVIX continuation are handled, but unusual indexing, nested `rec ` structures, extra streams, malformed chunk layout, non-interleaved audio, and arbitrary OpenDML arrangements are **not guaranteed** to work. AVIX continuation is sequential, **not** indexed seeking.
- **No seek or fast-forward/rewind**, playlists, subtitles, soundtrack switching, network streaming, or file editing.
- **Only directories and `.avi` files appear** in the browser. At most **128 entries** per directory are collected; longer listings may be truncated. Long paths/names can be skipped to keep within fixed buffers.
- **One SD device per launch:** Device selection follows the fixed detection priority above. There is no live adapter picker.
- **Audio startup may take a moment:** Playback fills an audio preroll buffer (about one second of stereo PCM) before starting. Very short files or poorly interleaved PCM may fail with a preload/buffer message.
- **Fixed console layout:** The menu uses the standard libogc console with small dark character backgrounds over the embedded wallpaper. It is optimized for the tested GameCube/TV setup; other display modes and safe areas have not been exhaustively validated.
- **No per-movie saves or persistent settings.** Playback status exists only in memory during that run.

### Reading the playback statistics

After stopping a movie or reaching normal EOF, the bottom status reports abbreviated measurements such as:

- `fr`: video frames displayed (not the number originally present in the file).
- `avg SD/JPEG/SW`: average I/O, JPEG decode, and RGB565 tile-swizzle times in milliseconds.
- `max ... ms`: maximum observed times in those phases.
- `slow`: number of observed processing intervals exceeding the 50 ms target for 20 FPS; **not** the same as frames skipped.
- `fill`: input-cache refill count; `max` near it is the longest individual refill time.
- `pf`: proactive cache-prefetch count.
- `audio gap`: number of DMA silence-fallback buffers (each nominally 128 ms).
- `skip`: late video frames deliberately discarded to stay synchronized to audio.

The status is compact and may wrap on some TVs. Counters are diagnostics, not a benchmark or guarantee of media quality.

### Build warnings

The frozen source may emit warnings about unused declarations, integer signedness, indentation, or bounded-string handling; a successful build still ends with the `.dol` output. A `Clock skew detected` message can arise from extracted ZIP timestamps and is unrelated to the movie player. These warnings are documented rather than fixed to avoid changing the approved code.

## Project structure

```text
BEANZ-AVI-Player/
├── Makefile
├── README.md
├── LICENSE
├── THIRD_PARTY.md
├── .gitignore
└── source/
    ├── main.c             # Browser, JPEG decoding, framebuffer and playback control
    ├── avi.c              # AVI reader, read-ahead cache and AVIX continuation
    ├── avi.h
    ├── pcm_audio.c        # PCM conversion, ring buffering and GameCube DMA
    ├── pcm_audio.h
    └── beanz_wallpaper.c  # Embedded 640 × 480 YUYV wallpaper data
```

The entire runtime wallpaper is contained in `source/beanz_wallpaper.c`. The source PNGs and previous test patches are intentionally **not** part of this final minimal release. There are **seven original build files**, plus the four GitHub documentation/housekeeping files.

## Credits and licensing

- **BEANZ:** project concept, requirements, scope and release decisions, GameCube hardware testing, and final approval.
- **AI-assisted development:** implementation and debugging were carried out collaboratively using AI assistance, with behavior verified and decisions made by BEANZ on real hardware.
- **devkitPro / libogc / libfat / JPEG library contributors:** independent upstream software used to build and run this application; see [`THIRD_PARTY.md`](THIRD_PARTY.md).

Original project source/documentation is released under the **MIT License**; see [`LICENSE`](LICENSE). External dependencies remain under their own licenses. This project is **unofficial** and is **not affiliated with, endorsed by, or sponsored by Nintendo**. Nintendo and GameCube names/logos are trademarks of their respective owners; the presence of themed artwork does not grant trademark rights. No proprietary Nintendo SDK or copyrighted movies are included.

**Release approach:** frozen, small, source-available. New features are not part of this release's scope.
