/* ============================================================
   RATH SAARTHI MUKUT - ESP-NOW Alert Node
   ------------------------------------------------------------
   IDENTICAL code for every board. The ONLY line you change
   before uploading to a new board is NODE_ID below.

   What this does:
   - Reads MQ4 (methane) and MQ7 (CO) gas levels in PPM
   - Reads MPU6050 accelerometer to detect abnormal vibration
   - Prints ALL live readings to Serial Monitor every loop,
     so you can watch real values for debugging/calibration.
   - Does NOT transmit raw sensor data over ESP-NOW.
     It only transmits a single alert packet (1 char + MAC)
     when a value crosses its safety threshold, to keep
     the wireless payload minimal.
   - Uses a hardcoded "topology table": each node already
     knows which MAC address to send its alert to. You fill
     this in once, and it doesn't change per-board (only
     NODE_ID changes).
   ============================================================ */

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>

// ============================================================
// CHANGE ONLY THIS LINE PER BOARD BEFORE UPLOADING
// ============================================================
#define NODE_ID 1        // Node 1 on board A, Node 2 on board B, etc.

// ============================================================
// TOPOLOGY TABLE  (fill in real MAC addresses)
// ------------------------------------------------------------
// Index = NODE_ID - 1. This tells each node WHERE to send its
// own alert packets. Get a board's MAC from its own Serial
// Monitor at boot ("This Node's MAC: ..."), then hardcode it
// into the table on the OTHER board(s).
// This table is the same in every uploaded file -- only the
// NODE_ID macro above differs per board.
// ============================================================
uint8_t nextHopTable[][6] = {
  /* index 0 = Node 1 sends its alerts to -> */ {0x5C, 0x01, 0x3B, 0x73, 0x48, 0x04}, // <-- fill with Node 2's MAC
  /* index 1 = Node 2 sends its alerts to -> */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}  // <-- fill with Node 1's MAC
};

// ============================================================
// PIN CONFIG
// ============================================================
#define MQ4_PIN 34   // Methane sensor analog pin (through voltage divider)
#define MQ7_PIN 35   // CO sensor analog pin (through voltage divider)
// MPU6050 uses I2C -> default ESP32 pins: SDA = GPIO21, SCL = GPIO22

// ============================================================
// VOLTAGE DIVIDER CORRECTION
// ------------------------------------------------------------
// R1 = 18k, R2 = 27k -> ratio = R2 / (R1+R2) = 0.6
// This scales the sensor's ~5V AOUT swing down to a safe
// ~3.0V max for the ESP32 ADC. We must UNDO this ratio in
// code to recover the true sensor voltage before doing any
// gas-concentration math.
// ============================================================
const float DIVIDER_RATIO = 0.6;
const float ADC_VREF      = 3.3;   // ESP32 ADC reference voltage
const float SENSOR_VCC    = 5.0;   // Actual supply voltage to the MQ sensor's internal load circuit
const float RL            = 10.0;  // Load resistor on the MQ sensor module (check your module's datasheet)

// ============================================================
// GAS SENSOR CURVE CONSTANTS (approximate, from datasheets)
// Recalibrate MQ4_RO / MQ7_RO for your specific sensor units
// (measured resistance in clean air).
// ============================================================
float MQ4_RO = 10.0;
float MQ7_RO = 10.0;
const float MQ4_A = 1012.7;
const float MQ4_B = -2.786;
const float MQ7_A = 99.042;
const float MQ7_B = -1.518;

// ============================================================
// SAFETY THRESHOLDS -- PLACEHOLDERS
// Replace these with the actual limits from your safety report.
// ============================================================
const float MQ4_THRESHOLD_PPM     = 5000.0; // methane danger level (placeholder)
const float MQ7_THRESHOLD_PPM     = 50.0;   // CO exposure limit in ppm (placeholder)
const float VIBRATION_THRESHOLD_G = 0.3;    // acceptable deviation from 1g baseline

// Hysteresis margin so an alert doesn't spam on/off right at
// the threshold boundary. Value must drop below 90% of the
// threshold before the alert is allowed to fire again.
const float HYSTERESIS_FACTOR = 0.9;

// ============================================================
// MPU6050 REGISTERS (raw I2C access, no external library needed)
// ============================================================
#define MPU_ADDR       0x68
#define MPU_PWR_MGMT_1 0x6B
#define MPU_ACCEL_XOUT 0x3B

// ============================================================
// ALERT PACKET -- deliberately minimal payload
// ============================================================
typedef struct {
  uint8_t nodeId;     // which node detected the problem
  uint8_t mac[6];     // MAC address of the detecting node
  char alertCode;     // 'G' = MQ4 methane, 'C' = MQ7 CO, 'V' = vibration/seismic
} alert_message;

alert_message outgoingAlert;
alert_message incomingAlert;
esp_now_peer_info_t peerInfo;

// Latches so each alert type only fires once per breach event,
// not every single loop iteration while still over threshold.
bool mq4AlertActive = false;
bool mq7AlertActive = false;
bool vibAlertActive = false;

// ============================================================
// Send a minimal alert packet to this node's designated next hop
// ============================================================
void sendAlert(char code) {
  outgoingAlert.nodeId = NODE_ID;
  WiFi.macAddress(outgoingAlert.mac);
  outgoingAlert.alertCode = code;

  uint8_t *targetMac = nextHopTable[NODE_ID - 1];
  esp_err_t result = esp_now_send(targetMac, (uint8_t *)&outgoingAlert, sizeof(outgoingAlert));

  Serial.print("ALERT SENT [");
  Serial.print(code);
  Serial.println(result == ESP_OK ? "] -> send queued OK" : "] -> send FAILED");
}

