# WS2812 LED Signalizácia

## Hardware

- **GPIO pin:** GPIO 7
- **Radič:** WS2812 cez RMT (Remote Module) na ESP32
- **Jas pri štarte:** 96/255 (≈38%)
- **Obnovovací cyklus:** 50 ms

---

## Priorita stavov (od najvyššej)

1. Chybový stav
2. Kalibračný režim
3. Commissioning (párovanie)
4. Kritická batéria
5. Nízka batéria
6. Online
7. Offline (zariadenie beží)

---

## Stavy a farby

### Štart zariadenia

| Farba | RGB | Vzor | Perióda |
|-------|-----|------|---------|
| Oranžová | (255, 128, 0) | Blikanie | 600 ms |

---

### Sieťový stav

| Farba | RGB | Vzor | Perióda | Stav |
|-------|-----|------|---------|------|
| Zelená | (0, 255, 0) | Svietí | — | Zariadenie online a pripojené |
| Zelená | (0, 255, 0) | Pomalé blikanie | 1 200 ms | Zariadenie offline, ale beží |

---

### Párovanie (Commissioning)

| Farba | RGB | Vzor | Perióda | Stav |
|-------|-----|------|---------|------|
| Modrá | (0, 0, 255) | Blikanie | 500 ms | Commissioning okno otvorené / pairing mode |

---

### Batéria

| Farba | RGB | Vzor | Úroveň nabitia |
|-------|-----|------|----------------|
| Oranžová | (255, 128, 0) | Svietí | 10 – 40 % |
| Červená | (255, 0, 0) | Svietí | < 10 % |

---

### Kalibračný režim

| Farba | RGB | Vzor | Perióda | Príležitosť |
|-------|-----|------|---------|-------------|
| Žltá | (255, 180, 0) | Blikanie | 600 ms | Kalibrácia aktívna |
| Žltá | (255, 180, 0) | 5× rýchle bliknutie | 120 ms/cyklus | Nastavená domáca poloha |
| Žltá | (255, 180, 0) | 5× rýchle bliknutie | 120 ms/cyklus | Nastavená spodná poloha |
| Žltá | (255, 180, 0) | 10× rýchle bliknutie | 100 ms/cyklus | Chyba: pohyb príliš krátky (< 100 krokov) |
| Žltá | (255, 180, 0) | 10× rýchle bliknutie | 100 ms/cyklus | Chyba: pohyb príliš dlhý (> 20 000 krokov) |
| Červená | (255, 0, 0) | 3× bliknutie → svietí | — | Timeout kalibrácie (5 min bez aktivity) |

---

### Chybový stav

| Farba | RGB | Vzor | Príležitosť |
|-------|-----|------|-------------|
| Červená | (255, 0, 0) | Svietí | Všeobecná chyba (napr. zlyhanie commissioning) |

---

## Rýchle blikanie (Quick Blink)

Krátkodobý overlay na aktuálny stav. Používa sa na potvrdenie akcií počas kalibrácie. Po odblikaní sa LED vráti k predchádzajúcemu stavu.
