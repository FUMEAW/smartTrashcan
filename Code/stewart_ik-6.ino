/*
 * stewart_ik.ino
 * ──────────────────────────────────────────────────────────────────
 * Smart Trashcan — Stewart Platform IK Solver
 * Arduino UNO Q (STM32 side) + PCA9685 over I2C
 *
 * GEOMETRY (all locked values from spec sheet v2):
 *   Platform attachment radius  : 100mm
 *   Arm length                  : 38mm
 *   Pushrod length              : 53mm
 *   Neutral arm angle           : 46° from horizontal
 *   Clearance angle             : 5°
 *   Max tilt                    : 20° (raised to fit EXPEL_DEG)
 *   Expulsion threshold         : 18.5°
 *   Bowl lip angle              : 9.5°
 *   Bin directions              : A=0°, B=120°, C=240°
 *
 * WIRING:
 *   PCA9685 SDA → UNO Q SDA
 *   PCA9685 SCL → UNO Q SCL
 *   PCA9685 VCC → 3.3V or 5V (logic)
 *   PCA9685 V+  → 5V servo rail (XL4016 buck converter)
 *   Servo A     → PCA9685 channel 0  (top, 90° mount position)
 *   Servo B     → PCA9685 channel 3  (bottom-right, 210° mount)
 *   Servo C     → PCA9685 channel 2  (bottom-left, 330° mount)
 *
 * SERIAL COMMANDS (115200 baud):
 *   level              — return platform to level
 *   tilt <dir> <mag>   — tilt to direction (deg) at magnitude (deg)
 *   bin a|b|c          — tilt toward bin A, B, or C at expulsion angle
 *   sweep              — demo sweep through all three bin directions
 *   status             — print current position
 *   trim <a> <b> <c>   — set servo trims in µs e.g. "trim 10 -5 0"
 *   m                  — show menu
 * ──────────────────────────────────────────────────────────────────
 */

#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ── Result type (must be declared before any function that uses it) ─
struct ServoResult {
  float armAngleDeg;
  int   pulseUS;
};

// ── PCA9685 ────────────────────────────────────────────────────────
Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver(0x40);

// ── Servo channels on PCA9685 ──────────────────────────────────────
#define SERVO_A  0
#define SERVO_B  3
#define SERVO_C  2

const uint8_t SERVO_CH[3] = { SERVO_A, SERVO_B, SERVO_C };

// ── PWM ────────────────────────────────────────────────────────────
#define PWM_FREQ      50
#define SERVO_MIN_US  1000
#define SERVO_MAX_US  2000

// ── Geometry constants ─────────────────────────────────────────────
const float PLATE_R       = 100.0f;  // attachment radius on top plate (mm)
const float ARM_LEN       = 38.0f;   // servo arm length (mm)
const float ROD_LEN       = 53.0f;   // pushrod length (mm)
const float NEUTRAL_DEG   = 46.0f;   // arm neutral angle from horizontal (deg)
const float CLEARANCE_DEG = 5.0f;    // min arm angle from horizontal (deg)
const float MAX_TILT_DEG  = 20.0f;   // hard ceiling — must be >= EXPEL_DEG + BOWL_OFFSET
const float EXPEL_DEG     = 18.5f;   // commanded tilt to expel item
const float BOWL_OFFSET   = 2.0f;    // added inside computeServo before clamping

// Servo mount angles on base plate (degrees, 120° apart)
const float MOUNT_ANGLE[3] = { 90.0f, 210.0f, 330.0f };

// Bin tilt directions (degrees)
const float BIN_DIR[3]     = { 0.0f, 120.0f, 240.0f };
const char  BIN_NAME[3][12] = { "recycling", "landfill", "compost" };

// ── Per-servo trim (µs) ────────────────────────────────────────────
// Positive = arm moves higher, negative = arm moves lower.
// Update these from your zeroing calibration session.
int trimUS[3] = { 0, -40, -80 };   // [A, B, C]

// ── Current state ──────────────────────────────────────────────────
float currentDir = 0.0f;
float currentMag = 0.0f;

// ── Helpers ────────────────────────────────────────────────────────
#define DEG2RAD(x) ((x) * (float)M_PI / 180.0f)
#define RAD2DEG(x) ((x) * 180.0f / (float)M_PI)

uint16_t usTotick(uint16_t us) {
  return (uint16_t)((us / 20000.0f) * 4096.0f);
}

void setServoUS(uint8_t ch, int us) {
  us = constrain(us, SERVO_MIN_US, SERVO_MAX_US);
  pca.setPWM(ch, 0, usTotick((uint16_t)us));
}

