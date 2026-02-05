# ⚡ RÝCHLY NÁVOD - Kalibrácia Žalúzie

## 🎯 IMPLEMENTOVANÉ ✅

**Build:** 24.01.2026 21:17  
**Binary:** 1.5 MB  
**Status:** Pripravené s NVS validáciou

---

## ⚠️ DÔLEŽITÉ ZMENY

### NVS Validácia
- **Home** sa ukladá vždy ako **0** (absolútna referencia)
- **Bottom** sa ukladá ako **počet krokov od home**
- Automatická detekcia neplatných hodnôt
- Automatický reset na defaulty pri chybe

### Pri prvom spustení:
Ak máš zlé hodnoty v NVS (ako `home=62046, bottom=65535`), firmware ich automaticky **vymaže a nastaví defaulty**:
- `home = 0`
- `bottom = 5000` (default max_steps)

---

## 📍 Pinout

| Pin | Funkcia | Typ |
|-----|---------|-----|
| **GPIO 0** | Tlačidlo UP | Vstup, Active LOW, Pull-up |
| **GPIO 1** | Tlačidlo STOP | Vstup, Active LOW, Pull-up |
| **GPIO 3** | Tlačidlo DOWN | Vstup, Active LOW, Pull-up |
| **GPIO 7** | LED indikátor | Výstup |

---

## 🔄 KALIBRAČNÝ PROCES

### 1️⃣ VSTUP DO KALIBRÁCIE
```
Akcia:  Drž tlačidlo STOP 2 sekundy
LED:    Svieti TRVALO (continuous ON)
Stav:   Matter príkazy BLOKOVANÉ
```

### 2️⃣ NASTAVENIE HOME (Horná poloha)
```
Akcia:  Stlač UP → Motor ide hore
        Stlač STOP keď je na vrchu
LED:    2x rýchle bliknutie (200ms)
Uložené: current_steps = 0, pozícia v NVS
```

### 3️⃣ NASTAVENIE BOTTOM (Dolná poloha)
```
Akcia:  Stlač DOWN → Motor ide dole  
        Stlač STOP keď je na dole
LED:    3x rýchle bliknutie (200ms)
Uložené: bottom_steps = current_steps, uložené v NVS
Validácia: Musí byť min. 100 krokov rozdiel
```

### 4️⃣ UKONČENIE KALIBRÁCIE
```
Akcia:  Stlač STOP 2x rýchlo (do 1 sekundy)
LED:    5x bliknutie (150ms) = úspech!
Stav:   Matter príkazy znovu AKTÍVNE
```

---

## 🔴 CHYBOVÉ STAVY

### ❌ Príliš krátka vzdialenosť
```
Problém: bottom_steps < 100
LED:     10x rýchle bliknutie (100ms)
Riešenie: Vráti sa do stavu HOME_SET, skús znova DOWN
```

### ⏱️ Timeout (5 minút nečinnosti)
```
Problém: Žiadna aktivita 5 minút
LED:     10x rýchle bliknutie (100ms)
Riešenie: Automatický exit, kalibrácia neuložená
```

---

## 📊 STAVOVÁ MAŠINA

```mermaid
IDLE (Normálna prevádzka)
  ↓ [Hold STOP 2s]
READY (LED svieti trvalo)
  ↓ [Press UP]
MOVING_TO_HOME (Motor hore)
  ↓ [Press STOP → LED 2x]
HOME_SET (Home uložené)
  ↓ [Press DOWN]
MOVING_TO_BOTTOM (Motor dole)
  ↓ [Press STOP → LED 3x]
COMPLETE (Bottom uložené)
  ↓ [Press STOP 2x → LED 5x]
IDLE (Späť do normálu)
```

---

## 🛡️ BEZPEČNOSTNÉ FUNKCIE

✅ **Debouncing:** 50ms na každé tlačidlo  
✅ **Hold detekcia:** 2 sekundy pre STOP  
✅ **Double-press:** 1 sekunda medzi stlačeniami  
✅ **Timeout:** 5 minút automatický exit  
✅ **Validácia:** Min. 100 krokov  
✅ **Matter blokovanie:** Žiadne príkazy počas kalibrácie  

---

## 💾 NVS Úložisko

**Namespace:** `"calibration"`  
**Kľúče:**
- `"home_steps"` → uint16_t (default: 0)
- `"bottom_steps"` → uint16_t (default: 5000)

**Automatické ukladanie:**
- Po nastavení HOME
- Po nastavení BOTTOM
- Prečítanie pri boot

---

## 🖥️ Flash & Monitor príkazy

```bash
cd ~/esp-matter/examples/light_copy

# Flash
idf.py -p /dev/tty.usbserial-XXXX flash

# Monitor
idf.py -p /dev/tty.usbserial-XXXX monitor

# Flash + Monitor
idf.py -p /dev/tty.usbserial-XXXX flash monitor
```

**Ctrl+]** = exit monitor

---

## 🔍 Očakávané logy

### Pri boot:
```
[MOTOR] 🎛️  Calibration HW: UP=GPIO0 STOP=GPIO1 DOWN=GPIO3 LED=GPIO7
[MOTOR] 📂 Loaded calibration: home=0, bottom=5000
[STATE] 🎮 Calibration button task started
```

### Pri kalibrácii:
```
[STATE] 🔧 ENTERING CALIBRATION MODE
[STATE] ⬆️  Starting move to HOME position
[STATE] ✅ HOME position set!
[STATE] 💾 Calibration saved to NVS
[STATE] ⬇️  Starting move to BOTTOM position
[STATE] ✅ BOTTOM position set! Travel: 3456 steps
[STATE] 💾 Calibration saved to NVS
[STATE] 🏁 CALIBRATION COMPLETE - Exiting
```

### Pri Matter príkaze počas kalibrácie:
```
[STATE] ⚠️  Matter command BLOCKED - calibration in progress
```

---

## 🧪 TEST CHECKLIST

- [ ] LED je ON pri vstupe do kalibrácie
- [ ] UP tlačidlo pohybuje motor hore
- [ ] STOP tlačidlo zastaví motor
- [ ] LED blikne 2x po HOME
- [ ] DOWN tlačidlo pohybuje motor dole
- [ ] LED blikne 3x po BOTTOM
- [ ] Double STOP ukončí kalibráciu (LED 5x)
- [ ] Matter príkazy blokované počas kalibrácie
- [ ] Matter príkazy fungujú po kalibrácii
- [ ] Reboot → kalibrácia persistuje z NVS

---

## 📞 Technické detaily

**Compiler:** GCC 14.2.0 riscv32-esp-elf  
**IDF Version:** 5.4.1  
**Matter SDK:** ESP-Matter v1.3  
**Workaround:** `-Wno-error` flag (GCC 14.2 compatibility)

**Tasky:**
- `calib_led` (priority 1, stack 3072)
- `calib_btn` (priority 3, stack 4096)
- `wc_stepper` (priority 2, stack 4096)
- `wc_update` (priority 1, stack 4096)

---

✅ **READY FOR HARDWARE TESTING!**
