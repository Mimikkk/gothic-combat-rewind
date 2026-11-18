# zCombatRewind

Plugin [Union](https://gitlab.com/union-framework/union-api) dla **Gothic I** (1.08k_mod / Steam). Pozwala cofnąć się do stanu tuż sprzed walki, bez wczytywania całej gry: przywraca HP, potwory i przedmioty, a reszta świata zostaje nietknięta.

> Status: **etap 3 z 6**: gracz i NPC w promieniu (domyślnie 35 m), punkt kontrolny i cofnięcie na klawisze. Eksperymentalne.

## Sterowanie (etap 2)

| Klawisz | Działanie |
|---|---|
| **F7** | zapisz punkt kontrolny (gracz) |
| **F8** | cofnij do punktu kontrolnego |

Klawisze można zmienić w `System\GOTHIC.INI`, w sekcji `[ZCOMBATREWIND]`: `KeyCheckpoint` i `KeyRewind` przyjmują kody DirectInput (F7 = 65, F8 = 66). Po każdym cofnięciu `System\zCombatRewind.log` pokazuje, co zostało zmienione.

Wczytanie zapisu albo zmiana poziomu kasuje punkt kontrolny.

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
