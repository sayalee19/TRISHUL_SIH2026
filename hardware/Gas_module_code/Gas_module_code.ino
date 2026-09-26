#include <Arduino.h>
#include <Preferences.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------------- SHARED ----------------------
Adafruit_SSD1306 display(128, 64, &Wire, -1);
const int DHT_PIN = 10;
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

const unsigned long SAMPLE_INTERVAL_MS = 2000;
const int   SAMPLES_PER_READING = 20;
const int   CALIBRATION_SAMPLES = 50;
const float EMA_ALPHA = 0.2;
const unsigned long OLED_SWITCH_MS = 5000;
unsigned long g_lastOledSwitch = 0;
bool g_oledPageA = true; // true = H2S/H2/CH4, false = CO/NH3/Propane
const unsigned long OLED_REDRAW_MS = 200;
unsigned long g_lastOledDraw = 0;

// Last-known values, updated once per SAMPLE_INTERVAL_MS cycle, read by
// the OLED on its own independent 5s timer -- decoupled on purpose, since
// the display shouldn't block or be blocked by the sensor sample loop.
float g_disp_h2s = -2.0, g_disp_h2 = -2.0, g_disp_ch4 = -2.0;
float g_disp_co  = -2.0, g_disp_nh3 = -2.0, g_disp_prop = -2.0;

Preferences prefs;
unsigned long g_lastSampleTime = 0;

struct DhtReading { float tempC; float rh; bool valid; };

DhtReading readDHT() {
  DhtReading r;
  r.tempC = dht.readTemperature();
  r.rh = dht.readHumidity();
  r.valid = !(isnan(r.tempC) || isnan(r.rh));
  return r;
}

// Turns a value or sentinel into a short display string -- same sentinel
// convention as your Serial output, just condensed for a small screen.
String fmtPPM(float v) {
  if (v == -2.0) return "warm";
  if (v == -1.0) return "n/cal";
  if (v == -3.0) return "dht!";
  if (v == -4.0) return "n/dig";
  return String(v, 1);
}
void updateOLED() {
  unsigned long now = millis();
  if (now - g_lastOledSwitch >= OLED_SWITCH_MS) {
    g_lastOledSwitch = now;
    g_oledPageA = !g_oledPageA;
  }
  if (now - g_lastOledDraw < OLED_REDRAW_MS) return; // <-- add this guard
  g_lastOledDraw = now;

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  if (g_oledPageA) {
    display.println("H2S:  " + fmtPPM(g_disp_h2s) + " ppm");
    display.println("H2:   " + fmtPPM(g_disp_h2)  + " ppm");
    display.println("CH4:  " + fmtPPM(g_disp_ch4) + " ppm");
  } else {
    display.println("CO:   " + fmtPPM(g_disp_co)   + " ppm");
    display.println("NH3:  " + fmtPPM(g_disp_nh3)  + " ppm");
    display.println("Prop: " + fmtPPM(g_disp_prop) + " ppm");
  }
  display.display();
}

// ---------------------- H2S (GM-602B) ----------------------

namespace H2S {
  // *** VERIFY against your actual measured supply, not the datasheet's 5.0V test condition. ***
  const float VCC_MV  = 3300.0;
  const float RL_KOHM = 3.0;  // confirmed from Fermion board schematic (R2)

  // NOTE: pin 33 was in your original code with a comment claiming ESP32-S3
  // ADC1 -- on the S3, ADC1 channels are GPIO1-10, so 33 may actually be an
  // S2/classic-ESP32 pin mapping left over from a template. Double-check
  // against your actual board's pinout before trusting this.
  const int SENSOR_PIN = 4;

  const unsigned long WARMUP_TIME_MS = 120000; // assumes sensor already aged (48h/72h/168h per storage time)
  const float PPM_MIN_VALID = 0.5;
  const float PPM_MAX_VALID = 50.0;

