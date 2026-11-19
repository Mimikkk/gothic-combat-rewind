// On-screen presentation: toasts, checkpoint indicator, rewind flash, death prompt, sounds.
// Only engine resources are used: FONT_OLD_20_WHITE / FONT_OLD_10_WHITE, WHITE.TGA,
// DLG_CONVERSATION.TGA (dialog box background) and SFX instances from the game scripts.

#include <Windows.h>
#include <ZenGin/zGothicAPI.h>
#include <string>
#include "Ui.h"

namespace Gothic_I_Classic {

static UiSettings g_set;
void UiConfigure(const UiSettings& s) { g_set = s; }

static long long NowMs() { return (long long)GetTickCount64(); }

// texts are written in UTF-8; the Polish game fonts use code page 1250
static zSTRING Z(const std::string& utf8) {
  int wn = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
  std::wstring w(wn, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wn);
  int an = WideCharToMultiByte(1250, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
  std::string a(an, '\0');
  WideCharToMultiByte(1250, 0, w.c_str(), -1, &a[0], an, nullptr, nullptr);
  return zSTRING(a.c_str());
}

static std::string Clock(long long ms) {
  long long s = ms / 1000;
  char buf[32];
  if (s >= 3600) sprintf(buf, "%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
  else sprintf(buf, "%lld:%02lld", s / 60, s % 60);
  return buf;
}

// ---------------------------------------------------------------- views
static zCView* g_text = nullptr;    // full-screen layer for all texts
static zCView* g_panel = nullptr;   // toast background
static zCView* g_flash = nullptr;   // rewind flash
static zCView* g_dim = nullptr;     // death dimming
static bool g_panelIn = false, g_flashIn = false, g_dimIn = false;

static zCView* MakeLayer(const char* tex, int x0, int y0, int x1, int y1) {
  zCView* v = new zCView(x0, y0, x1, y1);
  if (tex) v->InsertBack(zSTRING(tex));
  v->SetAlphaBlendFunc(zRND_ALPHA_FUNC_BLEND);
  v->SetTransparency(0);
  return v;
}

static bool EnsureViews() {
  if (!screen) return false;
  if (!g_text) {
    g_text = MakeLayer(nullptr, 0, 0, 8192, 8192);
    screen->InsertItem(g_text);
    g_flash = MakeLayer("WHITE.TGA", 0, 0, 8192, 8192);
    g_flash->SetColor(zCOLOR(170, 200, 255, 255));
    g_dim = MakeLayer("WHITE.TGA", 0, 0, 8192, 8192);
    g_dim->SetColor(zCOLOR(0, 0, 0, 255));
  }
  return true;
}

static void Show(zCView* v, bool& in, bool want) {
  if (want == in || !screen) return;
  if (want) screen->InsertItem(v); else screen->RemoveItem(v);
  in = want;
  // keep the text layer on top
  if (want && g_text) { screen->RemoveItem(g_text); screen->InsertItem(g_text); }
}

void UiReset() {
  if (screen) {
    if (g_panelIn) screen->RemoveItem(g_panel);
    if (g_flashIn) screen->RemoveItem(g_flash);
    if (g_dimIn) screen->RemoveItem(g_dim);
    if (g_text) screen->RemoveItem(g_text);
  }
  g_panelIn = g_flashIn = g_dimIn = false;
  g_text = g_panel = g_flash = g_dim = nullptr;   // the engine frees views with the screen on world change
}

// ---------------------------------------------------------------- toast
struct Toast { std::string title, sub; ToastKind kind; long long start = -1; };
static Toast g_toast;
static const long long kIn = 150, kHold = 2600, kOut = 700;

void UiToast(ToastKind kind, const std::string& title, const std::string& sub) {
  g_toast.title = title; g_toast.sub = sub; g_toast.kind = kind; g_toast.start = NowMs();
}

static long long g_flashStart = -1;
void UiRewindFlash() { if (g_set.flash) g_flashStart = NowMs(); }

void UiSound(bool rewind) {
  if (!g_set.sounds || !zsound) return;
  const std::string& name = rewind ? g_set.soundRewind : g_set.soundCheckpoint;
  if (name.empty()) return;
  if (zCSoundFX* sfx = zsound->LoadSoundFXScript(zSTRING(name.c_str()))) {
    zsound->PlaySound(sfx, 0);
    sfx->Release();
  }
}

static void PrintCentered(int y, const char* font, const zCOLOR& c, const std::string& text) {
  g_text->SetFont(zSTRING(font));
  g_text->SetFontColor(c);
  g_text->PrintCX(y, Z(text));
}

static int Fade(long long t, long long in, long long hold, long long out) {   // 0..255
  if (t < 0) return 0;
  if (t < in) return (int)(255 * t / in);
  if (t < in + hold) return 255;
  if (t < in + hold + out) return (int)(255 * (in + hold + out - t) / out);
  return 0;
}

// ---------------------------------------------------------------- per frame
static bool g_hintShown = false;

void UiFrame(long long cpAge, bool inCombat, long long deadFor) {
  if (!EnsureViews()) return;
  long long now = NowMs();
  g_text->ClrPrintwin();

  if (!g_hintShown && g_set.hints) {
    g_hintShown = true;
    UiToast(ToastKind::Info, "Cofanie walki",
            g_set.keyCheckpoint + " — punkt kontrolny   ·   " + g_set.keyRewind + " — cofnij   ·   automatycznie przed walką");
  }

  // death prompt
  bool deathPrompt = deadFor >= 1200;
  int dimA = deathPrompt ? (int)min(130LL, (deadFor - 1200) / 4) : 0;
  Show(g_dim, g_dimIn, dimA > 0);
  if (dimA > 0) g_dim->SetTransparency(dimA);
  if (deathPrompt) {
    int a = (int)min(255LL, (deadFor - 1200) / 2);
    PrintCentered(3300, "FONT_OLD_20_WHITE.TGA", zCOLOR(225, 90, 70, (unsigned char)a), "Poległeś");
    if (cpAge >= 0) {
      PrintCentered(3900, "FONT_OLD_10_WHITE.TGA", zCOLOR(235, 220, 170, (unsigned char)a),
                    g_set.keyRewind + " — cofnij do punktu kontrolnego (sprzed " + Clock(cpAge) + ")");
      PrintCentered(4250, "FONT_OLD_10_WHITE.TGA", zCOLOR(190, 190, 190, (unsigned char)a), "ESC — menu gry");
    } else {
      PrintCentered(3900, "FONT_OLD_10_WHITE.TGA", zCOLOR(190, 190, 190, (unsigned char)a), "Brak punktu kontrolnego — ESC, aby wczytać grę");
    }
  }

  // toast: top of the screen, text only
  int ta = g_toast.start >= 0 ? Fade(now - g_toast.start, kIn, kHold, kOut) : 0;
  if (ta > 0 && !deathPrompt) {
    zCOLOR title = g_toast.kind == ToastKind::Rewind ? zCOLOR(170, 205, 255, (unsigned char)ta)
                 : g_toast.kind == ToastKind::Checkpoint ? zCOLOR(240, 205, 120, (unsigned char)ta)
                 : zCOLOR(230, 230, 230, (unsigned char)ta);
    PrintCentered(600, "FONT_OLD_20_WHITE.TGA", title, g_toast.title);
    if (!g_toast.sub.empty())
      PrintCentered(980, "FONT_OLD_10_WHITE.TGA", zCOLOR(215, 215, 215, (unsigned char)ta), g_toast.sub);
  }

  // rewind flash
  int fa = g_flashStart >= 0 ? Fade(now - g_flashStart, 0, 60, 650) * 190 / 255 : 0;
  Show(g_flash, g_flashIn, fa > 0);
  if (fa > 0) g_flash->SetTransparency(fa);

  // indicator above the health bar
  if (g_set.indicator && cpAge >= 0 && !deathPrompt) {
    g_text->SetFont(zSTRING("FONT_OLD_10_WHITE.TGA"));
    std::string t = inCombat ? "W walce  ·  punkt " + Clock(cpAge) : "Punkt kontrolny  ·  " + Clock(cpAge);
    g_text->SetFontColor(inCombat ? zCOLOR(240, 205, 120, 220) : zCOLOR(200, 200, 200, 140));
    g_text->Print(130, 7250, Z(t));
  }
}

} // namespace Gothic_I_Classic
