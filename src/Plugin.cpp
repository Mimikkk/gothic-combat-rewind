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
#include "Ui.h"

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
static long long NowMs() { return (long long)GetTickCount64(); }

// ---------------------------------------------------------------- settings (GOTHIC.INI [ZCOMBATREWIND])
static int g_keyCheckpoint = 0x41, g_keyRewind = 0x42;   // KEY_F7, KEY_F8 (DirectInput scan codes)
static float g_radius = 3500.0f;                         // cm around the player
static bool g_auto = true;                               // automatic checkpoint before fights
static long long g_combatEndMs = 6000;                   // calm time that ends a fight

static std::string KeyName(int dik) {
  if (dik >= 0x3B && dik <= 0x44) return "F" + std::to_string(dik - 0x3B + 1);   // F1..F10
  if (dik == 0x57) return "F11";
  if (dik == 0x58) return "F12";
  static const char* row[] = { "Q","W","E","R","T","Y","U","I","O","P" };
  static const char* row2[] = { "A","S","D","F","G","H","J","K","L" };
  static const char* row3[] = { "Z","X","C","V","B","N","M" };
  if (dik >= 0x10 && dik <= 0x19) return row[dik - 0x10];
  if (dik >= 0x1E && dik <= 0x26) return row2[dik - 0x1E];
  if (dik >= 0x2C && dik <= 0x32) return row3[dik - 0x2C];
  return "#" + std::to_string(dik);
}

static void ReadSettings() {
  static bool done = false;
  if (done || !zoptions) return;
  done = true;
  zSTRING sec("ZCOMBATREWIND");
  g_keyCheckpoint = zoptions->ReadInt(sec, "KeyCheckpoint", 0x41);
  g_keyRewind     = zoptions->ReadInt(sec, "KeyRewind", 0x42);
  g_radius        = (float)zoptions->ReadInt(sec, "Radius", 3500);
  g_auto          = zoptions->ReadInt(sec, "AutoCheckpoint", 1) != 0;
  UiSettings ui;
  ui.indicator = zoptions->ReadInt(sec, "ShowIndicator", 1) != 0;
  ui.hints     = zoptions->ReadInt(sec, "ShowHints", 1) != 0;
  ui.sounds    = zoptions->ReadInt(sec, "Sounds", 1) != 0;
  ui.flash     = zoptions->ReadInt(sec, "RewindFlash", 1) != 0;
  ui.keyCheckpoint = KeyName(g_keyCheckpoint);
  ui.keyRewind     = KeyName(g_keyRewind);
  UiConfigure(ui);
}

// ---------------------------------------------------------------- checkpoint state
static NpcSnap g_player;
struct SavedNpc { oCNpc* npc; NpcSnap snap; };
static std::vector<SavedNpc> g_npcs;   // we hold a reference on each, so the pointers stay valid
struct SavedItem { oCItem* item; int inst; int amount; zMAT4 trafo; };
static std::vector<SavedItem> g_items; // ground items around the checkpoint (referenced as well)
static zVEC3 g_center;

static void DropCheckpoint() {
  for (auto& n : g_npcs) n.npc->Release();
  g_npcs.clear();
  for (auto& i : g_items) i.item->Release();
  g_items.clear();
  g_player = NpcSnap{};
}

template<class F> static void ForEachWorldNpc(F f) {
  oCWorld* w = ogame ? ogame->GetGameWorld() : nullptr;
  if (!w || !w->voblist_npcs) return;
  for (auto* node = w->voblist_npcs->next; node; node = node->next)
    if (node->data) f(node->data);
}

template<class F> static void ForEachWorldItem(F f) {
  oCWorld* w = ogame ? ogame->GetGameWorld() : nullptr;
  if (!w || !w->voblist_items) return;
  std::vector<oCItem*> items;   // copy first: the callback may remove items from the list
  for (auto* node = w->voblist_items->next; node; node = node->next)
    if (node->data) items.push_back(node->data);
  for (oCItem* it : items) f(it);
}