  struct CurvePoint { float rsr0; float ppm; };
  const CurvePoint CURVE[] = {
    {0.6078f,  1.0176f}, {0.5623f,  1.4411f}, {0.5367f,  2.0409f},
    {0.4927f,  2.9411f}, {0.4630f,  3.8853f}, {0.4384f,  5.0440f},
    {0.3871f,  6.4352f}, {0.3262f,  9.7707f}, {0.2603f, 14.8349f},
    {0.2211f, 19.5971f}, {0.1907f, 24.1475f}, {0.1697f, 30.2767f},
    {0.1354f, 38.6280f}, {0.1073f, 50.1478f},
  };
  const int CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);

  const float TEMP_AXIS_C[7] = {-10, 0, 10, 20, 30, 40, 50};
  const float HUM_AXIS_PCT[3] = {30, 60, 85};  // reference RH = 55%
  const float CORRECTION_TABLE[3][7] = {
    { 1.71, 1.57, 1.46, 1.37, 1.13, 1.01, 0.89 },
    { 1.48, 1.32, 1.27, 1.09, 0.98, 0.87, 0.72 },
    { 1.28, 1.15, 1.09, 0.91, 0.86, 0.72, 0.67 },
  };



  float g_R0_kohm = -1.0;
  float g_ema_ppm = -1.0;
  unsigned long g_bootTime = 0;
  bool g_warmupDone = false;

  float readVoltage_mV(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogReadMilliVolts(SENSOR_PIN); delay(5); }
    return (float)sum / n;
  }

  float voltageToRs_kohm(float vout_mV) {
    if (vout_mV >= VCC_MV - 1.0) return 0.0001;
    if (vout_mV < 1.0) vout_mV = 1.0;
    return RL_KOHM * (VCC_MV - vout_mV) / vout_mV;
  }

  float tempHumidityFactor(float tempC, float rh) {
    if (tempC < TEMP_AXIS_C[0]) tempC = TEMP_AXIS_C[0];
    if (tempC > TEMP_AXIS_C[6]) tempC = TEMP_AXIS_C[6];
    if (rh < HUM_AXIS_PCT[0]) rh = HUM_AXIS_PCT[0];
    if (rh > HUM_AXIS_PCT[2]) rh = HUM_AXIS_PCT[2];
    int ti = 0; while (ti < 5 && TEMP_AXIS_C[ti+1] < tempC) ti++;
    float t0=TEMP_AXIS_C[ti], t1=TEMP_AXIS_C[ti+1];
    float ft = (t1>t0)?(tempC-t0)/(t1-t0):0.0;
    int hi = 0; while (hi < 1 && HUM_AXIS_PCT[hi+1] < rh) hi++;
    float h0=HUM_AXIS_PCT[hi], h1=HUM_AXIS_PCT[hi+1];
    float fh = (h1>h0)?(rh-h0)/(h1-h0):0.0;
    float row0 = CORRECTION_TABLE[hi][ti] + ft*(CORRECTION_TABLE[hi][ti+1]-CORRECTION_TABLE[hi][ti]);
    float row1 = CORRECTION_TABLE[hi+1][ti] + ft*(CORRECTION_TABLE[hi+1][ti+1]-CORRECTION_TABLE[hi+1][ti]);
    return row0 + fh*(row1-row0);
  }

  float ratioToPPM(float ratio) {
    if (ratio > 1.0f) ratio = 1.0f;
    if (ratio > CURVE[0].rsr0) {
      float t = (1.0f - ratio) / (1.0f - CURVE[0].rsr0);
      return t * CURVE[0].ppm;
    }
    if (ratio <= CURVE[CURVE_LEN-1].rsr0) return CURVE[CURVE_LEN-1].ppm;
    for (int i = 0; i < CURVE_LEN - 1; i++) {
      if (ratio <= CURVE[i].rsr0 && ratio >= CURVE[i+1].rsr0) {
        float logR = log10(ratio);
        float t = (logR - log10(CURVE[i].rsr0)) / (log10(CURVE[i+1].rsr0) - log10(CURVE[i].rsr0));
        float logP = log10(CURVE[i].ppm) + t*(log10(CURVE[i+1].ppm) - log10(CURVE[i].ppm));
        return pow(10.0f, logP);
      }
    }
    return CURVE[CURVE_LEN-1].ppm;
  }

  bool calibrateR0() {
    if (!g_warmupDone) { Serial.println(F("[H2S CAL REFUSED] still warming up")); return false; }
    float sumRs = 0; int valid = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
      float rawRs = voltageToRs_kohm(readVoltage_mV(1));
      DhtReading d = readDHT();
      if (!d.valid) { delay(20); continue; }
      float corrected = rawRs * tempHumidityFactor(d.tempC, d.rh);
      if (corrected > 0.001 && corrected < 100000.0) { sumRs += corrected; valid++; }
      delay(20);
    }
    if (valid < CALIBRATION_SAMPLES/2) { Serial.println(F("[H2S CAL FAILED]")); return false; }
    g_R0_kohm = sumRs / valid;
    prefs.putFloat("h2s_r0", g_R0_kohm);
    Serial.print(F("H2S calibrated. R0=")); Serial.println(g_R0_kohm, 4);
    return true;
  }

  float getPPM(DhtReading &d) {
    if (!g_warmupDone) return -2.0;
    if (g_R0_kohm <= 0.0) return -1.0;
    if (!d.valid) return -3.0;
    float corrected = voltageToRs_kohm(readVoltage_mV(SAMPLES_PER_READING)) * tempHumidityFactor(d.tempC, d.rh);
    float ratio = corrected / g_R0_kohm;
    if (ratio <= 0.0001) ratio = 0.0001;
    float ppm = ratioToPPM(ratio);
    if (ppm > PPM_MAX_VALID) ppm = PPM_MAX_VALID;
    g_ema_ppm = (g_ema_ppm < 0) ? ppm : (EMA_ALPHA*ppm + (1.0-EMA_ALPHA)*g_ema_ppm);
    return g_ema_ppm;
  }

  void begin() {
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
    g_R0_kohm = prefs.getFloat("h2s_r0", -1.0);
    g_bootTime = millis();
  }

  void updateWarmup() {
    if (!g_warmupDone && millis() - g_bootTime >= WARMUP_TIME_MS) {
      g_warmupDone = true;
      Serial.println(F("H2S warm-up complete."));
    }
  }
}
// ---------------------- CO (digitized power-law fit) ----------------------
// Rs/R0 = 3.58692 * ppm^(-0.84024), R²=0.996 -- strong fit. Validated range
// (0.94-1023.12 ppm) lines up closely with MiCS-5524's own datasheet-rated
// CO detection range (1-1000ppm), which is a good independent sanity check
// that this digitization is real, unlike the earlier failed attempt.
namespace CO {
  const float VCC_MV  = 3300.0;   // VERIFY against actual wiring
  const float RL_KOHM = 10.0;     // VERIFY against schematic -- shares the MICS-5524 physical sensor if that's what generated this data (see note below)

