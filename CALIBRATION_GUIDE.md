# Matter Calibration - Complete Usage Guide

## ✅ What Was Implemented

Your device now has **2 custom Matter attributes** for calibration:

### Attribute IDs:
- **0xFFF1** - Calibration Mode (Boolean)
  - `false` = Normal operation (software limits enabled)
  - `true` = Calibration mode (software limits disabled)
  
- **0xFFF2** - Calibration Command (UInt8)
  - `0` = No command
  - `1` = Set home position (top, 0%)
  - `2` = Set bottom position (100%, saves to NVS)

---

## 🧪 Testing with chip-tool

### Prerequisites
```bash
# Install chip-tool
brew install chip-tool

# Or build from source
git clone https://github.com/project-chip/connectedhomeip.git
cd connectedhomeip
./scripts/build/build_examples.py --target darwin-x64-chip-tool build
```

### Commission Device (if needed)
```bash
# Using QR code:
chip-tool pairing code <node-id> MT:Y.K9042C00KA0648G00

# Or manual code:
chip-tool pairing code <node-id> 34970112332

# Or on-network:
chip-tool pairing onnetwork-long 20202021 3840 <node-id>
```

### Complete Calibration Test
```bash
# Step 1: Enable calibration mode
chip-tool windowcovering write-by-id 0xFFF1 true <node-id> 1

# Verify in device logs:
# I (xxx) APP[WC]: Calibration mode ENABLED via Matter

# Step 2: Move blind to TOP
chip-tool windowcovering go-to-lift-percentage 0 <node-id> 1

# Wait for top, then stop:
chip-tool windowcovering stop-motion <node-id> 1

# Step 3: Set home position
chip-tool windowcovering write-by-id 0xFFF2 1 <node-id> 1

# Verify: "Home position set (current position = 0 steps, 0%)"

# Step 4: Move blind to BOTTOM
chip-tool windowcovering go-to-lift-percentage 10000 <node-id> 1

# Motor moves beyond old 5000 step limit
# Wait for bottom, then stop:
chip-tool windowcovering stop-motion <node-id> 1

# Step 5: Set bottom position (saves to NVS)
chip-tool windowcovering write-by-id 0xFFF2 2 <node-id> 1

# Verify: "Bottom position set (max_steps = XXXX, 100%)"

# Step 6: Disable calibration mode
chip-tool windowcovering write-by-id 0xFFF1 false <node-id> 1

# Step 7: Reboot and verify
# Check logs: "Loaded max_steps=XXXX from NVS"
```

### Quick Commands
```bash
# Read calibration mode
chip-tool windowcovering read-by-id 0xFFF1 <node-id> 1

# Read current position
chip-tool windowcovering read current-position-lift-percent-100ths <node-id> 1

# Move to position (after calibration)
chip-tool windowcovering go-to-lift-percentage 5000 <node-id> 1  # 50%

# Emergency stop
chip-tool windowcovering stop-motion <node-id> 1
```

---

## 📱 iOS App Integration

### Swift Code Example