// ── IK Solver ──────────────────────────────────────────────────────
//
// KEY FIX: bowl offset is applied INSIDE computeServo, AFTER which
// the result is clamped to MAX_TILT_DEG. movePlatform does NOT clamp
// the incoming magnitude — that was preventing EXPEL_DEG from reaching
// the solver when EXPEL_DEG > old MAX_TILT_DEG.
//
// ARM ANGLE → PWM MAPPING:
//   Horn attached at horizontal (0°) = 1000µs
//   46° neutral                      = 1256µs
//   90° vertical (avoid)             = 1500µs
//   us = 1000 + (arm_angle_deg / 180) * 1000

ServoResult computeServo(uint8_t idx, float tiltDirDeg, float tiltMagDeg) {
  // Apply bowl offset then clamp — offset happens FIRST
  float effectiveMag = tiltMagDeg + BOWL_OFFSET;
  effectiveMag = min(effectiveMag, MAX_TILT_DEG);

  float dirRad = DEG2RAD(tiltDirDeg);
  float magRad = DEG2RAD(effectiveMag);

  // Normal vector of tilted plate
  float nx =  sinf(magRad) * sinf(dirRad);
  float ny = -sinf(magRad) * cosf(dirRad);
  float nz =  cosf(magRad);

  // Attachment point on plate for this servo
  float mountRad = DEG2RAD(MOUNT_ANGLE[idx]);
  float px = PLATE_R * cosf(mountRad);
  float py = PLATE_R * sinf(mountRad);

  // Vertical displacement at this attachment point
  float dz = -(nx * px + ny * py) / nz;

  // Map displacement to arm angle
  float sinNeutral  = sinf(DEG2RAD(NEUTRAL_DEG));
  float sinArmAngle = (dz / ARM_LEN) + sinNeutral;
  sinArmAngle = constrain(sinArmAngle, -1.0f, 1.0f);
  float armAngleDeg = RAD2DEG(asinf(sinArmAngle));

  // Clamp to safe range
  armAngleDeg = constrain(armAngleDeg, CLEARANCE_DEG, 85.0f);

  // Convert to PWM
  int us = (int)(1000.0f + (armAngleDeg / 180.0f) * 1000.0f);
  us += trimUS[idx];

  ServoResult r;
  r.armAngleDeg = armAngleDeg;
  r.pulseUS     = constrain(us, SERVO_MIN_US, SERVO_MAX_US);
  return r;
}

// ── Move platform ──────────────────────────────────────────────────
// NOTE: no constrain on magDeg here — let computeServo handle clamping
// so EXPEL_DEG passes through correctly
void movePlatform(float dirDeg, float magDeg) {
  currentDir = dirDeg;
  currentMag = magDeg;
  for (uint8_t i = 0; i < 3; i++) {
    ServoResult r = computeServo(i, dirDeg, magDeg);
    setServoUS(SERVO_CH[i], r.pulseUS);
  }
}

// ── Level platform ─────────────────────────────────────────────────
void levelPlatform() {
  movePlatform(0.0f, 0.0f);
  Serial.println(F(">> Platform levelled"));
}

// ── Tilt toward a bin ──────────────────────────────────────────────
void tiltToBin(uint8_t binIdx) {
  if (binIdx > 2) return;
  Serial.print(F(">> Tilting toward bin "));
  Serial.print((char)('A' + binIdx));
  Serial.print(F(" ("));
  Serial.print(BIN_NAME[binIdx]);
  Serial.println(F(")"));
  movePlatform(BIN_DIR[binIdx], EXPEL_DEG);
}

// ── Demo sweep ─────────────────────────────────────────────────────
void sweepDemo() {
  Serial.println(F(">> Sweep demo starting..."));
  for (uint8_t i = 0; i < 3; i++) {
    tiltToBin(i);
    printStatus();
    delay(1500);
    levelPlatform();
    delay(800);
  }
  Serial.println(F(">> Sweep complete"));
}

// ── Print status ───────────────────────────────────────────────────
void printStatus() {
  Serial.println(F("────────────────────────────────────────"));
  Serial.print(F("  Tilt direction : ")); Serial.print(currentDir, 1); Serial.println(F("°"));
  Serial.print(F("  Tilt magnitude : ")); Serial.print(currentMag, 1); Serial.println(F("°"));

  bool expelling = currentMag >= EXPEL_DEG;
  Serial.print(F("  State          : "));
  if (currentMag == 0.0f)  Serial.println(F("Level"));
  else if (!expelling)     Serial.println(F("Holding"));
  else {
    Serial.print(F("Expelling → "));
    float minDiff = 999.0f;
    uint8_t closest = 0;
    for (uint8_t i = 0; i < 3; i++) {
      float d = fabsf(currentDir - BIN_DIR[i]);
      while (d > 360.0f) d -= 360.0f;
      float diff = fabsf(d - 180.0f);
      if (diff < minDiff) { minDiff = diff; closest = i; }
    }
    Serial.println(BIN_NAME[closest]);
  }

  Serial.println(F("  Servo positions:"));
  for (uint8_t i = 0; i < 3; i++) {
    ServoResult r = computeServo(i, currentDir, currentMag);
    Serial.print(F("    Servo "));
    Serial.print((char)('A' + i));
    Serial.print(F(" (ch")); Serial.print(SERVO_CH[i]); Serial.print(F("): "));
    Serial.print(r.armAngleDeg, 1);
    Serial.print(F("°  →  "));
    Serial.print(r.pulseUS);
    Serial.print(F(" µs  (trim: "));
    Serial.print(trimUS[i]);
    Serial.println(F(" µs)"));
  }
  Serial.println(F("────────────────────────────────────────"));
}