  const int SENSOR_PIN = 5;      // placeholder -- pick a real free ADC1 pin, distinct from all other gas namespaces

  const unsigned long WARMUP_TIME_MS = 120000; // settle window only, same caveat as all others
  const float PPM_MIN_VALID = 0.94;    // lower bound the fit was validated over
  const float PPM_MAX_VALID = 1023.12; // upper bound the fit was validated over

  const float CURVE_A = 3.58692f;
  const float CURVE_B = -0.84024f;
  const float INV_B   = 1.0f / (-CURVE_B);

  float g_R0_kohm = -1.0;
  float g_ema_ppm = -1.0;
  unsigned long g_bootTime = 0;
  bool g_warmupDone = false;

  float readVoltage_mV(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogReadMilliVolts(SENSOR_PIN); delay(5); }
    return (float)sum / n;
  }

  float voltageToRs_kohm(float vout_mV) {
    if (vout_mV >= VCC_MV - 1.0) return 0.0001;
    if (vout_mV < 1.0) vout_mV = 1.0;
    return RL_KOHM * (VCC_MV - vout_mV) / vout_mV;
  }

  // No temp/humidity correction -- MiCS-5524's datasheet doesn't publish
  // one, as established earlier.
  float ratioToPPM(float ratio) {
    if (ratio <= 0.0001f) ratio = 0.0001f;
    float ppm = pow(CURVE_A / ratio, INV_B);

    if (ppm < PPM_MIN_VALID) {
      ppm = PPM_MIN_VALID;
    } else if (ppm > PPM_MAX_VALID) {
      Serial.println(F("[CO WARNING] Reading above the power-law fit's validated range -- extrapolating past measured data."));
      ppm = PPM_MAX_VALID;
    }
    return ppm;
  }

  bool calibrateR0() {
    if (!g_warmupDone) { Serial.println(F("[CO CAL REFUSED] still warming up")); return false; }
    float sumRs = 0; int valid = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
      float rs = voltageToRs_kohm(readVoltage_mV(1));
      if (rs > 0.001 && rs < 100000.0) { sumRs += rs; valid++; }
      delay(20);
    }
    if (valid < CALIBRATION_SAMPLES/2) { Serial.println(F("[CO CAL FAILED]")); return false; }
    g_R0_kohm = sumRs / valid;
    prefs.putFloat("co_r0", g_R0_kohm);
    Serial.print(F("CO calibrated. R0=")); Serial.println(g_R0_kohm, 4);
    return true;
  }

  float getPPM() {
    if (!g_warmupDone) return -2.0;
    if (g_R0_kohm <= 0.0) return -1.0;
    float ratio = voltageToRs_kohm(readVoltage_mV(SAMPLES_PER_READING)) / g_R0_kohm;
    float ppm = ratioToPPM(ratio);
    g_ema_ppm = (g_ema_ppm < 0) ? ppm : (EMA_ALPHA*ppm + (1.0-EMA_ALPHA)*g_ema_ppm);
    return g_ema_ppm;
  }

  void begin() {
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
    g_R0_kohm = prefs.getFloat("co_r0", -1.0);
    g_bootTime = millis();
  }

  void updateWarmup() {
    if (!g_warmupDone && millis() - g_bootTime >= WARMUP_TIME_MS) {
      g_warmupDone = true;
      Serial.println(F("CO warm-up complete."));
    }
  }
}
// ---------------------- H2 (GMV-2021B) ----------------------

// ---------------------- NH3 (digitized power-law fit) ----------------------
// Rs/R0 = 0.98775 * ppm^(-0.22691), R²=0.987 -- good fit, points span
// ppm 1.26-164.55. Table built by sampling this fitted curve at points
// across that validated range (not raw digitized points, which is fine --
// the fit itself is the trustworthy artifact here, same treatment as CH4).
namespace NH3 {
  const float VCC_MV  = 3300.0;   // VERIFY against actual wiring
  const float RL_KOHM = 10.0;     // VERIFY against this board's schematic -- shares the MICS-5524 physical sensor if that's what generated this data

  const int SENSOR_PIN = 5;      // placeholder -- pick a real free ADC1 pin, distinct from all other gas namespaces

  const unsigned long WARMUP_TIME_MS = 120000; // settle window only, same caveat as all others
  const float PPM_MIN_VALID = 1.26;    // lower bound the fit was validated over
  const float PPM_MAX_VALID = 164.55;  // upper bound the fit was validated over -- not a physical ceiling, just where your data stopped

  const float CURVE_A = 0.98775f;
  const float CURVE_B = -0.22691f;
  const float INV_B   = 1.0f / (-CURVE_B);