static float Dist(zCVob* a, zCVob* b) { return (a->GetPositionWorld() - b->GetPositionWorld()).Length(); }

// ---------------------------------------------------------------- fight tracking
static long long g_cpTime = -1;        // when the checkpoint was taken
static bool g_inCombat = false;
static long long g_lastThreat = 0;     // last time an enemy near the player was targeting him
static long long g_deathTime = -1;
static bool g_restoring = false;       // our own engine calls must not trigger the automatics

static bool IsThreat(oCNpc* n, oCNpc* p) {
  return n != p && !n->IsDead() && !n->IsUnconscious() && n->enemy == p && Dist(n, p) < g_radius * 1.5f;
}

static void SnapNpc(oCNpc* n) {
  n->AddRef();
  g_npcs.push_back({ n, NpcSnap{} });
  TakeNpcSnap(n, g_npcs.back().snap);
}

static void Checkpoint(bool automatic) {
  oCNpc* p = oCNpc::player;
  if (!p || p->IsDead()) return;
  DropCheckpoint();
  g_cpTime = NowMs();
  TakeNpcSnap(p, g_player);
  ForEachWorldNpc([&](oCNpc* n) {
    if (n == p || Dist(n, p) > g_radius) return;
    SnapNpc(n);
  });
  g_center = p->GetPositionWorld();
  ForEachWorldItem([&](oCItem* it) {
    if ((it->GetPositionWorld() - g_center).Length() > g_radius) return;
    it->AddRef();
    g_items.push_back({ it, it->instanz, it->amount, it->trafoObjToWorld });
  });
  char buf[160];
  sprintf(buf, "%s checkpoint: player HP %d/%d, %d NPCs and %d ground items within %.0f m", automatic ? "auto" : "manual",
          g_player.attribute[0], g_player.attribute[1], (int)g_npcs.size(), (int)g_items.size(), g_radius / 100);
  Log(buf);
  UiToast(ToastKind::Checkpoint, automatic ? "Punkt kontrolny przed walką" : "Punkt kontrolny",
          KeyName(g_keyRewind) + " — cofnij do tej chwili");
  UiSound(false);
}

// Automatic checkpoint: when the player draws a weapon near an enemy, or an enemy takes the player as
// target. Never during a fight; enemies joining a running fight are added to the checkpoint as they come.
static void OnFightSignal(oCNpc* attacker) {
  oCNpc* p = oCNpc::player;
  if (!g_auto || g_restoring || !p || p->IsDead()) return;
  g_lastThreat = NowMs();
  if (g_inCombat) {
    if (attacker && g_player.valid) {
      for (auto& n : g_npcs) if (n.npc == attacker) return;
      SnapNpc(attacker);
      Log("late joiner added to the checkpoint: " + std::string(attacker->GetInstanceName().ToChar()));
    }
    return;
  }
  g_inCombat = true;
  // a checkpoint taken moments ago (e.g. weapon drawn just before the enemy noticed us) is better
  if (g_player.valid && NowMs() - g_cpTime < 20000) return;
  Checkpoint(true);
}

static void UpdateCombat() {
  oCNpc* p = oCNpc::player;
  if (!g_inCombat || !p) return;
  bool threat = false;
  ForEachWorldNpc([&](oCNpc* n) { if (!threat && IsThreat(n, p)) threat = true; });
  if (threat) g_lastThreat = NowMs();
  else if (NowMs() - g_lastThreat > g_combatEndMs) g_inCombat = false;
}

