#pragma once
#include <string>

namespace Gothic_I_Classic {
  enum class ToastKind { Checkpoint, Rewind, Info };

  struct UiSettings {
    bool indicator = true, hints = true, sounds = true, flash = true;
    std::string soundCheckpoint = "MFX_LIGHT_CAST", soundRewind = "MFX_TELEPORT_CAST";
    std::string keyCheckpoint = "F7", keyRewind = "F8";
  };

  void UiConfigure(const UiSettings& s);
  void UiToast(ToastKind kind, const std::string& title, const std::string& subtitle);   // UTF-8 text
  void UiRewindFlash();
  void UiSound(bool rewind);
  // called every frame after the world was rendered
  //   checkpointAgeMs < 0: no checkpoint; deadForMs < 0: player alive
  void UiFrame(long long checkpointAgeMs, bool inCombat, long long deadForMs);
  void UiReset();   // drop views (world change)
}