```swift
import Matter
import os.log

class WindowCoveringCalibration {
    private let device: MTRBaseDevice
    private let endpoint: NSNumber = 1
    private let cluster: NSNumber = 0x0102  // WindowCovering
    
    // Custom attribute IDs
    private let calibModeAttr: NSNumber = 0xFFF1
    private let calibCmdAttr: NSNumber = 0xFFF2
    
    init(device: MTRBaseDevice) {
        self.device = device
    }
    
    // MARK: - Calibration Functions
    
    func enableCalibrationMode(_ enabled: Bool) async throws {
        try await writeAttribute(
            cluster: cluster,
            attribute: calibModeAttr,
            value: NSNumber(value: enabled)
        )
    }
    
    func setHomePosition() async throws {
        try await writeAttribute(
            cluster: cluster,
            attribute: calibCmdAttr,
            value: NSNumber(value: 1)
        )
    }
    
    func setBottomPosition() async throws {
        try await writeAttribute(
            cluster: cluster,
            attribute: calibCmdAttr,
            value: NSNumber(value: 2)
        )
    }
    
    // MARK: - Movement
    
    func moveToPosition(_ percent: Int) async throws {
        let params = MTRWindowCoveringClusterGoToLiftPercentageParams()
        params.liftPercent100thsValue = NSNumber(value: percent * 100)
        
        try await invokeCommand(
            cluster: cluster,
            command: 0x05,
            params: params
        )
    }
    
    func stopMotion() async throws {
        try await invokeCommand(
            cluster: cluster,
            command: 0x02,
            params: nil
        )
    }
    
    // MARK: - Helper Methods
    
    private func writeAttribute(cluster: NSNumber, attribute: NSNumber, value: NSNumber) async throws {
        try await withCheckedThrowingContinuation { continuation in
            device.writeAttribute(
                withEndpointID: endpoint,
                clusterID: cluster,
                attributeID: attribute,
                value: value,
                timedWriteTimeout: nil,
                queue: .main
            ) { error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume()
                }
            }
        }
    }
    
    private func invokeCommand(cluster: NSNumber, command: NSNumber, params: Any?) async throws {
        try await withCheckedThrowingContinuation { continuation in
            device.invokeCommand(
                withEndpointID: endpoint,
                clusterID: cluster,
                commandID: command,
                commandFields: params as? [String: Any],
                timedInvokeTimeout: nil,
                queue: .main
            ) { _, error in
                if let error = error {
                    continuation.resume(throwing: error)
                } else {
                    continuation.resume()
                }
            }
        }
    }
}

// MARK: - SwiftUI Example

struct CalibrationView: View {
    @State private var step = 0
    @State private var isProcessing = false
    let calibManager: WindowCoveringCalibration
    
    var body: some View {
        VStack(spacing: 20) {
            Text("Blind Calibration")
                .font(.largeTitle)
            
            ProgressView(value: Double(step), total: 5)
            
            Text(stepDescription)
                .foregroundColor(.secondary)
            
            if isProcessing {
                ProgressView()
            }
            
            HStack {
                if step > 0 {
                    Button("Back") { step -= 1 }
                        .disabled(isProcessing)
                }
                
                Button(step == 5 ? "Finish" : "Next") {
                    performStep()
                }
                .buttonStyle(.borderedProminent)
                .disabled(isProcessing)
            }
            
            Button("Emergency Stop") {
                Task { try? await calibManager.stopMotion() }
            }
            .foregroundColor(.red)
        }
        .padding()
    }
    
    var stepDescription: String {
        switch step {
        case 0: return "Ready to start"
        case 1: return "Moving to top..."
        case 2: return "Tap Next when at TOP"
        case 3: return "Moving to bottom..."
        case 4: return "Tap Next when at BOTTOM"
        case 5: return "✅ Complete!"
        default: return ""
        }
    }
    
    func performStep() {
        isProcessing = true
        Task {
            do {
                switch step {
                case 0:
                    try await calibManager.enableCalibrationMode(true)
                    step = 1
                    try await calibManager.moveToPosition(0)
                    step = 2
                    
                case 2:
                    try await calibManager.stopMotion()
                    try await calibManager.setHomePosition()
                    step = 3
                    try await calibManager.moveToPosition(100)
                    step = 4
                    
                case 4:
                    try await calibManager.stopMotion()
                    try await calibManager.setBottomPosition()
                    try await calibManager.enableCalibrationMode(false)
                    step = 5
                    
                default:
                    break
                }
            } catch {
                print("Error: \(error)")
            }
            isProcessing = false
        }
    }
}
```

---

## 🤖 Android App Integration

