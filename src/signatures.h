#pragma once

// Every code signature the plugin scans libserver.so for, Linux x86-64. kFn* start a function and may be
// found behind another hook's jump (sf::BehindHookJump); kSig* must match exactly. tools/check_signatures.py reads
// this file and checks each one matches exactly once in a given libserver.so, so a CS2 update that breaks
// one shows up before the plugin is installed. One `constexpr const char *kFn... / kSig... = "...";` per line.
// Taken from GameFixes_mm (Slynx, GPL-3.0), which took several from CS2Fixes; see THIRD_PARTY_NOTICES.md.

namespace sf::sig
{

// void CBeam::SetBeamOrigin(const Vector *)
constexpr const char *kFnBeamSetOrigin = "55 48 89 E5 41 54 49 89 F4 53 48 89 FB 48 83 EC ? 0F 1F 80";
// void CBeam::SetBeamEndPos(const Vector *)
constexpr const char *kFnBeamSetEndPos = "55 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4 53 48 89 FB 48 81 EC ? ? ? ? 66 0F 1F 44 00";
// bool CEntityIdentity::AcceptInput(CUtlSymbolLarge *, CEntityInstance *activator, CEntityInstance *caller, variant_t *, ...)
constexpr const char *kFnAcceptInput = "55 48 89 E5 41 57 41 56 4C 8D BD ? ? ? ? 4D 89 CE";
// The jnz in SetSchemaHammerUniqueId that skips writing m_sUniqueHammerID; the match starts on it.
constexpr const char *kSigHammerIdJnz = "75 ? 48 8B 03 48 89 DF FF 90 ? ? ? ? 84 C0 74 ? 48 8D 05";
// lea rsi, [rip + CCSGameRules::sm_mapGcBanInformation]
constexpr const char *kSigGcBanMapRef = "48 8D 35 ? ? ? ? 89 C2 48 63 46";
// void GameSystem_Think_CheckSteamBan()
constexpr const char *kFnCheckSteamBan = "55 48 89 E5 41 57 41 56 41 55 41 54 53 48 83 EC ? 48 8B 05 ? ? ? ? 44 8B 68 ? 45 85 ED 0F 8E ? ? ? ? 8B 15";

} // namespace sf::sig