// ── Print menu ─────────────────────────────────────────────────────
void printMenu() {
  Serial.println(F("\n═══════════════════════════════════════════"));
  Serial.println(F("  STEWART IK — command menu"));
  Serial.println(F("═══════════════════════════════════════════"));
  Serial.println(F("  level              — level the platform"));
  Serial.println(F("  tilt <dir> <mag>   — e.g. 'tilt 120 18.5'"));
  Serial.println(F("  bin a              — expel → recycling"));
  Serial.println(F("  bin b              — expel → landfill"));
  Serial.println(F("  bin c              — expel → compost"));
  Serial.println(F("  sweep              — demo all three bins"));
  Serial.println(F("  status             — print current state"));
  Serial.println(F("  trim <a> <b> <c>   — e.g. 'trim 0 -40 -80'"));
  Serial.println(F("  m                  — show this menu"));
  Serial.println(F("═══════════════════════════════════════════"));
  Serial.print(F("  EXPEL_DEG  : ")); Serial.println(EXPEL_DEG);
  Serial.print(F("  MAX_TILT   : ")); Serial.println(MAX_TILT_DEG);
  Serial.print(F("  BOWL_OFFSET: ")); Serial.println(BOWL_OFFSET);
  Serial.println(F("═══════════════════════════════════════════\n"));
}

// ── Handle serial command ──────────────────────────────────────────
void handleCommand(String cmd) {
  cmd.trim();

  if (cmd == "level") {
    levelPlatform();
    printStatus();
  }
  else if (cmd.startsWith("tilt ")) {
    String args = cmd.substring(5);
    int spaceIdx = args.indexOf(' ');
    if (spaceIdx < 0) {
      Serial.println(F("Usage: tilt <dir_deg> <mag_deg>"));
      return;
    }
    float dir = args.substring(0, spaceIdx).toFloat();
    float mag = args.substring(spaceIdx + 1).toFloat();
    movePlatform(dir, mag);
    printStatus();
  }
  else if (cmd == "bin a" || cmd == "bin A") { tiltToBin(0); printStatus(); }
  else if (cmd == "bin b" || cmd == "bin B") { tiltToBin(1); printStatus(); }
  else if (cmd == "bin c" || cmd == "bin C") { tiltToBin(2); printStatus(); }
  else if (cmd == "sweep")  { sweepDemo(); }
  else if (cmd == "status") { printStatus(); }
  else if (cmd == "m")      { printMenu(); }
  else if (cmd.startsWith("trim ")) {
    String args = cmd.substring(5);
    int s1 = args.indexOf(' ');
    int s2 = args.indexOf(' ', s1 + 1);
    if (s1 < 0 || s2 < 0) {
      Serial.println(F("Usage: trim <A_us> <B_us> <C_us>"));
      return;
    }
    trimUS[0] = args.substring(0, s1).toInt();
    trimUS[1] = args.substring(s1 + 1, s2).toInt();
    trimUS[2] = args.substring(s2 + 1).toInt();
    Serial.print(F(">> Trims → A=")); Serial.print(trimUS[0]);
    Serial.print(F(" B="));          Serial.print(trimUS[1]);
    Serial.print(F(" C="));          Serial.println(trimUS[2]);
    movePlatform(currentDir, currentMag);
    printStatus();
  }
  else {
    Serial.print(F("Unknown: ")); Serial.println(cmd);
    Serial.println(F("Type 'm' for menu."));
  }
}

// ══════════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("\nStarting PCA9685..."));
  pca.begin();
  pca.setOscillatorFrequency(27000000);
  pca.setPWMFreq(PWM_FREQ);
  delay(10);

  Serial.println(F("PCA9685 ready."));
  levelPlatform();
  delay(500);
  printStatus();
  printMenu();
}

// ══════════════════════════════════════════════════════════════════
void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    handleCommand(cmd);
  }
}