// ============================================================
// ESP-NOW send callback (newer core signature: wifi_tx_info_t*)
// ============================================================
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("Delivery Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// ============================================================
// ESP-NOW receive callback -- fires when this node gets an alert
// from another node in the network
// ============================================================
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingBytes, int len) {
  memcpy(&incomingAlert, incomingBytes, sizeof(incomingAlert));

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           incomingAlert.mac[0], incomingAlert.mac[1], incomingAlert.mac[2],
           incomingAlert.mac[3], incomingAlert.mac[4], incomingAlert.mac[5]);

  Serial.println("!!!!!!!!!! ALERT RECEIVED !!!!!!!!!!");
  Serial.printf("From Node ID : %d\n", incomingAlert.nodeId);
  Serial.printf("MAC Address  : %s\n", macStr);
  Serial.printf("Alert Code   : %c\n", incomingAlert.alertCode);
  Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

  // NOTE: with only 2 nodes, we don't forward further (that would
  // just bounce the alert back and forth). When you add a 3rd node
  // in a chain topology, add forwarding logic here: check that
  // incomingAlert.nodeId != NODE_ID, then re-send to your own
  // nextHopTable[NODE_ID - 1] target, being careful not to send
  // it back the way it came (loop prevention).
}

// ============================================================
// MPU6050 setup
// ============================================================
void initMPU6050() {
  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_PWR_MGMT_1);
  Wire.write(0);  // wake the sensor up from sleep mode
  Wire.endTransmission(true);
}

// Reads raw accelerometer values and converts to g's (+-2g range default)
void readAccel(float &ax, float &ay, float &az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(MPU_ACCEL_XOUT);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6, true);

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  ax = rawX / 16384.0;
  ay = rawY / 16384.0;
  az = rawZ / 16384.0;
}

// ============================================================
// Convert ADC reading -> true gas PPM
// ============================================================
float readGasPPM(int pin, float Ro, float a, float b) {
  int adcVal = analogRead(pin);
  float adcVoltage = (adcVal / 4095.0) * ADC_VREF;

  // Undo the external voltage divider to recover the sensor's real AOUT voltage
  float sensorVoltage = adcVoltage / DIVIDER_RATIO;
  if (sensorVoltage <= 0.01) sensorVoltage = 0.01; // avoid divide-by-zero

  // Rs must be calculated using the sensor's actual 5V supply,
  // NOT the ESP32's 3.3V ADC reference (this was a bug in earlier
  // versions of this code -- fixed here).
  float Rs = ((SENSOR_VCC * RL) / sensorVoltage) - RL;
  float ratio = Rs / Ro;

  return a * pow(ratio, b);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);

  Serial.print("This Node's MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.printf("Running as NODE_ID: %d\n", NODE_ID);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  // Register this node's designated next-hop as an ESP-NOW peer
  // so we're allowed to send unicast packets to it.
  memcpy(peerInfo.peer_addr, nextHopTable[NODE_ID - 1], 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  analogReadResolution(12); // 0-4095 ADC range on ESP32
  initMPU6050();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  // ---- Read all sensors ----
  float mq4ppm = readGasPPM(MQ4_PIN, MQ4_RO, MQ4_A, MQ4_B);
  float mq7ppm = readGasPPM(MQ7_PIN, MQ7_RO, MQ7_A, MQ7_B);

  float ax, ay, az;
  readAccel(ax, ay, az);
  float accelMagnitude = sqrt(ax * ax + ay * ay + az * az);
  float vibrationDeviation = fabs(accelMagnitude - 1.0); // deviation from resting 1g

  // ---- Always print live readings locally for debugging ----
  Serial.println("---- Live Sensor Readings ----");
  Serial.printf("MQ4 (CH4) : %.2f ppm\n", mq4ppm);
  Serial.printf("MQ7 (CO)  : %.2f ppm\n", mq7ppm);
  Serial.printf("Vibration : %.3f g deviation\n", vibrationDeviation);
  Serial.println("-------------------------------");

  // ---- MQ4 threshold check (with hysteresis latch) ----
  if (!mq4AlertActive && mq4ppm >= MQ4_THRESHOLD_PPM) {
    mq4AlertActive = true;
    sendAlert('G');
  } else if (mq4AlertActive && mq4ppm < MQ4_THRESHOLD_PPM * HYSTERESIS_FACTOR) {
    mq4AlertActive = false; // reset once safely below threshold
  }

  // ---- MQ7 threshold check ----
  if (!mq7AlertActive && mq7ppm >= MQ7_THRESHOLD_PPM) {
    mq7AlertActive = true;
    sendAlert('C');
  } else if (mq7AlertActive && mq7ppm < MQ7_THRESHOLD_PPM * HYSTERESIS_FACTOR) {
    mq7AlertActive = false;
  }

  // ---- Vibration threshold check ----
  if (!vibAlertActive && vibrationDeviation >= VIBRATION_THRESHOLD_G) {
    vibAlertActive = true;
    sendAlert('V');
  } else if (vibAlertActive && vibrationDeviation < VIBRATION_THRESHOLD_G * HYSTERESIS_FACTOR) {
    vibAlertActive = false;
  }

  delay(6000); // sample rate -- adjust as needed
}