  float g_R0_kohm = -1.0;
  float g_ema_ppm = -1.0;
  unsigned long g_bootTime = 0;
  bool g_warmupDone = false;

  float readVoltage_mV(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogReadMilliVolts(SENSOR_PIN); delay(5); }
    return (float)sum / n;
  }

  float voltageToRs_kohm(float vout_mV) {
    if (vout_mV >= VCC_MV - 1.0) return 0.0001;
    if (vout_mV < 1.0) vout_mV = 1.0;
    return RL_KOHM * (VCC_MV - vout_mV) / vout_mV;
  }

  // No temp/humidity correction here -- MiCS-5524's datasheet doesn't
  // publish one (confirmed earlier), and this fit doesn't include it either.
  float ratioToPPM(float ratio) {
    if (ratio <= 0.0001f) ratio = 0.0001f;
    float ppm = pow(CURVE_A / ratio, INV_B);

    if (ppm < PPM_MIN_VALID) {
      ppm = PPM_MIN_VALID;
    } else if (ppm > PPM_MAX_VALID) {
      Serial.println(F("[NH3 WARNING] Reading above the power-law fit's validated range -- extrapolating past measured data."));
      ppm = PPM_MAX_VALID;
    }
    return ppm;
  }

  bool calibrateR0() {
    if (!g_warmupDone) { Serial.println(F("[NH3 CAL REFUSED] still warming up")); return false; }
    float sumRs = 0; int valid = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
      float rs = voltageToRs_kohm(readVoltage_mV(1));
      if (rs > 0.001 && rs < 100000.0) { sumRs += rs; valid++; }
      delay(20);
    }
    if (valid < CALIBRATION_SAMPLES/2) { Serial.println(F("[NH3 CAL FAILED]")); return false; }
    g_R0_kohm = sumRs / valid;
    prefs.putFloat("nh3_r0", g_R0_kohm);
    Serial.print(F("NH3 calibrated. R0=")); Serial.println(g_R0_kohm, 4);
    return true;
  }

  float getPPM() {
    if (!g_warmupDone) return -2.0;
    if (g_R0_kohm <= 0.0) return -1.0;
    float ratio = voltageToRs_kohm(readVoltage_mV(SAMPLES_PER_READING)) / g_R0_kohm;
    float ppm = ratioToPPM(ratio);
    g_ema_ppm = (g_ema_ppm < 0) ? ppm : (EMA_ALPHA*ppm + (1.0-EMA_ALPHA)*g_ema_ppm);
    return g_ema_ppm;
  }

  void begin() {
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
    g_R0_kohm = prefs.getFloat("nh3_r0", -1.0);
    g_bootTime = millis();
  }

  void updateWarmup() {
    if (!g_warmupDone && millis() - g_bootTime >= WARMUP_TIME_MS) {
      g_warmupDone = true;
      Serial.println(F("NH3 warm-up complete."));
    }
  }
}

// ---------------------- PROPANE (digitized power-law fit) ----------------------
// Rs/R0 = 19.29803 * ppm^(-0.59052), R²=0.993 -- strong fit, points span
// ppm 3116.76-15215.12. Same caveats as NH3 above.
namespace Propane {
  const float VCC_MV  = 3300.0;   // VERIFY
  const float RL_KOHM = 10.0;     // VERIFY

  const int SENSOR_PIN = 5;      // placeholder -- pick a real free ADC1 pin, distinct from all other gas namespaces

  const unsigned long WARMUP_TIME_MS = 120000;
  const float PPM_MIN_VALID = 3116.76;
  const float PPM_MAX_VALID = 15215.12;

  const float CURVE_A = 19.29803f;
  const float CURVE_B = -0.59052f;
  const float INV_B   = 1.0f / (-CURVE_B);

  float g_R0_kohm = -1.0;
  float g_ema_ppm = -1.0;
  unsigned long g_bootTime = 0;
  bool g_warmupDone = false;

  float readVoltage_mV(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogReadMilliVolts(SENSOR_PIN); delay(5); }
    return (float)sum / n;
  }

  float voltageToRs_kohm(float vout_mV) {
    if (vout_mV >= VCC_MV - 1.0) return 0.0001;
    if (vout_mV < 1.0) vout_mV = 1.0;
    return RL_KOHM * (VCC_MV - vout_mV) / vout_mV;
  }

  float ratioToPPM(float ratio) {
    if (ratio <= 0.0001f) ratio = 0.0001f;
    float ppm = pow(CURVE_A / ratio, INV_B);

    if (ppm < PPM_MIN_VALID) {
      ppm = PPM_MIN_VALID;
    } else if (ppm > PPM_MAX_VALID) {
      Serial.println(F("[PROPANE WARNING] Reading above the power-law fit's validated range -- extrapolating past measured data."));
      ppm = PPM_MAX_VALID;
    }
    return ppm;
  }

  bool calibrateR0() {
    if (!g_warmupDone) { Serial.println(F("[PROPANE CAL REFUSED] still warming up")); return false; }
    float sumRs = 0; int valid = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
      float rs = voltageToRs_kohm(readVoltage_mV(1));
      if (rs > 0.001 && rs < 100000.0) { sumRs += rs; valid++; }
      delay(20);
    }
    if (valid < CALIBRATION_SAMPLES/2) { Serial.println(F("[PROPANE CAL FAILED]")); return false; }
    g_R0_kohm = sumRs / valid;
    prefs.putFloat("propane_r0", g_R0_kohm);
    Serial.print(F("Propane calibrated. R0=")); Serial.println(g_R0_kohm, 4);
    return true;
  }

  float getPPM() {
    if (!g_warmupDone) return -2.0;
    if (g_R0_kohm <= 0.0) return -1.0;
    float ratio = voltageToRs_kohm(readVoltage_mV(SAMPLES_PER_READING)) / g_R0_kohm;
    float ppm = ratioToPPM(ratio);
    g_ema_ppm = (g_ema_ppm < 0) ? ppm : (EMA_ALPHA*ppm + (1.0-EMA_ALPHA)*g_ema_ppm);
    return g_ema_ppm;
  }

  void begin() {
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
    g_R0_kohm = prefs.getFloat("propane_r0", -1.0);
    g_bootTime = millis();
  }

  void updateWarmup() {
    if (!g_warmupDone && millis() - g_bootTime >= WARMUP_TIME_MS) {
      g_warmupDone = true;
      Serial.println(F("Propane warm-up complete."));
    }
  }
}

