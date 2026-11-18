#pragma once
#include <vector>
#include <string>

namespace Gothic_I_Classic {
  class oCNpc;

  struct InvEntry { int inst; int amount; bool equipped; };

  // Stage 2: what we keep of an NPC (used for the player for now)
  struct NpcSnap {
    bool valid = false;
    float trafo[16] = {};
    int attribute[8] = {}, protection[8] = {};
    int level = 0;
    unsigned long xp = 0, xpNext = 0, lp = 0;
    int aivars[50] = {};
    int weaponMode = 0;
    std::vector<InvEntry> inv;
  };

  void TakeNpcSnap(oCNpc* npc, NpcSnap& s);
  // Applies the snapshot; writes what had to change into 'report'
  void RestoreNpcSnap(oCNpc* npc, const NpcSnap& s, std::string& report);
}
