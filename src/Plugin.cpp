// zCombatRewind - Union plugin for Gothic I (1.08k_mod)
// Rewind to a checkpoint taken just before a fight instead of reloading the whole game.
// Stage 2: player only, checkpoint / rewind on hotkeys.

#include <Windows.h>
#include <Union/Hook.h>
#include "DevOverride.h"
#include <ZenGin/zGothicAPI.h>
#include <string>
#include <cstdio>
#include <vector>
#include <unordered_set>
#include "Snapshot.h"

HOOKSPACE(Gothic_I_Classic, GetGameVersion() == Engine_G1 && ZFSL_Active());

namespace Gothic_I_Classic {

#define HOOK(addr, fn) Union::CreateHook(reinterpret_cast<void*>(addr), fn, Union::HookType::Hook_Detours)

static std::string ExeDir() {
  char buf[MAX_PATH]; GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string p = buf; return p.substr(0, p.find_last_of("\\/") + 1);
}
static void Log(const std::string& text) {
  if (FILE* f = fopen((ExeDir() + "zCombatRewind.log").c_str(), "a")) {
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d] %s", st.wHour, st.wMinute, st.wSecond, text.c_str());
    if (text.empty() || text.back() != '\n') fputc('\n', f);
    fclose(f);
  }
}
static void Say(const char* text) { if (screen) screen->PrintTimedCXY(zSTRING(text), 2500.0f, nullptr); }

// ---------------------------------------------------------------- settings (GOTHIC.INI [ZCOMBATREWIND])
static int g_keyCheckpoint = 0x41, g_keyRewind = 0x42;   // KEY_F7, KEY_F8
static float g_radius = 3500.0f;                         // cm around the player
static void ReadSettings() {
  static bool done = false;
  if (done || !zoptions) return;
  done = true;
  g_keyCheckpoint = zoptions->ReadInt(zSTRING("ZCOMBATREWIND"), "KeyCheckpoint", 0x41);   // DirectInput scan codes
  g_keyRewind     = zoptions->ReadInt(zSTRING("ZCOMBATREWIND"), "KeyRewind", 0x42);
  g_radius        = (float)zoptions->ReadInt(zSTRING("ZCOMBATREWIND"), "Radius", 3500);
}

// ---------------------------------------------------------------- checkpoint state
static NpcSnap g_player;
struct SavedNpc { oCNpc* npc; NpcSnap snap; };
static std::vector<SavedNpc> g_npcs;   // we hold a reference on each, so the pointers stay valid

static void DropCheckpoint() {
  for (auto& n : g_npcs) n.npc->Release();
  g_npcs.clear();
  g_player = NpcSnap{};
}

template<class F> static void ForEachWorldNpc(F f) {
  oCWorld* w = ogame ? ogame->GetGameWorld() : nullptr;
  if (!w || !w->voblist_npcs) return;
  for (auto* node = w->voblist_npcs->next; node; node = node->next)
    if (node->data) f(node->data);
}

static float Dist(zCVob* a, zCVob* b) { return (a->GetPositionWorld() - b->GetPositionWorld()).Length(); }

static void Checkpoint() {
  oCNpc* p = oCNpc::player;
  if (!p) return;
  DropCheckpoint();
  TakeNpcSnap(p, g_player);
  ForEachWorldNpc([&](oCNpc* n) {
    if (n == p || Dist(n, p) > g_radius) return;
    n->AddRef();
    g_npcs.push_back({ n, NpcSnap{} });
    TakeNpcSnap(n, g_npcs.back().snap);
  });
  char buf[160];
  sprintf(buf, "checkpoint: player HP %d/%d, %d NPCs within %.0f m", g_player.attribute[0], g_player.attribute[1], (int)g_npcs.size(), g_radius / 100);
  Log(buf);
  Say("Punkt kontrolny zapisany");
}

static void Rewind() {
  oCNpc* p = oCNpc::player;
  if (!p) return;
  if (!g_player.valid) { Say("Brak punktu kontrolnego"); return; }
  std::unordered_set<oCNpc*> alive;
  ForEachWorldNpc([&](oCNpc* n) { alive.insert(n); });
  std::string report = "player:\n";
  RestoreNpcSnap(p, g_player, report, false);
  int restored = 0, gone = 0;
  for (auto& n : g_npcs) {
    if (!alive.count(n.npc)) { gone++; report += "  [" + std::string(n.npc->GetInstanceName().ToChar()) + "] no longer in the world (body removed) - not restored yet\n"; continue; }
    std::string r;
    RestoreNpcSnap(n.npc, n.snap, r, true);
    if (!r.empty()) report += std::string(n.npc->GetInstanceName().ToChar()) + ":\n" + r;
    restored++;
  }
  char buf[120];
  sprintf(buf, "rewind: %d NPCs restored, %d gone\n", restored, gone);
  Log(buf + report);
  Say("Cofnieto do punktu kontrolnego");
}

// ---------------------------------------------------------------- per-frame hook: hotkeys
void __fastcall Render_h(void*, void*);
auto Hook_Render = HOOK(0x0063DBE0, &Render_h);
void __fastcall Render_h(void* self, void* edx) {
  Hook_Render(self, edx);
  ReadSettings();
  if (!zinput || !oCNpc::player) return;
  if (zinput->KeyToggled(g_keyCheckpoint)) Checkpoint();
  if (zinput->KeyToggled(g_keyRewind)) Rewind();
}

// a load replaces the world: an old checkpoint must not be applied to it
void __fastcall LoadSavegame_h(void*, void*, int, int);
auto Hook_LoadSavegame = HOOK(0x0063C2A0, &LoadSavegame_h);
void __fastcall LoadSavegame_h(void* self, void* edx, int slot, int globals) { DropCheckpoint(); Hook_LoadSavegame(self, edx, slot, globals); }

void __fastcall ChangeLevel_h(void*, void*, const zSTRING&, const zSTRING&);
auto Hook_ChangeLevel = HOOK(0x0063CD60, &ChangeLevel_h);
void __fastcall ChangeLevel_h(void* self, void* edx, const zSTRING& a, const zSTRING& b) { DropCheckpoint(); Hook_ChangeLevel(self, edx, a, b); }

} // namespace Gothic_I_Classic

BOOL WINAPI DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    if (GetGameVersion() != Engine_G1) return FALSE;
    if (!ZFSL_Active()) return TRUE;
  }
  return TRUE;
}
