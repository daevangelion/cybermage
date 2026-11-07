# Attributions

This project's own code and documentation are MIT-licensed ([LICENSE](LICENSE)).
The components and references below keep their own licences and copyrights.

## Included in this repository or in built binaries

| Component | Used for | Where | Licence | Copyright |
|---|---|---|---|---|
| [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) | Yamaha OPL3 FM synthesis for music | `port/src/audio/opl3.c`, `opl3.h` (unmodified); built into every binary | LGPL-2.1-or-later ([COPYING.Nuked-OPL3](port/src/audio/COPYING.Nuked-OPL3)) | © 2013–2020 Nuke.YKT |
| [SDL2](https://www.libsdl.org) 2.30 | window, input, audio, GL context | linked by desktop builds; compiled into the APK; `SDL2.dll` / `libSDL2` shipped in Windows and macOS packages | zlib | © 1997–2024 Sam Lantinga |
| SDL2 Android Java glue | `SDLActivity` base of the Android app | compiled into the APK from the SDL2 source tree | zlib | © 1997–2024 Sam Lantinga |
| [Gradle wrapper](https://gradle.org) | Android build bootstrap | `port/android/gradlew`, `gradlew.bat`, `gradle/wrapper/` | Apache-2.0 | © 2015 the original authors |
| [CyberMage: Darklight Re-Awakening](https://github.com/lodilorenzo/cybermage-darklight-re-awakening) | code excerpts in the oracle patch | `tools/oracle-original.patch` (a patch against that project) | MIT | © 2026 CyberMage: Darklight Re-Awakening contributors |

Nuked-OPL3 is statically linked. Its LGPL obligations are met by this
repository: the complete source of every binary is public, so anyone can
rebuild with a modified OPL3.

## Prior art and references consulted

These were read while reverse-engineering; no code from them is included
except as listed above. `[rust: path]` citations in `docs/re/` and the source
point to the first entry.

| Reference | Used for | Licence |
|---|---|---|
| [lodilorenzo/cybermage-darklight-re-awakening](https://github.com/lodilorenzo/cybermage-darklight-re-awakening) (Rust hybrid port) | layout guidance for formats, rendering, hands, audio/TGV, RNG and timers, controls; its unmodified host runs the original executable for comparison | MIT |
| [OPL3BankEditor HMI BNK specification](https://github.com/Wohlstand/OPL3BankEditor/blob/master/Specifications/HMI%20BNK%20Format.txt) | HMI instrument bank format | GPL-3.0 (documentation consulted only) |
| [TechHelp manual: keyboard shift/status flags](http://www.techhelpmanual.com/58-keyboard_shift_status_flags.html) | BIOS keyboard data area | reference |
| [SDL2 wiki](https://wiki.libsdl.org/SDL2/) and [Khronos OpenGL ES 3.0 reference pages](https://registry.khronos.org/OpenGL-Refpages/es3.0/html/) | platform and GL behaviour | reference |

## Tools used by `tools/` (not included)

| Tool | Used for | Licence |
|---|---|---|
| [Unicorn Engine](https://www.unicorn-engine.org) | runs functions of the original executable in the comparison scripts | GPL-2.0 (Python binding; installed separately) |
| Python 3 standard library | analysis scripts | PSF |

## The game

*CyberMage: Darklight Awakening* (1995) is © Origin Systems, Inc. / Electronic
Arts. Its executable, data, art, music, speech and movies are **not** part of
this repository or its builds and are not covered by its licence; you must
supply your own copy. Names and addresses in `labels.csv` and `docs/re/`
describe the original executable for interoperability.
