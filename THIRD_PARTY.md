# Third-party dependencies and acknowledgments

BEANZ AVI Player's repository includes the application's own source and embedded wallpaper data. The **devkitPPC toolchain, libogc, libfat, and the JPEG decoder library are not copied into this source ZIP**; they are provided separately by the builder's development environment.

The MIT `LICENSE` covers the original project material to the extent the project contributors can license it. It **does not relicense** any upstream library, external toolchain, Nintendo-owned mark or third-party movie. Refer to the exact versions installed when reproducing or distributing a linked DOL.

## Build-time and linked dependencies

| Component | Use in this project | Upstream / license information |
| --- | --- | --- |
| [devkitPPC / devkitPro](https://devkitpro.org/) | PowerPC/GameCube cross-compilation, build rules and tooling | Toolchain components have their own upstream licenses; consult the installed distributions |
| [libogc](https://github.com/devkitPro/libogc) | GameCube display, GX, gamepad, audio DMA, SD device interfaces | See the [upstream `LICENSE`](https://github.com/devkitPro/libogc/blob/master/LICENSE), which includes original libogc terms and third-party notices |
| [libfat](https://github.com/devkitPro/libfat) | FAT filesystem access and SD device mounting | Refer to the exact installed libfat package/source for its license and notices |
| libjpeg (`-ljpeg`, `jpeglib.h`) | MJPEG frame decompression | License depends on the installed JPEG implementation; see upstream distribution. For [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md), the libjpeg API uses IJG-derived licensing. |

**Acknowledgment for JPEG implementations based on IJG's work:**

> This software is based in part on the work of the Independent JPEG Group.

When distributing a compiled DOL with statically linked libraries, preserve any required copyright notices, acknowledgments, and disclaimers from those libraries' actual installed versions. In particular, the libogc upstream `LICENSE` contains separate third-party redistribution requirements. Do not assume the MIT project license alone fulfills all binary redistribution obligations.

## GameCube artwork and marks

The wallpaper is embedded as GameCube YUYV data in `source/beanz_wallpaper.c`. It was produced for this application using AI-assisted artwork creation. **Nintendo and GameCube names, hardware appearance, and trademarks are associated with Nintendo**, and this project is not affiliated with Nintendo. No third-party trademark rights are conveyed by this repository's license.

## Media

No films, audio tracks, ROMs, firmware, Nintendo proprietary SDK files, or retail game files are shipped. Users are responsible for obtaining and converting their own legally usable media.
