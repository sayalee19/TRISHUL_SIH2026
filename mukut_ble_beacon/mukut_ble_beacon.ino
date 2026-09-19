/* ============================================================
   RATH SAARTHI MUKUT - BLE Beacon (ESP32-C3 Mini)
   ------------------------------------------------------------
   - Worker carries this MUKUT unit.
   - The device advertises a BLE beacon CONTINUOUSLY from boot,
     containing this unit's MUKUT_ID + a status byte (0x00 = normal).
     This lets SAARTHI nodes track approximate worker location
     via RSSI at all times, not just during an emergency.
   - Pressing the button triggers a hardware interrupt. The SAME
     beacon is then updated in place to status byte 0x01
     (emergency), advertised at a faster interval for quicker
     RSSI sampling/response.
   - NOTE: continuous advertising means continuous power draw --
     this is a deliberate trade-off for always-on tracking, not
     an oversight. If you only want the beacon active during an
     emergency (lower power, no routine tracking), that's a
     different design -- say so and this can be reverted.
   - There is still no "cancel" path back to normal once an
     emergency is triggered -- resolving one requires a reboot,
     same caveat as before.
   ============================================================ */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLEAdvertising.h>

// ============================================================
// CHANGE THIS PER MUKUT UNIT -- unique ID for each worker's device
// ============================================================
#define MUKUT_ID 1

// ============================================================
// PIN CONFIG
// ============================================================
#define BUTTON_PIN 4   // Button's other leg goes to GND. GPIO9 is
                        // avoided here since it's a strapping pin
                        // on ESP32-C3 and unsafe for a simple button.

// ============================================================
// Debounce settings
// ============================================================
const unsigned long DEBOUNCE_MS = 250;
volatile unsigned long lastInterruptTime = 0;

// Flag set inside the ISR, read/cleared in loop().
// Keeping the ISR itself minimal is important -- no Serial
// prints or BLE calls allowed directly inside an ISR.
volatile bool emergencyTriggered = false;

// Status byte values embedded in the beacon's manufacturer data.
// SAARTHI nodes read this byte to tell a routine location ping
// apart from an actual emergency.
#define STATUS_NORMAL    0x00
#define STATUS_EMERGENCY 0x01

BLEAdvertising *pAdvertising;
bool emergencyActive = false; // true once the beacon has been switched to emergency status

// ============================================================
// Interrupt Service Routine
// Fires the moment the button is pressed (FALLING edge, since
// we're using INPUT_PULLUP -- pin reads HIGH normally, LOW on press)
// ============================================================
void IRAM_ATTR buttonISR() {
  unsigned long now = millis();
  // Software debounce inside the ISR: ignore interrupts that
  // fire too close together (button bounce / electrical noise)
  if (now - lastInterruptTime > DEBOUNCE_MS) {
    emergencyTriggered = true;
    lastInterruptTime = now;
  }
}

// ============================================================
// Build and push the beacon's manufacturer data with a given
// status byte (STATUS_NORMAL or STATUS_EMERGENCY).
// MUKUT_ID + status are embedded as manufacturer-specific data
// so scanning SAARTHI nodes can read both directly from the
// advertisement packet without needing to connect.
// ------------------------------------------------------------
// Advertising must be stopped and restarted for updated data to
// take effect reliably -- just calling setAdvertisementData()
// while already advertising does not consistently push the
// change on all cores/library versions.
// ============================================================
void updateBeaconData(uint8_t status) {
  BLEAdvertisementData advData;
  std::string mfgData;
  mfgData += (char)0xFF;      // company ID low byte (placeholder)
  mfgData += (char)0xFF;      // company ID high byte (placeholder)
  mfgData += (char)MUKUT_ID;  // MUKUT unit ID
  mfgData += (char)status;    // STATUS_NORMAL or STATUS_EMERGENCY

  advData.setManufacturerData(mfgData);
  advData.setName("MUKUT_" + String(MUKUT_ID));

  pAdvertising->stop();
  pAdvertising->setAdvertisementData(advData);

  if (status == STATUS_EMERGENCY) {
    // Faster interval during an emergency: more frequent packets
    // means better RSSI sampling and quicker response, at the
    // cost of higher power draw -- acceptable trade during a crisis.
    pAdvertising->setMinInterval(0x20); // ~32ms  (0x20 * 0.625ms)
    pAdvertising->setMaxInterval(0x40); // ~64ms
  } else {
    // Slower interval during normal operation to conserve battery,
    // since this is now advertising continuously rather than
    // only during an emergency.
    pAdvertising->setMinInterval(0x140); // ~200ms
    pAdvertising->setMaxInterval(0x190); // ~250ms
  }

  pAdvertising->start();
}

// ============================================================
// One-time BLE setup, called once from setup(). Starts the
// beacon immediately in NORMAL status -- it never stops after
// this, it only ever switches status to EMERGENCY.
// ============================================================
void initBeacon() {
  BLEDevice::init(("MUKUT_" + String(MUKUT_ID)).c_str());
  pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setScanResponse(true);

  updateBeaconData(STATUS_NORMAL);

  Serial.println(">>> NORMAL BEACON ACTIVE <<<");
  Serial.printf("Broadcasting MUKUT_ID: %d, status: NORMAL\n", MUKUT_ID);
}

// ============================================================
// Switches the already-running beacon into emergency status.
// Called once, the first time the button interrupt fires.
// ============================================================
void triggerEmergencyBeacon() {
  updateBeaconData(STATUS_EMERGENCY);
  emergencyActive = true;

  Serial.println(">>> EMERGENCY BEACON ACTIVE <<<");
  Serial.printf("Broadcasting MUKUT_ID: %d, status: EMERGENCY\n", MUKUT_ID);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonISR, FALLING);

  initBeacon(); // starts continuous normal-status advertising immediately

  Serial.println("MUKUT unit ready. Waiting for emergency button press...");
  Serial.printf("MUKUT_ID: %d\n", MUKUT_ID);
}

// ============================================================
// LOOP
// ------------------------------------------------------------
// The ISR only sets a flag -- all actual work (Serial output,
// starting BLE) happens here in the main loop, which is safe.
// ============================================================
void loop() {
  if (emergencyTriggered && !emergencyActive) {
    emergencyTriggered = false; // clear flag, we're handling it now
    triggerEmergencyBeacon();
  }

  // NOTE: once triggered, status stays EMERGENCY indefinitely.
  // There is currently no "cancel" mechanism -- resolving an
  // emergency means physically rebooting/resetting this unit.
  // If you want a deliberate reset path (e.g. long-press the
  // same button, or a second button), that logic goes here.

  delay(50);
}
