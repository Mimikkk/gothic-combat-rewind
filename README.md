# zCombatRewind

Plugin [Union](https://gitlab.com/union-framework/union-api) dla **Gothic I** (1.08k_mod / Steam). Pozwala cofnąć się do stanu tuż sprzed walki, bez wczytywania całej gry: przywraca HP, potwory i przedmioty, a reszta świata zostaje nietknięta.

> Status: gracz, NPC i przedmioty w promieniu, automatyczny punkt kontrolny przed walką, interfejs. Eksperymentalne.

## Jak to działa w grze

- **Automatycznie:** punkt kontrolny powstaje sam, gdy wyjmiesz broń przy wrogu albo gdy wróg weźmie Cię na cel. W trakcie walki nie jest nadpisywany. Przeciwnicy, którzy dołączą później, są dopisywani do punktu w chwili dołączenia. Walka kończy się dopiero po 10 s spokoju: nikt w promieniu 100 m nie celuje w gracza (także w pościgu) i gracz nie traci HP. Automatyczny punkt powstaje tylko na początku walki: gdy nikt inny jeszcze nie celuje w gracza, gracz ma co najmniej 60% HP i nie dostał obrażeń przez ostatnie 15 s. W przeciwnym razie zostaje poprzedni punkt.
- **F7:** punkt kontrolny ręcznie.
- **F8:** cofnięcie, także po śmierci.

| Sytuacja | Na ekranie |
|---|---|
| Punkt kontrolny | Komunikat u góry ekranu („Punkt kontrolny” / „Punkt kontrolny przed walką”, „F8 — cofnij do tej chwili”) i cichy dźwięk |
| Cofnięcie | Niebieskawy błysk, dźwięk teleportu, „Czas cofnięty — Przywrócono 3 postacie, 2 przedmioty” |
| Stały wskaźnik | Nieco nad paskiem zdrowia: „Punkt kontrolny · 0:42”, a w walce złote „W walce · punkt 0:42” |
| Śmierć | Ekran ciemnieje: „Poległeś”, „F8 — cofnij do punktu kontrolnego (sprzed 0:42)”, „ESC — menu gry” |
| Start gry | Jednorazowa podpowiedź z klawiszami |

Wczytanie zapisu albo zmiana poziomu kasuje punkt kontrolny. Szczegóły każdego cofnięcia trafiają do `System\zCombatRewind.log`.

## Ustawienia (`System\GOTHIC.INI`, sekcja `[ZCOMBATREWIND]`)

```ini
[ZCOMBATREWIND]
KeyCheckpoint=65      ; kod DirectInput (65 = F7)
KeyRewind=66          ; 66 = F8
Radius=3500           ; promień w cm wokół gracza
AutoCheckpoint=1      ; automatyczny punkt kontrolny przed walką
AutoMinHealthPercent=60 ; bez automatycznego punktu poniżej tego % zdrowia
CombatCalmSeconds=10  ; tyle sekund spokoju kończy walkę
ChaseRange=10000      ; wróg celujący w gracza liczy się do tej odległości (cm), także w pościgu
ShowIndicator=1       ; wskaźnik nad paskiem zdrowia
ShowHints=1           ; podpowiedź przy starcie
Sounds=1              ; dźwięki (MFX_LIGHT_CAST, MFX_TELEPORT_CAST)
RewindFlash=1         ; błysk przy cofnięciu
```

## Co jest przywracane (gracz)

- pozycja i obrót;
- atrybuty: HP, mana, siła, zręczność i ich maksima;
- ochrona;
- poziom, doświadczenie, punkty nauki;
- zmienne AI skryptu (`aivar`);
- ekwipunek: ilość każdego przedmiotu przez te same wywołania co skrypty (`CreateInvItems` → `oCNpc::CreateItems`, `Npc_RemoveInvItems` → `oCNpc::RemoveFromInv`), plus założone rzeczy;
- tryb broni (dobyta lub schowana);
- przerwanie bieżących akcji (`ClearEM`), zgaszenie płonięcia;
- *eksperymentalnie:* wskrzeszenie po śmierci (wyjście ze stanu AI, stan ciała, kamera, animacja stania).

## Ustalenia z silnika (etap 1)

- Martwy = `attribute[HP] <= 0` (`oCNpc::IsDead`). `DoDie` przerywa akcje, wyrzuca broń z rąk, gasi płonięcie, ustawia instancje `self` i `other`, uruchamia stan AI śmierci (`ZS_Dead`, w nim m.in. przyznanie XP), wysyła percepcję „morderstwo”, a dla gracza włącza kamerę śmierci. Gothic 1 nie ma osobnego ekranu końca gry.
- `oCSpawnManager::CheckRemoveNpc` usuwa ciała martwych NPC z dala od gracza. Cofanie musi więc obsłużyć też NPC, którego już nie ma, przez wstawienie go od nowa z instancji.
- Klocki do kolejnych etapów: `zCArchiverFactory::CreateArchiverWrite/Read(zCBuffer*)` (zapis obiektów do pamięci), `zCParser::SaveGlobalVars/LoadGlobalVars` (zmienne skryptów), `oCNpc::SetEnemy` / `SetWeaponMode` / `OnDamage` (wykrycie początku walki), `oCNpc_States::ClearAIState` / `StartRtnState` (powrót do planu dnia).

## Plan

1. ~~Rozpoznanie w silniku~~
2. Gracz: punkt kontrolny i cofnięcie na klawisze (**teraz**)
3. NPC w promieniu: pozycja, atrybuty, ekwipunek, wskrzeszenie, reset AI i wrogości
4. Przedmioty na ziemi, zmienne skryptów, czas gry, usuwanie przyzwanych stworów i pocisków
5. Automatyczny punkt kontrolny na początku walki i okno wyboru po śmierci
6. Test zgodności z pełnym zapisem

## Budowanie i praca

Tak samo jak w zFastSaveLoad: `tools\dev.ps1` (build i wgranie `System\Autorun\zCombatRewind_dev.dll`), `tools\dis.ps1` (disassembler `GothicMod.exe`), `tools\dumpstack.py` (stos ze zrzutu crasha).

## Steam Workshop

Prywatny (ukryty) element Workshopu, ID `3842553042`. Nowe wersje wysyła `tools\release.ps1 "opis zmian"`. Po wysłaniu Steam czasem nie pobiera aktualizacji sam: wtedy trzeba anulować subskrypcję i zasubskrybować ponownie.
