# Changelog

## v1.3 — Playback reliability update

- Added a continuously replenished 20-frame decoded video queue, providing one second of video at 20 FPS.
- Separated frame preparation and presentation through a dedicated libogc worker; prebuffered audio and video before starting the movie clock.
- Corrected audio-clock accounting to exclude fallback silence and final padding; added independent PCM read-ahead.
- Corrected final partial PCM DMA handling and the observed natural EOF watchdog stall.
- Retained late-frame recovery and expanded diagnostics, including queue occupancy, starvation and EOF/padding counters.
- Documented runtime functions without refactoring the hardware-tested engine.
- Added a GitHub conversion reminder and repository address beneath the file-browser title.
- Replaced the recommended conversion command with an FFmpeg/FFprobe Bash block that calculates and verifies a minimum two-second PCM margin, preserves MJPEG packets during padding, and protects existing outputs.
- Confirmed the media-side EOF workaround on physical hardware: padded Balto displayed all 97,864 frames with zero reported skips, audio gaps and empty-queue events, and no EOF warning. The experimental native EOF fallback is excluded.

**Hardware validation:** original Cyber Chase AVI completed on NTSC GameCube via SD Gecko Slot B with 87,948 displayed frames, zero skips, zero audio gaps and normal EOF. Additional partial movie tests and one pause/resume cycle were reported successful. PAL and v1.3-specific SD2SP2 validation are not established here.

**Unchanged:** MJPEG/PCM profile, 20 FPS, controls, wallpaper, storage detection, original libogc 3.2.1 toolchain and single-threaded video-only path.

## v1.2

- Corrected SD Gecko root directory navigation.
