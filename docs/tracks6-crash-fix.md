# T-Racks 6 Crash – Diagnózis és javítás

**Branch:** `claude/fix-tracks6-crash-cCZjI`  
**Érintett fájl:** `src/ownvst3.cpp`  

---

## Mi volt a master branch állapota az elemzés előtt

A master branch már tartalmazta az alábbi javításokat (T-Racks 5-tel kapcsolatos korábbi hibák):

| Meglévő fix | Leírás |
|-------------|--------|
| `IComponentHandler2` implementálva | `OwnComponentHandler` mindkét interfészt valósítja meg |
| Bus-számok member változóban | `numInputBuses`/`numOutputBuses` tárolva `setupProcessing()`-ből |
| `vector<AudioBusBuffers>` | `processAudio()` valós bus-számnak megfelelő tömböt használ |
| `IProcessContextRequirements` lekérdezés | `setupProcessing()` lekérdezi, de végül mindig teli `ProcessContext`-et ad |
| `kParamValuesChanged` deferred kezelés | `pendingParamRefresh` flag → `processIdle()` → `updateParameters()` |
| `performEdit()` implementálva | Plugin UI-ból érkező paraméterváltozások visszakerülnek a hostba |

**A T-Racks 6 crash egyetlen fennmaradó oka:** a `kIoChanged` és `kReloadComponent` flagek nem voltak kezelve.

---

## A crash mechanizmusa

### Mi történik T-Racks 6-ban, ami T-Racks 5-ben nem

A T-Racks 6 **moduláris lánc-architektúrával** rendelkezik: a felhasználó futás közben adhat hozzá/vehet el modulokat (EQ, kompresszor, limiter stb.). Minden ilyen változáskor a plugin meghívja:

```
IComponentHandler::restartComponent(kIoChanged)       // bus-layout változott
IComponentHandler::restartComponent(kReloadComponent) // teljes újrainicializálás
```

Ezeket a hívásokat a plugin **belső háttérszáláról** indítja, nem a főszálról.

### Régi viselkedés (hibás)

```cpp
Steinberg::tresult PLUGIN_API OwnComponentHandler::restartComponent(Steinberg::int32 flags) {
    if (flags & Steinberg::Vst::kParamValuesChanged)
        scheduleParameterRefresh();
    if (flags & Steinberg::Vst::kIoChanged)
        std::cerr << "kIoChanged received (not handled)" << std::endl;  // csak log!
    if (flags & Steinberg::Vst::kReloadComponent)
        std::cerr << "kReloadComponent received (not handled)" << std::endl; // csak log!
    return Steinberg::kResultOk;
}
```

A T-Racks 6 visszatér `kResultOk`-t kap, azt hiszi a host elvégezte az újrainicializálást, majd folytatja a feldolgozást a **régi, érvénytelen bus-konfigurációval** → `process()` híváskor crash.

---

## A javítás

### Megközelítés: deferred végrehajtás `processIdle()`-ban

A közvetlen végrehajtás (azonnal `setActive(false)` + `setupProcessing()` a callback belsejéből) crashelne, mert:
- A callback érkezhet a plugin háttérszálán
- macOS-en AppKit objektumokat csak a főszálról szabad érinteni
- A JUCE modal loop alatt lévő objektumok nem biztonságosak más szálakból

A megoldás a már meglévő `pendingParamRefresh` mintát követi: atomikus flag, amit a `processIdle()` olvas ki a UI szálon.

### 1. változás – `pendingIoRestart` flag az `OwnComponentHandler`-ben

```cpp
// Előtte:
void scheduleParameterRefresh() { pendingParamRefresh.store(true, std::memory_order_release); }
std::atomic<bool> pendingParamRefresh{false};

// Utána:
void scheduleParameterRefresh() { pendingParamRefresh.store(true, std::memory_order_release); }
void scheduleIoRestart()        { pendingIoRestart.store(true,  std::memory_order_release); }  // ÚJ
std::atomic<bool> pendingParamRefresh{false};
std::atomic<bool> pendingIoRestart{false};  // ÚJ
```

**Érintett sorok:** `422–425` (`ownvst3.cpp`)