namespace H2 {  
  // *** FILL IN once confirmed from this board's own schematic, same way you
  // confirmed H2S's RL=3K rather than trusting the generic wiki spec. ***
  const float VCC_MV  = 3300.0;   // VERIFY
  const float RL_KOHM = 10.0;     // datasheet says "Adjustable" -- VERIFY your board's actual value

  const int SENSOR_PIN = 34;  // placeholder -- pick a real ADC1 pin (GPIO1-10 on ESP32-S3), distinct from H2S's

  const unsigned long WARMUP_TIME_MS = 120000; // shorter aging requirement than H2S (24h/48h/72h vs 48h/72h/168h) -- still just a settle window here, do full aging on the bench first
  const float PPM_MIN_VALID = 0.1;
  const float PPM_MAX_VALID = 1000.0;

  // TODO: digitize from this datasheet's Fig.3 "H2" curve (black squares),
  // same MATLAB pixel-click process you used for H2S. Leaving this empty/
  // fabricated would silently produce wrong ppm for a real gas.
  struct CurvePoint { float rsr0; float ppm; };
  const CurvePoint CURVE[] = {
  {0.8276f,   1.0000f}, {0.7648f,   1.6681f}, {0.6849f,   2.5354f},
  {0.6037f,   4.8626f}, {0.5579f,   7.5646f}, {0.5406f,   9.5455f},
  {0.4765f,  13.8489f}, {0.4006f,  21.0490f}, {0.3531f,  32.7455f},
  {0.2922f,  48.6260f}, {0.2576f,  64.2807f}, {0.2066f,  97.7010f},
  {0.1764f, 126.1857f}, {0.1415f, 187.3817f}, {0.1153f, 278.2559f},
  {0.1016f, 319.9267f}, {0.0854f, 464.1589f}, {0.0729f, 559.0810f},
  {0.0604f, 657.9332f}, {0.0508f, 756.4633f}, {0.0462f, 830.2176f},
  {0.0427f, 932.6033f},
  };
  const int CURVE_LEN = sizeof(CURVE) / sizeof(CURVE[0]);

  // TODO: digitize from Fig.4. Note the reference condition here is
  // 20°C/65%RH (NOT 55%RH like H2S) -- don't copy H2S's axis/reference blindly.
  const int H2_TEMP_AXIS_LEN = 5;
  const int H2_HUM_AXIS_LEN  = 3;
  const float TEMP_AXIS_C[5]  = {10, 20, 30, 40, 50};
  const float HUM_AXIS_PCT[3] = {40, 65, 85};   // note: NOT H2S's {30,60,85}
  const float CORRECTION_TABLE[3][5] = {
  { 1.23, 1.12, 1.04, 0.96, 0.86 },
  { 1.04, 0.99, 0.90, 0.80, 0.74 },
  { 0.85, 0.74, 0.68, 0.61, 0.59 },
  };

  float g_R0_kohm = -1.0;
  float g_ema_ppm = -1.0;
  unsigned long g_bootTime = 0;
  bool g_warmupDone = false;

