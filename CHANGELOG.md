# Changelog

## 1.0.0 (2026-11-08)

First release. Replaces GameFixes_mm; run one or the other, not both.

- 14 fixes: slow_animation, beam_crash, input_activator_crash, team_limit, server_list_players, steam_ban, sv_cheats, voice_flood, demo_record, hammer_id, abstract_entity, water_jump, rampbug (off by default), block_nav_lookup (off by default).
- `block_nav_lookup`, after CS2Fixes: `CNavMesh::GetNearestNavArea` returns "none". Only for servers without bots. `measure_only 1` logs how many lookups ran and how long they took, without blocking.
- `server_fixes_reload`: reads `server_fixes.cfg` again through a Metamod unload and load. If the file cannot be read, the running fixes stay.
- `server_fixes_status`: the fixes that are on and every signature with its address.
- At load, every signature is checked against the running `libserver.so`, with one summary line and a line for each one that failed.
- `slow_animation` only reloads the map when two independent counts both say the server is empty. GameFixes_mm reloaded under players because one of its counts read 0.
- Signatures are still found when another plugin's hook jump sits at the start of the function (GameFixes_mm leaves these after it unloads).
- The build fails when the plugin imports a symbol that no library CS2 loads provides.
- `tools/query_players.py` checks what the server browser lists; `tools/check_signatures.py` checks signatures against a `libserver.so` file.
- Gate tests (63 checks): config parsing, signature scanning, the player count, the steam_ban whitelist, team limits, voice limits, the RTTI vtable finder.

Tested live on a CS2 server (Metamod 2.0 build 1483, CounterStrikeSharp, CS2AC): every fix loads, the addresses match those GameFixes_mm found in the same process, all 12 signatures match once, and the server browser lists every player with a name.