```kotlin
import chip.devicecontroller.ChipDeviceController
import chip.devicecontroller.ChipClusters
import kotlinx.coroutines.*

class WindowCoveringCalibration(
    private val controller: ChipDeviceController,
    private val deviceId: Long
) {
    companion object {
        const val ENDPOINT = 1
        const val CLUSTER = 0x0102L
        const val CALIB_MODE_ATTR = 0xFFF1L
        const val CALIB_CMD_ATTR = 0xFFF2L
    }
    
    suspend fun setCalibrationMode(enabled: Boolean) {
        // Write to 0xFFF1 attribute
        controller.write(
            WriteAttributesCallback(),
            deviceId,
            listOf(
                AttributeWriteRequest.newInstance(
                    ENDPOINT, CLUSTER, CALIB_MODE_ATTR,
                    if (enabled) 1 else 0
                )
            )
        )
    }
    
    suspend fun setHomePosition() {
        // Write 1 to 0xFFF2
        controller.write(
            WriteAttributesCallback(),
            deviceId,
            listOf(
                AttributeWriteRequest.newInstance(
                    ENDPOINT, CLUSTER, CALIB_CMD_ATTR, 1
                )
            )
        )
    }
    
    suspend fun setBottomPosition() {
        // Write 2 to 0xFFF2
        controller.write(
            WriteAttributesCallback(),
            deviceId,
            listOf(
                AttributeWriteRequest.newInstance(
                    ENDPOINT, CLUSTER, CALIB_CMD_ATTR, 2
                )
            )
        )
    }
    
    suspend fun moveToPosition(percent: Int) {
        val cluster = ChipClusters.WindowCoveringCluster(
            controller, deviceId, ENDPOINT
        )
        cluster.goToLiftPercentage(percent * 100, null)
    }
    
    suspend fun stopMotion() {
        val cluster = ChipClusters.WindowCoveringCluster(
            controller, deviceId, ENDPOINT
        )
        cluster.stopMotion(null)
    }
}

// Jetpack Compose UI
@Composable
fun CalibrationScreen(manager: WindowCoveringCalibration) {
    var step by remember { mutableStateOf(0) }
    var isProcessing by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    
    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text("Calibration", style = MaterialTheme.typography.h4)
        
        LinearProgressIndicator(progress = step / 5f)
        
        Text(getStepDescription(step))
        
        if (isProcessing) CircularProgressIndicator()
        
        Row {
            if (step > 0) {
                Button(onClick = { step-- }) { Text("Back") }
            }
            Button(
                onClick = {
                    isProcessing = true
                    scope.launch {
                        performStep(step, manager) { step++ }
                        isProcessing = false
                    }
                }
            ) {
                Text(if (step == 5) "Finish" else "Next")
            }
        }
        
        Button(
            onClick = { scope.launch { manager.stopMotion() } },
            colors = ButtonDefaults.buttonColors(
                containerColor = MaterialTheme.colorScheme.error
            )
        ) {
            Text("Emergency Stop")
        }
    }
}
```

---

## 🔍 Verification

### Device Serial Logs
```
# On boot:
I (xxx) APP[WC]: Adding custom calibration attributes...
I (xxx) APP[WC]: Custom attributes added: CalibrationMode=0xFFF1...

# When enabled:
I (xxx) APP[WC]: Calibration mode ENABLED via Matter
I (xxx) DRIVER[STATE]: Calibration mode ENABLED

# When home set:
I (xxx) APP[WC]: Home position set via Matter
I (xxx) DRIVER[STATE]: Home position set (current position = 0 steps, 0%)

# When bottom set:
I (xxx) APP[WC]: Bottom position set via Matter
I (xxx) DRIVER[STATE]: Bottom position set (max_steps = 6234, 100%)

# After reboot:
I (xxx) DRIVER[MOTOR]: Loaded max_steps=6234 from NVS
```

---

## 🆘 Troubleshooting

### Attribute write fails
- Verify device is commissioned
- Check node ID is correct
- Ensure Matter fabric is active

### Calibration doesn't save
- Check "Bottom position set" in logs
- Verify current_steps > 0 before setting bottom
- Ensure NVS partition isn't full

### Motor doesn't move beyond limits
- Confirm calibration mode enabled
- Check logs for "Calibration mode ENABLED"
- Verify 0xFFF1 write succeeded

---

## ✅ Success Indicators

✓ chip-tool commands succeed
✓ Logs show "Calibration mode ENABLED"
✓ Motor moves beyond 5000 steps
✓ After reboot: "Loaded max_steps=XXXX from NVS"
✓ Percentages match physical position

---

## 📚 Additional Resources

- Console commands (USB): `calibration-enable`, `calibration-home`, `calibration-bottom`
- Matter spec: Manufacturer-specific attributes (0xFFF0-0xFFFF)
- Your backend: `app_driver.cpp` functions

---

Ready to use! 🚀