// Ground items first (before inventories): whatever appeared around the checkpoint since then is
// removed (weapons dropped on death, loot, arrows), whatever was picked up is put back, moved items
// go back to their place. Same calls as the scripts: Wld_InsertItem = CreateVob + SetPosition +
// AddVob + Release, Wld_RemoveItem = RemoveVob.
static int g_itemsChanged = 0;
static void RestoreGroundItems(std::string& report) {
  oCWorld* w = ogame ? ogame->GetGameWorld() : nullptr;
  if (!w) return;
  std::unordered_set<oCItem*> saved, present;
  for (auto& i : g_items) saved.insert(i.item);
  int removed = 0, created = 0, moved = 0;
  ForEachWorldItem([&](oCItem* it) {
    present.insert(it);
    if (saved.count(it)) return;
    if ((it->GetPositionWorld() - g_center).Length() > g_radius * 1.5f) return;
    report += "  remove new ground item " + std::string(it->GetInstanceName().ToChar()) + "\n";
    w->RemoveVob(it);
    removed++;
  });
  for (auto& i : g_items) {
    if (present.count(i.item)) {
      zVEC3 now = i.item->GetPositionWorld(), was(i.trafo[0][3], i.trafo[1][3], i.trafo[2][3]);
      if ((now - was).Length() > 10.0f) { i.item->SetTrafoObjToWorld(i.trafo); moved++; }
      i.item->amount = i.amount;
      continue;
    }
    oCItem* it = (oCItem*)w->CreateVob(zVOB_TYPE_ITEM, i.inst);
    if (!it) continue;
    it->amount = i.amount;
    it->SetTrafoObjToWorld(i.trafo);
    w->AddVob(it);
    report += "  put back ground item " + std::string(it->GetInstanceName().ToChar()) + "\n";
    it->Release();
    created++;
  }
  g_itemsChanged = removed + created + moved;
  char buf[120];
  sprintf(buf, "ground items: %d removed, %d put back, %d moved back\n", removed, created, moved);
  report = buf + report;
}

static void Rewind() {
  oCNpc* p = oCNpc::player;
  if (!p) return;
  if (!g_player.valid) {
    UiToast(ToastKind::Info, "Brak punktu kontrolnego", KeyName(g_keyCheckpoint) + " — zapisz punkt kontrolny");
    return;
  }
  g_restoring = true;
  std::unordered_set<oCNpc*> alive;
  ForEachWorldNpc([&](oCNpc* n) { alive.insert(n); });
  std::string report;
  RestoreGroundItems(report);
  report += "player:\n";
  RestoreNpcSnap(p, g_player, report, false);
  int restored = 0, gone = 0, changed = 0;
  for (auto& n : g_npcs) {
    if (!alive.count(n.npc)) { gone++; report += "  [" + std::string(n.npc->GetInstanceName().ToChar()) + "] no longer in the world (body removed) - not restored yet\n"; continue; }
    std::string r;
    RestoreNpcSnap(n.npc, n.snap, r, true);
    if (!r.empty()) { report += std::string(n.npc->GetInstanceName().ToChar()) + ":\n" + r; changed++; }
    restored++;
  }
  g_restoring = false;
  g_inCombat = false;
  g_deathTime = -1;
  char buf[120];
  sprintf(buf, "rewind: %d NPCs restored (%d changed), %d gone\n", restored, changed, gone);
  Log(buf + report);

  std::string sub;
  auto plural = [](int n, const char* one, const char* few, const char* many) {
    int m10 = n % 10, m100 = n % 100;
    return std::to_string(n) + " " + (n == 1 ? one : (m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14)) ? few : many);
  };
  if (changed) sub = "Przywrócono " + plural(changed, "postać", "postacie", "postaci");
  if (g_itemsChanged) sub += (sub.empty() ? "Przywrócono " : ", ") + plural(g_itemsChanged, "przedmiot", "przedmioty", "przedmiotów");
  if (sub.empty()) sub = "Stan sprzed " + std::to_string((NowMs() - g_cpTime) / 1000) + " s";
  UiRewindFlash();
  UiSound(true);
  UiToast(ToastKind::Rewind, "Czas cofnięty", sub);
}