  float readVoltage_mV(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogReadMilliVolts(SENSOR_PIN); delay(5); }
    return (float)sum / n;
  }

  float voltageToRs_kohm(float vout_mV) {
    if (vout_mV >= VCC_MV - 1.0) return 0.0001;
    if (vout_mV < 1.0) vout_mV = 1.0;
    return RL_KOHM * (VCC_MV - vout_mV) / vout_mV;
  }

  // Stub until CURVE is populated -- returns -1 rather than a fabricated number.
  float ratioToPPM(float ratio) {
  if (CURVE_LEN == 0) return -1.0;
  if (ratio > 1.0f) ratio = 1.0f;

  if (ratio > CURVE[0].rsr0) {
    float t = (1.0f - ratio) / (1.0f - CURVE[0].rsr0);
    return t * CURVE[0].ppm;
  }
  if (ratio <= CURVE[CURVE_LEN - 1].rsr0) return CURVE[CURVE_LEN - 1].ppm;

  for (int i = 0; i < CURVE_LEN - 1; i++) {
    if (ratio <= CURVE[i].rsr0 && ratio >= CURVE[i + 1].rsr0) {
      float logR = log10(ratio);
      float t = (logR - log10(CURVE[i].rsr0)) / (log10(CURVE[i + 1].rsr0) - log10(CURVE[i].rsr0));
      float logP = log10(CURVE[i].ppm) + t * (log10(CURVE[i + 1].ppm) - log10(CURVE[i].ppm));
      return pow(10.0f, logP);
    }
  }
  return CURVE[CURVE_LEN - 1].ppm;
  }

  float tempHumidityFactor(float tempC, float rh) {
  if (tempC < TEMP_AXIS_C[0]) tempC = TEMP_AXIS_C[0];
  if (tempC > TEMP_AXIS_C[H2_TEMP_AXIS_LEN - 1]) tempC = TEMP_AXIS_C[H2_TEMP_AXIS_LEN - 1];
  if (rh < HUM_AXIS_PCT[0]) rh = HUM_AXIS_PCT[0];
  if (rh > HUM_AXIS_PCT[H2_HUM_AXIS_LEN - 1]) rh = HUM_AXIS_PCT[H2_HUM_AXIS_LEN - 1];

  int ti = 0;
  while (ti < H2_TEMP_AXIS_LEN - 2 && TEMP_AXIS_C[ti + 1] < tempC) ti++;
  float t0 = TEMP_AXIS_C[ti], t1 = TEMP_AXIS_C[ti + 1];
  float ft = (t1 > t0) ? (tempC - t0) / (t1 - t0) : 0.0;

  int hi = 0;
  while (hi < H2_HUM_AXIS_LEN - 2 && HUM_AXIS_PCT[hi + 1] < rh) hi++;
  float h0 = HUM_AXIS_PCT[hi], h1 = HUM_AXIS_PCT[hi + 1];
  float fh = (h1 > h0) ? (rh - h0) / (h1 - h0) : 0.0;

  float row0 = CORRECTION_TABLE[hi][ti]     + ft * (CORRECTION_TABLE[hi][ti + 1]     - CORRECTION_TABLE[hi][ti]);
  float row1 = CORRECTION_TABLE[hi + 1][ti] + ft * (CORRECTION_TABLE[hi + 1][ti + 1] - CORRECTION_TABLE[hi + 1][ti]);
  return row0 + fh * (row1 - row0);
  }

  bool calibrateR0() {
    if (!g_warmupDone) { Serial.println(F("[H2 CAL REFUSED] still warming up")); return false; }
    float sumRs = 0; int valid = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
      float rawRs = voltageToRs_kohm(readVoltage_mV(1));
      DhtReading d = readDHT();
      if (!d.valid) { delay(20); continue; }
      float corrected = rawRs * tempHumidityFactor(d.tempC, d.rh);
      if (corrected > 0.001 && corrected < 100000.0) { sumRs += corrected; valid++; }
      delay(20);
    }
    if (valid < CALIBRATION_SAMPLES/2) { Serial.println(F("[H2 CAL FAILED]")); return false; }
    g_R0_kohm = sumRs / valid;
    prefs.putFloat("h2_r0", g_R0_kohm);
    Serial.print(F("H2 calibrated. R0=")); Serial.println(g_R0_kohm, 4);
    return true;
  }

  float getPPM(DhtReading &d) {
    if (!g_warmupDone) return -2.0;
    if (g_R0_kohm <= 0.0) return -1.0;
    if (!d.valid) return -3.0;
    if (CURVE_LEN == 0) return -4.0; // no curve digitized yet
    float corrected = voltageToRs_kohm(readVoltage_mV(SAMPLES_PER_READING)) * tempHumidityFactor(d.tempC, d.rh);
    float ratio = corrected / g_R0_kohm;
    float ppm = ratioToPPM(ratio);
    if (ppm > PPM_MAX_VALID) ppm = PPM_MAX_VALID;
    g_ema_ppm = (g_ema_ppm < 0) ? ppm : (EMA_ALPHA*ppm + (1.0-EMA_ALPHA)*g_ema_ppm);
    return g_ema_ppm;
  }

  void begin() {
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
    g_R0_kohm = prefs.getFloat("h2_r0", -1.0);
    g_bootTime = millis();
  }

  void updateWarmup() {
    if (!g_warmupDone && millis() - g_bootTime >= WARMUP_TIME_MS) {
      g_warmupDone = true;
      Serial.println(F("H2 warm-up complete."));
    }
  }
  }

// ---------------------- CH4 (digitized power-law fit) ----------------------
// NOTE: source data was labeled "H2" by the digitization script but
// confirmed by you to actually be CH4 -- if that confirmation was wrong,
// this curve is wrong. The raw digitized (ppm, Rs/R0) point table was too
// coarse (only ~2 sig figs) to interpolate reliably, so this uses your
// fitted power law directly instead (R²=0.997, a strong fit). Valid range
// is what the fit was actually validated over: ppm ~392-5270. Outside
// that, this is extrapolating past measured data.

