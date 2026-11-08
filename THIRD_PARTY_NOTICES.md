# Third-party notices

Server Fixes is licensed under the GPL-3.0 (`LICENSE`) because its fixes follow GPL-3.0 code:

| Project | Author | License | What comes from it |
| --- | --- | --- | --- |
| [GameFixes_mm](https://github.com/SlynxCZ/GameFixes_mm) | Michal "Slynx" Přikryl | GPL-3.0 | The fixes and their Linux signatures (`src/signatures.h`), at commit 7042639; `src/movedata.h` |
| [CS2Fixes](https://github.com/Source2ZE/CS2Fixes) | Source2ZE | GPL-3.0 | beam_crash (0686a80), hammer_id (61937f7) and steam_ban (c82d21a), through GameFixes_mm |
| [cs2kz-metamod](https://github.com/KZGlobalTeam/cs2kz-metamod) and [CS2Fixes-RampbugFix](https://github.com/Interesting-exe/CS2Fixes-RampbugFix) | KZGlobalTeam, Interesting-exe | GPL-3.0 | rampbug, through GameFixes_mm |
| [ServerListPlayersFix](https://github.com/Source2ZE/ServerListPlayersFix) | Source2ZE | GPL-3.0 | server_list_players, through GameFixes_mm |

The code was written again for the AlliedModders SDK and Metamod:Source (`build-dependencies.json`). The slow_animation player count is new: GameFixes_mm's count read 0 with players on.

Build dependencies, fetched at build time and not shipped in this repository: Metamod:Source and KHook (zlib/libpng-style license), the AlliedModders s2sdk (Valve's SDK license), protobuf 3.21.8 (BSD-3-Clause, linked statically from the SDK).

Counter-Strike 2 and its libraries belong to Valve.
