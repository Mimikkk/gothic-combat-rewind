// Snapshot / restore of a single NPC by values (no object recreation, identity is kept).
// Inventory changes go through the same engine calls the scripts use:
//   CreateInvItems     -> oCNpc::CreateItems(instance, amount)
//   Npc_RemoveInvItems -> oCNpc::RemoveFromInv(instance, amount)

#include <Windows.h>
#include <ZenGin/zGothicAPI.h>
#include <map>
#include <cstring>
#include <cstdio>
#include "Snapshot.h"

namespace Gothic_I_Classic {

static const char* kAttrNames[8] = { "HP", "HP_MAX", "MANA", "MANA_MAX", "STR", "DEX", "REGEN_HP", "REGEN_MANA" };

static void ForEachItem(oCNpc* npc, void (*fn)(oCItem*, void*), void* ctx) {
  oCNpcInventory& inv = npc->inventory2;
  for (int cat = 0; cat < INV_MAX; cat++) {
    inv.UnpackCategory(cat);
    int n = inv.GetNumItemsInCategory(cat);
    for (int i = 0; i < n; i++)
      if (oCItem* it = inv.GetItem(cat, i)) fn(it, ctx);
  }
}

void TakeNpcSnap(oCNpc* npc, NpcSnap& s) {
  s = NpcSnap{};
  memcpy(s.trafo, &npc->trafoObjToWorld, sizeof(s.trafo));
  memcpy(s.attribute, npc->attribute, sizeof(s.attribute));
  memcpy(s.protection, npc->protection, sizeof(s.protection));
  s.level = npc->level;
  s.xp = npc->experience_points;
  s.xpNext = npc->experience_points_next_level;
  s.lp = npc->learn_points;
  memcpy(s.aivars, npc->aiscriptvars, sizeof(s.aivars));
  s.weaponMode = npc->GetWeaponMode();
  ForEachItem(npc, [](oCItem* it, void* ctx) {
    ((NpcSnap*)ctx)->inv.push_back({ it->instanz, it->amount, it->HasFlag(ITM_FLAG_ACTIVE) != 0 });
  }, &s);
  s.valid = true;
}

struct InvSum { int amount = 0; bool equipped = false; };

static std::map<int, InvSum> Summarize(const std::vector<InvEntry>& v) {
  std::map<int, InvSum> m;
  for (auto& e : v) { auto& s = m[e.inst]; s.amount += e.amount; s.equipped |= e.equipped; }
  return m;
}

static std::string InstName(int inst) {
  zSTRING n = oCNpc::GetInstanceByID(inst);   // works for any parser symbol index
  return n.ToChar() ? n.ToChar() : "?";
}

void RestoreNpcSnap(oCNpc* npc, const NpcSnap& s, std::string& report) {
  char buf[256];
  if (!s.valid) { report += "no snapshot\n"; return; }
  bool wasDead = npc->IsDead() != 0;

  // stop whatever is going on (pending AI messages, burning, weapon in hand)
  npc->ClearEM();
  npc->StopBurn();

  // attributes, protection, progression, script vars
  for (int i = 0; i < 8; i++)
    if (npc->attribute[i] != s.attribute[i]) {
      sprintf(buf, "  %s %d -> %d\n", kAttrNames[i], npc->attribute[i], s.attribute[i]); report += buf;
    }
  memcpy(npc->attribute, s.attribute, sizeof(s.attribute));
  memcpy(npc->protection, s.protection, sizeof(s.protection));
  if (npc->experience_points != s.xp || npc->level != s.level) {
    sprintf(buf, "  XP %lu -> %lu, level %d -> %d, LP %lu -> %lu\n", npc->experience_points, s.xp, npc->level, s.level, npc->learn_points, s.lp);
    report += buf;
  }
  npc->level = s.level;
  npc->experience_points = s.xp;
  npc->experience_points_next_level = s.xpNext;
  npc->learn_points = s.lp;
  memcpy(npc->aiscriptvars, s.aivars, sizeof(s.aivars));

  // inventory: per instance difference, then equipment state
  std::vector<InvEntry> cur;
  ForEachItem(npc, [](oCItem* it, void* ctx) {
    ((std::vector<InvEntry>*)ctx)->push_back({ it->instanz, it->amount, it->HasFlag(ITM_FLAG_ACTIVE) != 0 });
  }, &cur);
  auto want = Summarize(s.inv), have = Summarize(cur);
  for (auto& kv : have) if (!want.count(kv.first)) want[kv.first] = InvSum{};
  for (auto& kv : want) {
    int diff = kv.second.amount - have[kv.first].amount;
    if (!diff) continue;
    sprintf(buf, "  item %s %+d\n", InstName(kv.first).c_str(), diff); report += buf;
    if (diff < 0) npc->RemoveFromInv(kv.first, -diff);
    else npc->CreateItems(kv.first, diff);
  }
  struct Ctx { oCNpc* npc; std::map<int, InvSum>* want; std::string* report; };
  Ctx ctx{ npc, &want, &report };
  ForEachItem(npc, [](oCItem* it, void* p) {
    Ctx& c = *(Ctx*)p;
    bool should = (*c.want)[it->instanz].equipped, is = it->HasFlag(ITM_FLAG_ACTIVE) != 0;
    if (should == is) return;
    *c.report += std::string("  ") + (should ? "equip " : "unequip ") + InstName(it->instanz) + "\n";
    if (should) c.npc->EquipItem(it); else c.npc->UnequipItem(it);
  }, &ctx);

  // weapon mode
  if (npc->GetWeaponMode() != s.weaponMode) {
    sprintf(buf, "  weapon mode %d -> %d\n", npc->GetWeaponMode(), s.weaponMode); report += buf;
    npc->SetWeaponMode(s.weaponMode);
  }

  // position and orientation (collision off while teleporting)
  zMAT4 m; memcpy(&m, s.trafo, sizeof(s.trafo));
  int cd = npc->collDetectionStatic, cs = npc->collDetectionDynamic;
  npc->SetCollDetStat(FALSE); npc->SetCollDetDyn(FALSE);
  npc->SetTrafoObjToWorld(m);
  npc->SetCollDetStat(cd); npc->SetCollDetDyn(cs);

  // experimental: bring a dead NPC back (HP is already > 0 from the attributes above)
  if (wasDead && !npc->IsDead()) {
    report += "  revive (experimental)\n";
    npc->state.ClearAIState();
    npc->SetBodyState(0);   // BS_STAND
    if (npc == oCNpc::player && npc->human_ai) npc->human_ai->SetCamMode("CAMMODNORMAL", 0);
    if (npc->anictrl) npc->anictrl->StartStandAni();
  }
}

} // namespace Gothic_I_Classic