namespace CH4 {
  const float VCC_MV  = 3300.0;   // VERIFY against actual wiring
  const float RL_KOHM = 10.0;     // VERIFY against this board's schematic

  const int SENSOR_PIN = 35;      // placeholder -- pick a real free ADC1 pin (GPIO1-10 on ESP32-S3), distinct from H2S/H2/others already used

  const unsigned long WARMUP_TIME_MS = 120000; // settle window only -- same caveat as H2/H2S, do full aging separately
  const float PPM_MIN_VALID = 392.0;   // lower bound the fit was validated over
  const float PPM_MAX_VALID = 5270.0;  // upper bound the fit was validated over -- not a real physical ceiling, just where your data stopped

  // Rs/R0 = CURVE_A * ppm ^ CURVE_B  =>  ppm = (CURVE_A/ratio) ^ (1/-CURVE_B)
  const float CURVE_A = 2.29271f;
  const float CURVE_B = -0.18978f;
  const float INV_B   = 1.0f / (-CURVE_B); // precomputed = 5.2693

  const int TEMP_AXIS_LEN = 5;
  const int HUM_AXIS_LEN  = 3;
  const float TEMP_AXIS_C[5]  = {10, 20, 30, 40, 50};
  const float HUM_AXIS_PCT[3] = {66.57, 75.52, 86.58}; // ascending, from your digitized %RH labels
  const float CORRECTION_TABLE[3][5] = {
    { 1.09, 0.97, 0.93, 0.79, 0.76 },  // ~66.57% RH
    { 1.28, 1.11, 1.08, 0.92, 0.83 },  // ~75.52% RH
    { 1.57, 1.40, 1.26, 1.18, 0.96 },  // ~86.58% RH
  };

  float g_R0_kohm = -1.0;
  float g_ema_ppm = -1.0;
  unsigned long g_bootTime = 0;
  bool g_warmupDone = false;

  float readVoltage_mV(int n) {
    long sum = 0;
    for (int i = 0; i < n; i++) { sum += analogReadMilliVolts(SENSOR_PIN); delay(5); }
    return (float)sum / n;
  }

  float voltageToRs_kohm(float vout_mV) {
    if (vout_mV >= VCC_MV - 1.0) return 0.0001;
    if (vout_mV < 1.0) vout_mV = 1.0;
    return RL_KOHM * (VCC_MV - vout_mV) / vout_mV;
  }

  float tempHumidityFactor(float tempC, float rh) {
    if (tempC < TEMP_AXIS_C[0]) tempC = TEMP_AXIS_C[0];
    if (tempC > TEMP_AXIS_C[TEMP_AXIS_LEN-1]) tempC = TEMP_AXIS_C[TEMP_AXIS_LEN-1];
    if (rh < HUM_AXIS_PCT[0]) rh = HUM_AXIS_PCT[0];
    if (rh > HUM_AXIS_PCT[HUM_AXIS_LEN-1]) rh = HUM_AXIS_PCT[HUM_AXIS_LEN-1];

    int ti = 0;
    while (ti < TEMP_AXIS_LEN - 2 && TEMP_AXIS_C[ti+1] < tempC) ti++;
    float t0 = TEMP_AXIS_C[ti], t1 = TEMP_AXIS_C[ti+1];
    float ft = (t1 > t0) ? (tempC - t0) / (t1 - t0) : 0.0;

    int hi = 0;
    while (hi < HUM_AXIS_LEN - 2 && HUM_AXIS_PCT[hi+1] < rh) hi++;
    float h0 = HUM_AXIS_PCT[hi], h1 = HUM_AXIS_PCT[hi+1];
    float fh = (h1 > h0) ? (rh - h0) / (h1 - h0) : 0.0;

    float row0 = CORRECTION_TABLE[hi][ti]   + ft * (CORRECTION_TABLE[hi][ti+1]   - CORRECTION_TABLE[hi][ti]);
    float row1 = CORRECTION_TABLE[hi+1][ti] + ft * (CORRECTION_TABLE[hi+1][ti+1] - CORRECTION_TABLE[hi+1][ti]);
    return row0 + fh * (row1 - row0);
  }

  float ratioToPPM(float ratio) {
    if (ratio <= 0.0001f) ratio = 0.0001f; // guard: power law undefined/nonsensical at or below zero
    float ppm = pow(CURVE_A / ratio, INV_B);

    if (ppm < PPM_MIN_VALID) {
      ppm = PPM_MIN_VALID; // below the fit's validated range -- clamp rather than trust the extrapolation
    } else if (ppm > PPM_MAX_VALID) {
      Serial.println(F("[CH4 WARNING] Reading above the power-law fit's validated range -- extrapolating past measured data."));
      ppm = PPM_MAX_VALID;
    }
    return ppm;
  }