// ---------------------------------------------------------------- per-frame hook: hotkeys
void __fastcall Render_h(void*, void*);
auto Hook_Render = HOOK(0x0063DBE0, &Render_h);
void __fastcall Render_h(void* self, void* edx) {
  Hook_Render(self, edx);
  ReadSettings();
  oCNpc* p = oCNpc::player;
  if (!zinput || !p) return;
  if (zinput->KeyToggled(g_keyCheckpoint)) Checkpoint(false);
  if (zinput->KeyToggled(g_keyRewind)) Rewind();

  static long long lastCombatCheck = 0;
  if (NowMs() - lastCombatCheck > 500) { lastCombatCheck = NowMs(); UpdateCombat(); }
  if (p->IsDead()) { if (g_deathTime < 0) { g_deathTime = NowMs(); g_inCombat = false; } }
  else g_deathTime = -1;

  UiFrame(g_player.valid ? NowMs() - g_cpTime : -1, g_inCombat, g_deathTime >= 0 ? NowMs() - g_deathTime : -1);
}

// fight signals --------------------------------------------------------------------------------------
// oCNpc::SetEnemy(oCNpc*) (0x00691A80): an NPC / monster takes the player as target
void __fastcall SetEnemy_h(oCNpc*, void*, oCNpc*);
auto Hook_SetEnemy = HOOK(0x00691A80, &SetEnemy_h);
void __fastcall SetEnemy_h(oCNpc* self, void* edx, oCNpc* enemy) {
  bool fresh = enemy && enemy == oCNpc::player && self != enemy && self->enemy != enemy;
  Hook_SetEnemy(self, edx, enemy);
  if (fresh && !self->IsDead()) OnFightSignal(self);
}

// oCNpc::SetWeaponMode2(int) (0x00695A60): the player draws a weapon with an enemy around
void __fastcall SetWeaponMode2_h(oCNpc*, void*, int);
auto Hook_SetWeaponMode2 = HOOK(0x00695A60, &SetWeaponMode2_h);
void __fastcall SetWeaponMode2_h(oCNpc* self, void* edx, int mode) {
  int before = self->GetWeaponMode();
  Hook_SetWeaponMode2(self, edx, mode);
  oCNpc* p = oCNpc::player;
  if (self != p || before != 0 || mode == 0 || g_inCombat || g_restoring) return;
  bool enemyNear = false;
  ForEachWorldNpc([&](oCNpc* n) {
    if (enemyNear || n == p || n->IsDead() || n->IsUnconscious() || Dist(n, p) > g_radius) return;
    if (n->GetAttitude(p) <= 1 /* NPC_ATT_HOSTILE / NPC_ATT_ANGRY */ || n->enemy == p) enemyNear = true;
  });
  if (enemyNear) OnFightSignal(nullptr);
}

// a load replaces the world: an old checkpoint must not be applied to it
static void WorldChanging() { DropCheckpoint(); g_cpTime = -1; g_inCombat = false; g_deathTime = -1; UiReset(); }

void __fastcall LoadSavegame_h(void*, void*, int, int);
auto Hook_LoadSavegame = HOOK(0x0063C2A0, &LoadSavegame_h);
void __fastcall LoadSavegame_h(void* self, void* edx, int slot, int globals) { WorldChanging(); Hook_LoadSavegame(self, edx, slot, globals); }

void __fastcall ChangeLevel_h(void*, void*, const zSTRING&, const zSTRING&);
auto Hook_ChangeLevel = HOOK(0x0063CD60, &ChangeLevel_h);
void __fastcall ChangeLevel_h(void* self, void* edx, const zSTRING& a, const zSTRING& b) { WorldChanging(); Hook_ChangeLevel(self, edx, a, b); }

} // namespace Gothic_I_Classic

BOOL WINAPI DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    if (GetGameVersion() != Engine_G1) return FALSE;
    if (!ZFSL_Active()) return TRUE;
  }
  return TRUE;
}
