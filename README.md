# Server Fixes

Metamod plugin for Linux CS2 servers. It fixes problems the game leaves open, 14 in all. Each fix is switched on or off in `server_fixes.cfg`.

| Fix | What it does |
| --- | --- |
| `slow_animation` | After a day or two on one map, animation and movement go sluggish (curtime is a 32-bit float). Every `reload_interval` seconds, when nobody is on the server, the map is reloaded. The rest of `mp_timelimit` carries over |
| `beam_crash` | A beam with no parent makes `CBeam::SetBeamOrigin` / `SetBeamEndPos` loop forever. Those calls are finished without the loop |
| `input_activator_crash` | A `TestActivator` input to a `filter_*` entity with no activator crashes the server. Those calls are dropped |
| `team_limit` | The game refuses a join as "team full" when a map has few spawn points. The team caps are raised to maxplayers every frame |
| `server_list_players` | The server browser shows the players: their SteamID, name and score go to Steam every `update_interval` seconds |
| `steam_ban` | The engine's ban and competitive-cooldown kick pass skips `whitelist` accounts, and below `sv_kick_players_with_cooldown 2` the cooldowns. `clear_after_pass` empties its list afterwards |
| `sv_cheats` | Turning `sv_cheats` off takes everyone out of noclip |
| `voice_flood` | Malformed or oversized voice messages are dropped, and each player gets 192 messages, 256 KiB and 1024 packets of voice per second. Nobody is kicked |
| `demo_record` | With `tv_enable 1`, GOTV stays connected and gets its own slot, so a demo keeps recording across match restarts |
| `hammer_id` | Every entity gets its Hammer id (`m_sUniqueHammerID`) |
| `abstract_entity` | A map entity whose class the game cannot construct (`weapon_cs_base` in old map ports) would crash the server on spawn. It is not created, like an unknown classname |
| `water_jump` | Touching the bottom of water no longer stops the jump out of it |
| `rampbug` | Surf and kz ramps: lessens getting stuck on or flung off a ramp, and landing on a slope keeps its speed. Off by default: it adds traces to every player's movement and only helps on surf or kz maps |
| `block_nav_lookup` | `CNavMesh::GetNearestNavArea` answers "none" without searching. Breaks bot movement, so only for servers that never run bots. Off by default. `measure_only 1` blocks nothing and logs the lookups per minute and the time they took. Measured on a 24-slot aim server with 1 to 2 players: 43 to 814 lookups and 0.2 to 12 ms a minute, so the saving there is next to nothing |

`slow_animation` must find the server empty in two independent ways before it reloads: the engine's authenticated Steam accounts, and the connected human player controllers. If it cannot count, it does not reload. GameFixes_mm, which this plugin replaces, reloaded the map under players because one of its counts read 0.

## Install

1. Build (below), or take a built `dist/addons`.
2. Copy `dist/addons` into `game/csgo/addons`. This adds `addons/metamod/server_fixes.vdf`, `addons/server_fixes/bin/server_fixes.so` and `addons/server_fixes/server_fixes.cfg`.
3. If GameFixes_mm is installed, remove it or turn off every fix in `game_fixes.ini` that Server Fixes also does. Two plugins must not hook the same function.
4. Restart the server. The console shows `signatures: 12 of 12 found in libserver.so`, then one line per fix: `hooked ...`, `off`, or `NOT loaded, <reason>`.

A fix that cannot find its function or field after a CS2 update stays off with a reason; the others still load.

## Configuration

`addons/server_fixes/server_fixes.cfg`, one `<fix>.<option> <value>` per line, `//` comments. Every option is in `configs/server_fixes.cfg` with its default and a comment. The file is read when the plugin loads.

Two server console (and rcon) commands; a player who types them is refused:

| Command | What it does |
| --- | --- |
| `server_fixes_reload` | Reads `server_fixes.cfg` again: Metamod unloads and loads the plugin. If the file cannot be read, the running fixes stay |
| `server_fixes_status` | The version, the fixes that are on, and every signature with its address (`patched by us` for the one `hammer_id` rewrites) |

A new `server_fixes.so` is not picked up by `server_fixes_reload`, because the old copy is still open when the load runs and Linux hands it back. Use `meta unload <id>`, wait a second, then `meta load addons/server_fixes/bin/server_fixes`, or restart.

Metamod 2.0 (build 1483 and current master) only closes a plugin library once every hook it ever created is removed, and its `Unloader` erases from that set while it walks it. A plugin that removes and adds hooks while it runs can be left open for good, and then the next `meta load` runs the old code. That is why `server_fixes_reload` goes through Metamod instead of rebuilding the hooks in place.

## Build

```bash
powershell.exe -ExecutionPolicy Bypass -File scripts/build-steamrt3.ps1
```

The build runs in Docker with Valve's SteamRT3 image, so the plugin runs on the same glibc as CS2. It first builds and runs the gate tests (`tests/test_core.cpp`: config parsing, signature scanning, the player count, the steam_ban whitelist, team limits, the voice limits and the RTTI vtable finder), then the plugin, then checks that every symbol the plugin imports exists in a library CS2 loads (a plugin with a missing one builds fine but fails at `meta load`). The result lands in `dist/addons`. Dependencies are pinned to exact commits in `build-dependencies.json`.

## After a CS2 update

```bash
python tools/check_signatures.py <server>/game/csgo/bin/linuxsteamrt64/libserver.so
```

Every code signature in `src/signatures.h` must match exactly once; the script names any that don't. The plugin runs the same check on the running server at load (one summary line, plus every signature that failed) and in `server_fixes_status`. Schema fields (offsets) are read from CS2 itself at load and need no update.

## Checking the server browser

```bash
python tools/query_players.py <host> [port]
```

Asks the server what the server browser sees (A2S_INFO and A2S_PLAYER) and passes when every human is listed with a name, which is what `server_list_players` fixes. GOTV counts as a bot and the engine lists it without a name. `--self-test` checks the parser.

## License

GPL-3.0 (`LICENSE`). The fixes follow GameFixes_mm by Slynx, which follows CS2Fixes and ServerListPlayersFix; see `THIRD_PARTY_NOTICES.md`.