  bool calibrateR0() {
    if (!g_warmupDone) { Serial.println(F("[CH4 CAL REFUSED] still warming up")); return false; }
    float sumRs = 0; int valid = 0;
    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
      float rawRs = voltageToRs_kohm(readVoltage_mV(1));
      DhtReading d = readDHT();
      if (!d.valid) { delay(20); continue; }
      float corrected = rawRs * tempHumidityFactor(d.tempC, d.rh);
      if (corrected > 0.001 && corrected < 100000.0) { sumRs += corrected; valid++; }
      delay(20);
    }
    if (valid < CALIBRATION_SAMPLES/2) { Serial.println(F("[CH4 CAL FAILED]")); return false; }
    g_R0_kohm = sumRs / valid;
    prefs.putFloat("ch4_r0", g_R0_kohm);
    Serial.print(F("CH4 calibrated. R0=")); Serial.println(g_R0_kohm, 4);
    return true;
  }

  float getPPM(DhtReading &d) {
    if (!g_warmupDone) return -2.0;
    if (g_R0_kohm <= 0.0) return -1.0;
    if (!d.valid) return -3.0;
    float corrected = voltageToRs_kohm(readVoltage_mV(SAMPLES_PER_READING)) * tempHumidityFactor(d.tempC, d.rh);
    float ratio = corrected / g_R0_kohm;
    float ppm = ratioToPPM(ratio);
    g_ema_ppm = (g_ema_ppm < 0) ? ppm : (EMA_ALPHA*ppm + (1.0-EMA_ALPHA)*g_ema_ppm);
    return g_ema_ppm;
  }

  void begin() {
    analogSetPinAttenuation(SENSOR_PIN, ADC_11db);
    g_R0_kohm = prefs.getFloat("ch4_r0", -1.0);
    g_bootTime = millis();
  }

  void updateWarmup() {
    if (!g_warmupDone && millis() - g_bootTime >= WARMUP_TIME_MS) {
      g_warmupDone = true;
      Serial.println(F("CH4 warm-up complete."));
    }
  }
}
// ---------------------- SETUP / LOOP ----------------------

void setup() {
  Serial.begin(115200);
  delay(200);
  analogReadResolution(12);
  dht.begin();
  Wire.begin(); // confirm SDA/SCL pins don't collide with your final gas-sensor pin map
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // 0x3C is the common address -- verify against your specific OLED module
  Serial.println(F("[OLED] init failed -- check wiring/address"));
  }
display.clearDisplay();
display.display();
  prefs.begin("gassensors", false);

  H2S::begin();
  H2::begin();
  CH4::begin();
  CO::begin();
  NH3::begin();
  Propane::begin();

  Serial.println(F("H2S and H2 sensors initializing. Send 's' to calibrate H2S, 'h' to calibrate H2 (both require clean air + completed warm-up)."));
}

void loop() {
  H2S::updateWarmup();
  H2::updateWarmup();
  CH4::updateWarmup();
  CO::updateWarmup();
  NH3::updateWarmup();
  Propane::updateWarmup();

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 's' || c == 'S') H2S::calibrateR0();
    if (c == 'h' || c == 'H') H2::calibrateR0();
    if (c == 'c' || c == 'C') CH4::calibrateR0();
    if (c == 'o' || c == 'O') CO::calibrateR0();
    if (c == 'n' || c == 'N') NH3::calibrateR0();
    if (c == 'p' || c == 'P') Propane::calibrateR0();
  }

  unsigned long now = millis();
  if (now - g_lastSampleTime >= SAMPLE_INTERVAL_MS) {
    g_lastSampleTime = now;
    DhtReading d = readDHT();

    float h2sPpm = H2S::getPPM(d);
    float h2Ppm  = H2::getPPM(d);
    float ch4Ppm = CH4::getPPM(d);
    float coPpm  = CO::getPPM();
    float nh3Ppm = NH3::getPPM();
    float propPpm = Propane::getPPM();

    g_disp_h2s = h2sPpm; g_disp_h2 = h2Ppm; g_disp_ch4 = ch4Ppm;
    g_disp_co = coPpm; g_disp_nh3 = nh3Ppm; g_disp_prop = propPpm;

    Serial.printf("T=%.1fC H=%.1f%% | H2S: %s | H2: %s | CH4: %s | CO: %s | NH3: %s | Propane: %s\n",
      d.tempC, d.rh,
      (h2sPpm  == -2.0) ? "warming up" : (h2sPpm  == -1.0) ? "not calibrated" : (h2sPpm  == -3.0) ? "DHT fail" : String(h2sPpm,2).c_str(),
      (h2Ppm   == -2.0) ? "warming up" : (h2Ppm   == -1.0) ? "not calibrated" : (h2Ppm   == -3.0) ? "DHT fail" : (h2Ppm == -4.0) ? "curve not digitized" : String(h2Ppm,2).c_str(),
      (ch4Ppm  == -2.0) ? "warming up" : (ch4Ppm  == -1.0) ? "not calibrated" : (ch4Ppm  == -3.0) ? "DHT fail" : String(ch4Ppm,2).c_str(),
      (coPpm   == -2.0) ? "warming up" : (coPpm   == -1.0) ? "not calibrated" : String(coPpm,2).c_str(),
      (nh3Ppm  == -2.0) ? "warming up" : (nh3Ppm  == -1.0) ? "not calibrated" : String(nh3Ppm,2).c_str(),
      (propPpm == -2.0) ? "warming up" : (propPpm == -1.0) ? "not calibrated" : String(propPpm,2).c_str()
    );
  }
  updateOLED();
}