### 2. változás – `restartComponent()` implementáció

```cpp
// Előtte: kIoChanged/kReloadComponent csak logolt
Steinberg::tresult PLUGIN_API OwnComponentHandler::restartComponent(Steinberg::int32 flags) {
    if (flags & Steinberg::Vst::kParamValuesChanged)
        scheduleParameterRefresh();
    if (flags & Steinberg::Vst::kIoChanged)
        std::cerr << "kIoChanged received (not handled)" << std::endl;
    if (flags & Steinberg::Vst::kReloadComponent)
        std::cerr << "kReloadComponent received (not handled)" << std::endl;
    return Steinberg::kResultOk;
}

// Utána: kIoChanged/kReloadComponent deferred flag-et állít
Steinberg::tresult PLUGIN_API OwnComponentHandler::restartComponent(Steinberg::int32 flags) {
    if (flags & Steinberg::Vst::kParamValuesChanged)
        scheduleParameterRefresh();
    if (flags & (Steinberg::Vst::kIoChanged | Steinberg::Vst::kReloadComponent))
        scheduleIoRestart();  // ÚJ – deferred UI-szálon való végrehajtáshoz
    return Steinberg::kResultOk;
}
```

**Érintett sorok:** `1617–1625` (`ownvst3.cpp`)

### 3. változás – `processIdle()` kezeli a `pendingIoRestart` flaget

```cpp
// Előtte:
void processIdle() {
    if (componentHandler && componentHandler->pendingParamRefresh.exchange(
            false, std::memory_order_acq_rel)) {
        updateParameters();
    }
    // ...
}

// Utána:
void processIdle() {
    if (componentHandler) {
        // kIoChanged / kReloadComponent: re-negotiate bus layout and reactivate.
        if (componentHandler->pendingIoRestart.exchange(false, std::memory_order_acq_rel)) {
            if (isActive.load(std::memory_order_acquire) && component) {
                component->setActive(false);
                isActive.store(false, std::memory_order_release);
            }
            setupProcessing();   // bus újratárgyalás
            if (component && component->setActive(true) == kResultOk)
                isActive.store(true, std::memory_order_release);
            updateParameters();  // paraméter cache frissítés
        }
        // kParamValuesChanged: refresh parameter cache only.
        if (componentHandler->pendingParamRefresh.exchange(false, std::memory_order_acq_rel)) {
            updateParameters();
        }
    }
    // ... platform-specific idle (Linux/macOS)
}
```

**Érintett sorok:** `1383–1410` (`ownvst3.cpp`)

---

## Miért működött T-Racks 5-tel?

| Tulajdonság | T-Racks 5 | T-Racks 6 |
|-------------|-----------|----------|
| `restartComponent(kIoChanged)` | Nem hívja | Minden modul-betöltéskor hívja |
| `restartComponent(kReloadComponent)` | Ritkán/soha | Preset/lánc változáskor hívja |
| Bus-konfiguráció | Fix 1-in/1-out stereo | Dinamikusan változhat |
| Architektúra | Monolitikus effekt | Moduláris lánc |

---

## A változtatások összesítése

| # | Sor | Változás | Hatás |
|---|-----|----------|-------|
| 1 | `422–425` | `scheduleIoRestart()` + `pendingIoRestart` flag | Thread-safe jelzés a UI-szálnak |
| 2 | `1617–1625` | `restartComponent()` hívja `scheduleIoRestart()` | kIoChanged/kReloadComponent kezelve |
| 3 | `1383–1410` | `processIdle()` drains `pendingIoRestart` | Biztonságos UI-szálon való reinit |

**Nettó sorok:** +8 sor hozzáadva, 3 sor törölve (stderr logok)

---

## SDK verzió

Az SDK submodule verzió valószínűleg nem az elsődleges ok – az érintett interfészek (`IComponentHandler2`, `IProcessContextRequirements`) már VST3 3.7.0 (2020) óta elérhetők. Ha mégis szükséges frissítés:

```bash
cd sdk
git fetch origin
git checkout <legújabb tag>
cd ..
git add sdk
git commit -m "chore: update VST3 SDK"
```
