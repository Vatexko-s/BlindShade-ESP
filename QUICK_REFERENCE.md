# Calibration Quick Reference

## 📍 File Locations

```
~/esp-matter/examples/light_copy/
├── CALIBRATION_GUIDE.md      ← Full guide (this location)
├── QUICK_REFERENCE.md         ← This file
├── main/
│   ├── app_driver.cpp         ← Backend functions
│   ├── app_main.cpp           ← Matter attributes
│   └── app_priv.h             ← Function declarations
```

## 🔑 Matter Attributes

| Attribute | Type | Values | Purpose |
|-----------|------|--------|---------|
| `0xFFF1` | Boolean | `true`/`false` | Enable/disable calibration mode |
| `0xFFF2` | UInt8 | `1`=home, `2`=bottom | Trigger calibration commands |

**Cluster:** Window Covering (`0x0102`)  
**Endpoint:** `1`

## ⚡ chip-tool Commands

```bash
# Enable calibration
chip-tool windowcovering write-by-id 0xFFF1 true <node-id> 1

# Set home
chip-tool windowcovering write-by-id 0xFFF2 1 <node-id> 1

# Set bottom
chip-tool windowcovering write-by-id 0xFFF2 2 <node-id> 1

# Disable calibration
chip-tool windowcovering write-by-id 0xFFF1 false <node-id> 1

# Movement
chip-tool windowcovering go-to-lift-percentage 0 <node-id> 1      # Top
chip-tool windowcovering go-to-lift-percentage 10000 <node-id> 1  # Bottom
chip-tool windowcovering stop-motion <node-id> 1                  # Stop
```

## 📱 App Code (iOS)

```swift
// Enable calibration
device.writeAttribute(
    withEndpointID: 1,
    clusterID: 0x0102,
    attributeID: 0xFFF1,
    value: NSNumber(value: 1)
)

// Set home
device.writeAttribute(
    withEndpointID: 1,
    clusterID: 0x0102,
    attributeID: 0xFFF2,
    value: NSNumber(value: 1)
)

// Set bottom
device.writeAttribute(
    withEndpointID: 1,
    clusterID: 0x0102,
    attributeID: 0xFFF2,
    value: NSNumber(value: 2)
)
```

## 🖥️ Console Commands (USB)

```bash
# In serial monitor (esp32> prompt):
calibration-enable 1     # Enable
calibration-home         # Set home
calibration-bottom       # Set bottom
calibration-enable 0     # Disable
```

## 🔄 Calibration Flow

1. **Enable** calibration mode → `write 0xFFF1 = true`
2. **Move** to top → `go-to-lift-percentage 0`
3. **Set** home → `write 0xFFF2 = 1`
4. **Move** to bottom → `go-to-lift-percentage 10000`
5. **Set** bottom & save → `write 0xFFF2 = 2`
6. **Disable** calibration → `write 0xFFF1 = false`

## 📊 Expected Logs

```
I (xxx) APP[WC]: Custom attributes added: CalibrationMode=0xFFF1...
I (xxx) APP[WC]: Calibration mode ENABLED via Matter
I (xxx) DRIVER[STATE]: Home position set (current position = 0 steps, 0%)
I (xxx) DRIVER[STATE]: Bottom position set (max_steps = 6234, 100%)
I (xxx) DRIVER[MOTOR]: Loaded max_steps=6234 from NVS  ← After reboot
```

## 🛠️ Build & Flash

```bash
cd ~/esp-matter/examples/light_copy
source ~/esp-idf/export.sh && source ~/esp-matter/export.sh
idf.py build flash monitor
```

## �� Debugging

```bash
# Read current calibration mode
chip-tool windowcovering read-by-id 0xFFF1 <node-id> 1

# Read current position
chip-tool windowcovering read current-position-lift-percent-100ths <node-id> 1

# Check device logs for errors
idf.py monitor
```

## 🆘 Common Issues

| Problem | Solution |
|---------|----------|
| Write fails | Check device is commissioned & online |
| Doesn't save | Verify current_steps > 0 before bottom |
| No unlimited move | Confirm calibration mode enabled in logs |
| Lost after reboot | Check "Loaded max_steps from NVS" message |

## 📚 More Info

- Full guide: `CALIBRATION_GUIDE.md`
- Backend code: `main/app_driver.cpp`
- Matter integration: `main/app_main.cpp`

---

**Need help?** Check the full guide or device serial logs!
