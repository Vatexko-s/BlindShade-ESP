# Blindshade Window Covering

Minimalist Window Covering example based on `esp-matter/examples/light`, trimmed to a single endpoint with a dummy motor simulation. It exposes the Matter WindowCovering cluster (lift only) and keeps the position in sync for Apple Home slider use.

## 1. Build & Flash

ESP32-C3:
```
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```

ESP32-C6:
```
idf.py set-target esp32c6
idf.py build
idf.py flash monitor
```

## 2. Window Covering Behavior

- One endpoint with WindowCovering (Lift + PositionAwareLift).
- Commands: Open, Close, Stop, GoToLiftPercentage.
- Stepper motor control: 5000 steps = 100%, STEP pulse 10us, delay 2000us.
- Open sets target to 100%, Close sets target to 0%, Stop freezes immediately.

## 3. Apple Home Test

- Test slider at 0/50/100%.
- Tap Stop mid-movement and verify the position holds.

## 4. GPIO Prep (unused)

Stepper driver pins (A4988 EN active LOW):
- STEP = GPIO4
- DIR  = GPIO5
- EN   = GPIO6

## 5. Notes

- Commissioning uses the Matter setup code printed at boot.
- Use `chip-tool payload parse-setup-payload <QR>` to confirm passcode/discriminator if needed.
