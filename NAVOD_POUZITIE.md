# 🇸🇰 NÁVOD NA POUŽITIE - Hardvérová Kalibrácia

## 📋 Obsah
1. [Hardvérové Pripojenie](#1-hardvérové-pripojenie)
2. [Flashnutie Firmware](#2-flashnutie-firmware)
3. [Kalibrácia Žalúzie](#3-kalibrácia-žalúzie)
4. [Ovládanie cez Matter](#4-ovládanie-cez-matter)
5. [Riešenie Problémov](#5-riešenie-problémov)

---

## 1. 📌 Hardvérové Pripojenie

### Potrebné komponenty:
- **ESP32-C3** (už máš)
- **3x tlačidlá** (push button) - normálne otvorené (NO)
- **1x LED** (s rezistorom 220-330Ω)
- **Stepper motor driver** (už pripojený)

### Schéma zapojenia:

```
ESP32-C3 Pinout:

┌─────────────────────────────────┐
│         ESP32-C3                │
│                                 │
│  GPIO 0  ──────┐                │  ← Tlačidlo UP
│                 ├──[BTN UP]── GND
│  GPIO 1  ──────┐                │  ← Tlačidlo STOP  
│                 ├──[BTN STOP]── GND
│  GPIO 3  ──────┐                │  ← Tlačidlo DOWN
│                 ├──[BTN DOWN]── GND
│                                 │
│  GPIO 7  ──[220Ω]──[LED]── GND  │  ← LED indikátor
│                                 │
└─────────────────────────────────┘
```

### Detailné pripojenie:

**Tlačidlá (všetky rovnako):**
```
ESP32 GPIO Pin ──────┬──────┐
                     │      │
                   [BTN]    │
                     │      │
                    GND ────┘
```
- Tlačidlá sú aktívne LOW (stlačené = 0V)
- ESP32 má interné PULL-UP rezistory (už nakonfigurované v kóde)
- **Nepotrebuješ externé rezistory!**

**LED:**
```
GPIO 7 ──[220Ω]──[LED+]───[LED-]── GND
                  ↑
            dlhšia nožička
```

### ⚠️ DÔLEŽITÉ:
- **GPIO 0** jeBootLoader pin - pri štarte nesmie byť tlačidlo stlačené!
- Všetky tlačidlá pripoj medzi GPIO pin a GND (nie VCC!)
- LED polarity: dlhšia nožička = + (k rezistoru)

---

## 2. 💾 Flashnutie Firmware

### Krok 1: Pripoj ESP32-C3 cez USB

```bash
# Skontroluj, či ESP32 je pripojené
ls /dev/tty.usbserial-* # alebo /dev/ttyUSB*
```

### Krok 2: Flash firmware

```bash
cd ~/esp-matter/examples/light_copy

# Aktivuj ESP-IDF a ESP-Matter
source ~/esp-idf/export.sh
source ~/esp-matter/export.sh

# Flash (automaticky nájde port)
idf.py flash monitor
```

**Čo očakávať:**
```
Connecting.......
Chip is ESP32-C3 (revision v0.4)
...
Writing at 0x00020000... (100%)
Hash of data verified.

Leaving...
Hard resetting via RTS pin...
```

### Krok 3: Monitor výstup

Po úspešnom flashi uvidíš:
```
I (xxx) DRIVER[MOTOR]: Motor: STEP=GPIOxx DIR=GPIOxx EN=GPIOxx
I (xxx) DRIVER[MOTOR]: Buttons: UP=GPIO0 STOP=GPIO1 DOWN=GPIO3
I (xxx) DRIVER[MOTOR]: LED: GPIO7
I (xxx) DRIVER[MOTOR]: Loaded max_steps=XXXX from NVS
I (xxx) DRIVER[MOTOR]: Calibration: Hold STOP for 2s to enter calibration mode
```

**Ukončiť monitor:** `CTRL+]`

---

## 3. 🎯 Kalibrácia Žalúzie

### Kedy kalibrovať?
- Pri prvom spustení (default: 5000 krokov)
- Po výmene motora
- Keď chceš zmeniť rozsah pohybu

### Kalibračný proces:

#### KROK 1️⃣: Vstup do kalibračného módu
```
▶ Drž tlačidlo STOP 2 sekundy
  
✓ LED začne POMALY blikať (500ms ON/OFF)
✓ V logoch uvidíš:
  "🔧 CALIBRATION MODE ENTERED"
  
⚠️ Matter príkazy sú teraz BLOKOVANÉ!
```

#### KROK 2️⃣: Nastavenie HOME pozície (vrch)
```
▶ Stlač tlačidlo UP
  → Motor ide HORE neobmedzene
  
▶ Keď dosiahneš vrch, stlač STOP
  
✓ LED rýchlo BLIKNE 2x
✓ V logoch:
  "🏠 HOME position set (0 steps, 0%)"
```

#### KROK 3️⃣: Nastavenie BOTTOM pozície (spodok)
```
▶ Stlač tlačidlo DOWN
  → Motor ide DOLE neobmedzene
  
▶ Keď dosiahneš spodok, stlač STOP
  
✓ LED rýchlo BLIKNE 3x
✓ V logoch:
  "🎯 BOTTOM position set (max_steps=XXXX, 100%)"
  "💾 Calibration saved to NVS"
  "📋 Press STOP twice to exit calibration"
```

#### KROK 4️⃣: Ukončenie kalibrácie
```
▶ Stlač STOP 2x rýchlo (do 1 sekundy)
  
✓ LED rýchlo BLIKNE 5x
✓ V logoch:
  "✅ CALIBRATION COMPLETE - Matter enabled"
  
✓ Kalibrácia uložená do NVS (prežije reboot!)
✓ Matter príkazy sú znovu AKTÍVNE
```

### LED Signály - Prehľad

| LED Vzor | Význam |
|----------|--------|
| Pomaly bliká (500ms) | Kalibračný mód aktívny |
| 2x rýchlo blikne | Home pozícia nastavená |
| 3x rýchlo blikne | Bottom pozícia nastavená |
| 5x rýchlo blikne | Kalibrácia úspešná |
| 10x veľmi rýchlo | CHYBA (napr. neplatná kalibrácia) |

### 🛡️ Bezpečnostné funkcie

**Emergency Stop:**
- Akékoľvek tlačidlo zastaví motor **OKAMŽITE**

**Timeout:**
- Kalibrácia sa automaticky ukončí po **5 minútach** nečinnosti
- Uvidíš: "⚠️ Calibration timeout - auto-exit"
- LED blikne 10x (error)

**Validácia:**
- Bottom pozícia musí byť min. **100 krokov** od home
- Ak nie, kalibrácia zlyhá a LED blikne 10x

---

## 4. 🌐 Ovládanie cez Matter

### Po kalibrácii:

Žalúzia je normálne ovládateľná cez Matter protokol:

**Cez iOS app (Home.app alebo tvoja app):**
```
0% = Hore (Home pozícia)
100% = Dole (Bottom pozícia)
```

**Cez chip-tool (testing):**
```bash
# Ísť hore (0%)
chip-tool windowcovering go-to-lift-percentage 0 <node-id> 1

# Ísť dole (100%)  
chip-tool windowcovering go-to-lift-percentage 10000 <node-id> 1

# Ísť na 50%
chip-tool windowcovering go-to-lift-percentage 5000 <node-id> 1

# Stop
chip-tool windowcovering stop-motion <node-id> 1
```

### Blokovanie Matter počas kalibrácie:

Ak sa pokúsiš ovládať cez Matter počas kalibrácie:
```
I (xxx) APP[WC]: Matter command blocked - calibration in progress
```
Príkaz sa ignoruje, aby sa neprekážalo v kalibrácii.

---

## 5. 🔧 Riešenie Problémov

### Problém: LED nebliká pri vstupe do kalibrácie

**Riešenie:**
1. Skontroluj zapojenie LED (+ cez rezistor k GPIO 7, - k GND)
2. Skontroluj polaritu LED
3. Skús iný rezistor (220-470Ω)
4. Pozri logy: `idf.py monitor`

### Problém: Tlačidlá nefungujú

**Riešenie:**
1. Skontroluj zapojenie (GPIO pin → tlačidlo → GND)
2. Uisti sa, že GPIO 0 NIE JE stlačené pri bootovaní
3. Testuj multimetrom: stlačené tlačidlo = 0V medzi GPIO a GND
4. Pozri logy - mali by sa zobrazovať stlačenia

### Problém: Kalibrácia sa neuloží

**Riešenie:**
1. Skontroluj logy pre chybové hlášky NVS
2. Uisti sa, že current_steps > 0 pred nastavením bottom
3. Vymaž NVS a skús znova:
   ```bash
   idf.py erase-flash
   idf.py flash
   ```

### Problém: Motor sa nestavuje na kalibračnej pozícii

**Riešenie:**
1. Počas kalibrácie motor ignoruje limity - to je správne!
2. Uisti sa, že si nastavil HOME skôr ako BOTTOM
3. Skontroluj, či je kalibračný mód aktívny (LED bliká pomaly)

### Problém: Device sa nespáruje s Matter

**Riešenie:**
1. Reset na factory settings:
   ```bash
   idf.py erase-flash
   idf.py flash monitor
   ```
2. Načítaj QR kód alebo zadaj pairing code z logov:
   ```
   I (xxx) APP[QR]: Setup Code: [20202021]
   I (xxx) APP[QR]: Discriminator: [3840]
   ```

### Debug logy:

**Zapni verbose logging:**
```bash
idf.py menuconfig
# Component config → ESP Matter → Log level → Debug
```

**Užitočné log tagy:**
```
DRIVER[MOTOR]  - motorové operácie
DRIVER[STATE]  - state machine kalibrácie
APP[WC]        - Matter window covering
```

---

## 📊 Príklad kompletného použitia:

```bash
# 1. Flash firmware
cd ~/esp-matter/examples/light_copy
source ~/esp-idf/export.sh && source ~/esp-matter/export.sh
idf.py flash monitor

# 2. Po bootnutí:
#    - Skontroluj že vidíš: "Calibration: Hold STOP for 2s..."

# 3. Začni kalibráciu:
#    - Drž STOP 2s → LED pomaly bliká

# 4. Nastav home:
#    - Stlač UP → motor hore
#    - Stlač STOP → LED 2x blikne

# 5. Nastav bottom:
#    - Stlač DOWN → motor dole  
#    - Stlač STOP → LED 3x blikne

# 6. Ukonči:
#    - STOP 2x rýchlo → LED 5x blikne

# 7. Testuj cez Matter:
#    chip-tool windowcovering go-to-lift-percentage 5000 <node-id> 1

# 8. Rebootni a skontroluj perzistenciu:
#    idf.py monitor
#    → Mali by si vidieť: "Loaded max_steps=XXXX from NVS"
```

---

## 🎓 Tipy & Triky

1. **Pred kalibráciou** uisti sa, že žalúzia je voľne pohyblivá
2. **Pri nastavovaní home/bottom** zober margin (1-2 cm od krajov) aby motor nepracoval na limite
3. **Po kalibrácii** otestuj plný rozsah pohybu cez Matter
4. **Kalibračné dáta** sú uložené v NVS a prežijú reboot i firmware update
5. **Emergency stop** funguje vždy - ak niečo nejde, stlač ľubovoľné tlačidlo

---

## 📞 Support

Ak máš problémy:
1. Skontroluj logy: `idf.py monitor`
2. Pozri [RIEŠENIE PROBLÉMOV](#5-riešenie-problémov)
3. Skontroluj zapojenie podľa [SCHÉMY](#1-hardvérové-pripojenie)

**Dokumenty:**
- `CALIBRATION_GUIDE.md` - pôvodný anglický návod s Matter detailmi
- `QUICK_REFERENCE.md` - rýchla referencia

---

Užívaj si automatizovanú žalúziu! 🎉
