// BMW M2 RC car - ESP32 firmware
//
// Sections in order:
//   1  config     temp numbers
//   2  motors     talking to the motor chip, including braking
//   3  sensors    gyro and battery current
//   4  regen      the fast switching that charges the battery while braking
//   5  control    the loop that runs 200 times a second and does the driving
//   6  web        the Wi-Fi network and the web page
//
// The big idea: the browser does NOT drive the car. It only reports which
// keys you're holding. All the actual decisions happen in section 5, on the
// ESP32. That's because Wi-Fi delay is unpredictable - a message might take
// 5 milliseconds or 200 - and anything automatic needs steady timing.

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PCA9685.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <INA226.h>

#include "page.h"

// ===========================================================================
// 1  CONFIG
// ===========================================================================

// These two pins are fixed by the Freenove board, so don't change them.
// All three chips share the same two wires and take turns talking.
static constexpr int I2C_SDA = 13;
static constexpr int I2C_SCL = 14;
static constexpr uint32_t I2C_CLOCK_HZ = 400000;   // 4x the default speed,
                                                   // because chatting to the
                                                   // chips is the slowest part
                                                   // of each loop

// Each chip has its own address so the ESP32 knows who it's talking to.
static constexpr uint8_t ADDR_PCA9685 = 0x5F;
static constexpr uint8_t ADDR_MPU6050 = 0x68;
static constexpr uint8_t ADDR_INA226  = 0x40;

// The kit sets this to 50 because it shares the chip with steering servos.
// We have no servos, so we can turn it right up. Smoother, and free.
static constexpr int MOTOR_PWM_FREQ = 1000;
static constexpr int MOTOR_MAX = 4095;             // full speed

// Don't use pins 0, 2, 12 or 15 - the ESP32 checks those when it starts up
// and pulling them the wrong way stops it booting.
static constexpr int REGEN_IN1_PIN = 25;
static constexpr int REGEN_IN2_PIN = 26;
static constexpr int REGEN_PWM_FREQ = 20000;       // 20,000 switches a second.
                                                   // Too fast to hear, and fast
                                                   // enough for the pumping
                                                   // trick to work.
static constexpr int REGEN_PWM_BITS = 10;

// How many times a second the control loop runs. 200 was chosen because it's
// as fast as we can go before talking to the chips takes too long.
static constexpr int   LOOP_HZ = 200;
static constexpr float LOOP_DT = 1.0f / LOOP_HZ;   // 5 milliseconds
static constexpr uint32_t INPUT_TIMEOUT_MS = 300;  // silence this long = stop

// A key is only on or off, but steering is an amount. So we use TIME as the
// missing information: tap for a little, hold for a lot.
// Letting go is faster than holding, so the car straightens up promptly.
static constexpr float STEER_RISE_RATE = 2.0f;     // half a second to full lock
static constexpr float STEER_FALL_RATE = 4.0f;
static constexpr float THROTTLE_RISE_RATE = 1.5f;
static constexpr float THROTTLE_FALL_RATE = 2.5f;
static constexpr float BRAKE_RISE_RATE = 3.0f;
static constexpr float BRAKE_FALL_RATE = 6.0f;

// Drift mode just lets the steering build up further than normal.
static constexpr float STEER_LIMIT_NORMAL = 0.60f;
static constexpr float STEER_LIMIT_DRIFT  = 1.00f;

// How fast the car should spin around at full steering and full speed,
// in degrees per second. Found by driving it and seeing what it can do.
static constexpr float YAW_TARGET_GAIN = 180.0f;

// How hard the car corrects itself. START AT 0 and raise it slowly. When the
// car starts weaving side to side on a straight, that's too much - back off.
static constexpr float YAW_KP = 0.004f;

// The most the car is allowed to correct. Without this, on a slippery floor
// it would push harder and harder and just spin on the spot.
static constexpr float YAW_CORRECTION_LIMIT = 0.45f;

// Whether "turning right" reads as a positive or negative number depends on
// which way up you mounted the gyro. Run the test in setup(), and if it's
// backwards change this to -1.0f. Easier than remounting the chip.
static constexpr float GYRO_Z_SIGN = 1.0f;

// Anything smaller than this is just the motors shaking the car, not the car
// actually turning. Ignoring it stops the controller twitching.
static constexpr float GYRO_DEADBAND_DPS = 1.5f;

// There are no encoders on this car, so we can't measure speed. Instead we
// guess: assume the car catches up to the throttle with a delay. It's wrong
// if the wheels spin, but we only need a rough fast-or-slow.
static constexpr float SPEED_RISE_TAU = 0.8f;      // seconds to get up to speed
static constexpr float SPEED_FALL_TAU = 1.2f;      // coasts down slower
static constexpr float SPEED_BRAKE_TAU = 0.4f;     // braking drops it fast

// Two lithium cells fully charged sit at 8.4V. A full battery has no room for
// more charge, so we stop regen before we get there. Real electric cars do
// the same thing - it's why regen feels weak right after charging.
static constexpr float PACK_REGEN_CUTOFF_VOLTS = 8.30f;

// CHECK YOUR BOARD. Lots of cheap current sensors come with a 0.1 ohm shunt
// rated for under 1 amp, and four motors will cook it. You want 0.01 or less.
// If this number doesn't match your board, every reading is wrong.
static constexpr float SHUNT_OHMS = 0.010f;
static constexpr float SHUNT_MAX_AMPS = 8.0f;

// The car makes its own Wi-Fi network, so it works anywhere with no router.
static const char* AP_SSID = "ESP32-RC-Car";
static const char* AP_PASS = "";

// ===========================================================================
// 2  MOTORS
// ===========================================================================

// These are channel numbers on the PCA9685 chip, NOT ESP32 pins. Easy to mix
// up. Each motor uses two channels, because the driver needs two signals to
// know both how fast to go and which direction.
static constexpr uint8_t M1_IN1 = 15, M1_IN2 = 14;
static constexpr uint8_t M2_IN1 = 9,  M2_IN2 = 8;
static constexpr uint8_t M3_IN1 = 12, M3_IN2 = 13;
static constexpr uint8_t M4_IN1 = 10, M4_IN2 = 11;

// If a wheel spins the wrong way after you rewire something, flip its
// number to -1 here instead of changing any logic.
static constexpr int M1_DIR = 1, M2_DIR = 1, M3_DIR = 1, M4_DIR = 1;

static PCA9685 pca;

// Remembers what we last sent to each channel. Talking to the motor chip is
// slow, and most of the time only a couple of channels actually change, so
// skipping the rest gives us back a big chunk of each loop.
static int lastWritten[16];

static int clampMotor(int s) {
    if (s < -MOTOR_MAX) return -MOTOR_MAX;
    if (s >  MOTOR_MAX) return  MOTOR_MAX;
    return s;
}

static void writeChannel(uint8_t ch, int value) {
    if (lastWritten[ch] == value) return;      // nothing changed, skip it
    pca.setChannelPulseWidth(ch, value);
    lastWritten[ch] = value;
}

// The motor driver has four possible states, and the original code only ever
// used three of them:
//
//   speed / 0        forward
//   0 / speed        reverse
//   0 / 0            coast - motor spins freely
//   high / high      BRAKE - the two motor wires get joined together
//
// That last one is the whole trick behind motor braking. You don't connect
// any wires yourself. The switches are already inside the chip - the old code
// just never asked for them.
static void driveChannels(uint8_t in1, uint8_t in2, int speed) {
    speed = clampMotor(speed);

    if (speed >= 0) { writeChannel(in1, speed);  writeChannel(in2, 0); }
    else            { writeChannel(in1, 0);      writeChannel(in2, -speed); }
}

// Sending the same value to both inputs joins the motor wires together.
// At full value it's permanently joined and brakes hard. At half, it spends
// half its time joined and half coasting, which gives about half the braking.
// That's how you get a brake with pressure levels out of an on/off chip.
static void brakeChannels(uint8_t in1, uint8_t in2, int strength) {
    if (strength < 0) strength = -strength;
    if (strength > MOTOR_MAX) strength = MOTOR_MAX;

    writeChannel(in1, strength);
    writeChannel(in2, strength);
}

// m1 and m2 are the right side. m3 and m4 are the left side.
static void motorsMove(int m1, int m2, int m3, int m4) {
    driveChannels(M1_IN1, M1_IN2, M1_DIR * clampMotor(m1));
    driveChannels(M2_IN1, M2_IN2, M2_DIR * clampMotor(m2));
    driveChannels(M3_IN1, M3_IN2, M3_DIR * clampMotor(m3));
    driveChannels(M4_IN1, M4_IN2, M4_DIR * clampMotor(m4));
}

static void motorsBrake(int strength) {
    brakeChannels(M1_IN1, M1_IN2, strength);
    brakeChannels(M2_IN1, M2_IN2, strength);
    brakeChannels(M3_IN1, M3_IN2, strength);
    brakeChannels(M4_IN1, M4_IN2, strength);
}

static void motorsBegin() {
    for (int i = 0; i < 16; i++) lastWritten[i] = -1;   // force the first write

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_CLOCK_HZ);

    // The motor chip powers up asleep. This wakes it. The library is supposed
    // to do this but it doesn't always work, so we do it by hand first.
    Wire.beginTransmission(ADDR_PCA9685);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();

    pca.setupSingleDevice(Wire, ADDR_PCA9685);
    pca.setToFrequency(MOTOR_PWM_FREQ);

    motorsMove(0, 0, 0, 0);
}

// ===========================================================================
// 3  SENSORS
// ===========================================================================
// Both sensors are optional. If one isn't plugged in, the car still drives -
// it just turns off the feature that needed it, instead of refusing to start.

static Adafruit_MPU6050 mpu;
static INA226 ina(ADDR_INA226, &Wire);

static bool gyroOK = false, inaOK = false;
static float gyroZeroOffset = 0.0f;     // the gyro's own built-in error
static float yawRateDps = 0.0f;         // how fast the car is turning
static float packV = 0.0f, packA = 0.0f, packW = 0.0f;
static float joulesOut = 0.0f, joulesIn = 0.0f;

// We only read the current sensor every 10th loop, so 20 times a second.
// Reading it is slow, and battery current doesn't change much in 5ms anyway.
static uint8_t inaSkip = 0;
static uint32_t lastEnergyUs = 0;

static void sensorsBegin() {
    // The two wires are already switched on by motorsBegin(), so we just
    // join the conversation. That's why the order in setup() matters.

    if (mpu.begin(ADDR_MPU6050, &Wire)) {
        gyroOK = true;

        // The narrowest range the chip offers. The car never turns faster
        // than this, and a narrow range means finer detail on small numbers -
        // which is exactly what the controller reacts to.
        mpu.setGyroRange(MPU6050_RANGE_250_DEG);
        mpu.setAccelerometerRange(MPU6050_RANGE_4_G);

        // A smoothing filter inside the chip. 21Hz is the sweet spot: enough
        // to hide motor vibration, not so much that readings arrive late.
        // Late readings make the controller unstable.
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

        Serial.println("[sensors] MPU6050 found");
    } else {
        Serial.println("[sensors] no MPU6050 - torque vectoring disabled");
    }

    if (ina.begin()) {
        inaOK = true;
        ina.setMaxCurrentShunt(SHUNT_MAX_AMPS, SHUNT_OHMS);
        Serial.println("[sensors] INA226 found");
    } else {
        Serial.println("[sensors] no INA226 - regen will run blind");
    }

    lastEnergyUs = micros();
}

// Every gyro reports a small wrong number even when perfectly still, and it's
// different for every chip. So we sit still, take 500 readings, average them,
// and subtract that from everything afterwards.
//
// One reading isn't enough because the noise is about the same size as the
// error we're trying to find. Averaging cancels the noise out.
//
// THE CAR MUST BE STILL while this runs.
static void calibrateGyro(uint16_t samples = 500) {
    if (!gyroOK) return;

    Serial.println("[sensors] calibrating - keep the car still");

    double sum = 0.0;
    for (uint16_t i = 0; i < samples; i++) {
        sensors_event_t a, g, t;
        mpu.getEvent(&a, &g, &t);
        sum += g.gyro.z;
        delay(3);
    }

    // The library gives radians per second; we want degrees per second.
    gyroZeroOffset = (float)(sum / samples) * 57.2958f;

    Serial.print("[sensors] gyro offset: ");
    Serial.print(gyroZeroOffset, 3);
    Serial.println(" deg/s");
}

static void sensorsUpdate() {
    if (gyroOK) {
        sensors_event_t a, g, t;
        mpu.getEvent(&a, &g, &t);

        float dps = (g.gyro.z * 57.2958f) - gyroZeroOffset;
        dps *= GYRO_Z_SIGN;

        // Tiny readings are the motors shaking the car, not the car turning.
        if (dps > -GYRO_DEADBAND_DPS && dps < GYRO_DEADBAND_DPS) dps = 0.0f;

        yawRateDps = dps;
    }

    if (inaOK) {
        if (++inaSkip >= 10) {
            inaSkip = 0;
            packV = ina.getBusVoltage();
            packA = ina.getCurrent();      // negative means charging
            packW = packV * packA;
        }

        // Energy is just power multiplied by time, added up as we go.
        // We do this every loop, even though the reading only updates every
        // 10th, so the time part stays accurate.
        uint32_t now = micros();
        float dt = (now - lastEnergyUs) / 1000000.0f;
        lastEnergyUs = now;

        // The microsecond timer wraps around every 70 minutes and gives a
        // nonsense value when it does. This throws that one reading away.
        if (dt > 0.0f && dt < 0.5f) {
            if (packW > 0) joulesOut += packW * dt;
            else           joulesIn  += (-packW) * dt;
        }
    }
}

// Run this once after mounting the gyro. Rotate the car by hand and watch the
// numbers: one direction should be positive, the other negative. If it's
// backwards, change GYRO_Z_SIGN above rather than remounting the chip.
static void gyroSignTest() {
    if (!gyroOK) { Serial.println("[test] no gyro"); return; }

    Serial.println("[test] rotate the car by hand for 10s");

    uint32_t until = millis() + 10000;
    while (millis() < until) {
        sensorsUpdate();
        Serial.print("yaw: ");
        Serial.print(yawRateDps, 1);
        Serial.println(" deg/s");
        delay(100);
    }
}

// ===========================================================================
// 4  REGEN
// ===========================================================================
// This section exists because the motor chip is too slow. It can switch about
// 1,500 times a second. Regen needs about 20,000. The ESP32 itself can do
// that easily, so one motor gets its own driver board wired straight to two
// ESP32 pins, skipping the slow chip entirely.
//
// How the charging works:
//
//   Both pins HIGH -> the motor's two wires are joined together. Electricity
//                     builds up inside the motor's coil. The battery isn't
//                     part of this at all yet.
//
//   Both pins LOW  -> the connection breaks. That built-up electricity has
//                     nowhere to go, so it forces itself out at a higher
//                     voltage - high enough to reach the battery.
//
// Build, release, build, release, 20,000 times a second. It's a pump. How
// much of each cycle is spent building is how hard you brake.
//
// Both pins get the exact same signal, which is why this is only a few lines.

static constexpr int REGEN_CH1 = 4;    // channels 0-3 are often used by other
static constexpr int REGEN_CH2 = 5;    // libraries, so we start at 4
static constexpr int REGEN_DUTY_MAX = (1 << REGEN_PWM_BITS) - 1;

static void regenWrite(int duty) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(REGEN_IN1_PIN, duty);
    ledcWrite(REGEN_IN2_PIN, duty);
#else
    ledcWrite(REGEN_CH1, duty);
    ledcWrite(REGEN_CH2, duty);
#endif
}

// duty is 0 to 1, and more is NOT always better:
//   0.0   coasting, nothing happens
//   0.3   light braking, and most of the energy reaches the battery
//   1.0   the motor stays joined the whole time, so it brakes hard but the
//         electricity never gets released - all drag, no charge
// The useful range is about 0.2 to 0.7, which is why the controller caps it.
static void regenSetDuty(float duty) {
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    regenWrite((int)(duty * REGEN_DUTY_MAX));
}

static void regenCoast() { regenWrite(0); }

static void regenBegin() {
    // Newer versions of the ESP32 software renamed these functions, so we
    // handle both. Saves you a confusing error if you update.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(REGEN_IN1_PIN, REGEN_PWM_FREQ, REGEN_PWM_BITS);
    ledcAttach(REGEN_IN2_PIN, REGEN_PWM_FREQ, REGEN_PWM_BITS);
#else
    ledcSetup(REGEN_CH1, REGEN_PWM_FREQ, REGEN_PWM_BITS);
    ledcSetup(REGEN_CH2, REGEN_PWM_FREQ, REGEN_PWM_BITS);
    ledcAttachPin(REGEN_IN1_PIN, REGEN_CH1);
    ledcAttachPin(REGEN_IN2_PIN, REGEN_CH2);
#endif
    regenCoast();
}

// ===========================================================================
// 5  CONTROL
// ===========================================================================

// What the browser sends us: which keys are held down. Not motor commands.
// The old version had the browser do the maths and send finished numbers,
// which meant the driving decisions were being made on the far side of a
// Wi-Fi connection. Fine for a person pressing keys, useless for anything
// that has to react to the car.
struct Input {
    bool w = false, a = false, s = false, d = false, drift = false;
    uint32_t lastSeenMs = 0;
};

// Everything the loop works out gets published here, not just what the
// dashboard shows. Being able to log what the car wanted versus what it did
// is how you tune the controller and how you measure the regen.
struct Telemetry {
    float steer, throttle, brake, speedEst;
    float yawTarget, yawActual, yawError, correction;
    float volts, amps, watts, joulesOut, joulesIn;
    uint32_t overruns;
    bool failsafe;
};

// Two parts of the program touch these at the same time, so they need
// protecting or you get half-updated values. This is the quick kind of lock -
// the web page must never be able to hold up the motors.
static portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
static Input     sharedInput;
static Telemetry sharedTelem;

// Only the control loop touches these, so they don't need protecting.
static float steer = 0.0f, throttle = 0.0f, brake = 0.0f, speedEst = 0.0f;
static uint32_t overrunCount = 0;

// Moves a value a little bit toward a target, without overshooting it.
static float rampToward(float value, float target, float rate, float dt) {
    float step = rate * dt;

    if (value < target)      { value += step; if (value > target) value = target; }
    else if (value > target) { value -= step; if (value < target) value = target; }
    return value;
}

// HOLD TO BUILD
// A key is on or off, but steering is an amount. So we use how long you hold
// it: tap for a little, hold for a lot.
//
// This has to happen here on the car, not in the browser. If the browser did
// the ramping, every step of it would have to travel over Wi-Fi, and the ramp
// would end up as jerky as the connection.
static void updateInputs(const Input& in) {
    float limit = in.drift ? STEER_LIMIT_DRIFT : STEER_LIMIT_NORMAL;

    // Letting go fades faster than holding builds, so the car straightens up
    // quickly instead of carrying steering into whatever you do next.
    if (in.a && !in.d)      steer = rampToward(steer, -limit, STEER_RISE_RATE, LOOP_DT);
    else if (in.d && !in.a) steer = rampToward(steer,  limit, STEER_RISE_RATE, LOOP_DT);
    else                    steer = rampToward(steer,  0.0f,  STEER_FALL_RATE, LOOP_DT);

    // S is a proper brake now. It used to subtract from throttle, which meant
    // pressing brake actually commanded reverse - spending battery power to
    // fight the motors instead of taking power out of them.
    throttle = in.w ? rampToward(throttle, 1.0f, THROTTLE_RISE_RATE, LOOP_DT)
                    : rampToward(throttle, 0.0f, THROTTLE_FALL_RATE, LOOP_DT);

    brake = in.s ? rampToward(brake, 1.0f, BRAKE_RISE_RATE, LOOP_DT)
                 : rampToward(brake, 0.0f, BRAKE_FALL_RATE, LOOP_DT);
}

// SPEED GUESS
// This is a guess, not a measurement - there are no encoders on the wheels.
// We assume the car catches up to whatever throttle you asked for, with a
// delay. It's wrong if the wheels are spinning, but the steering maths below
// only needs a rough idea of fast or slow.
static void updateSpeedEstimate() {
    float target = fabsf(throttle);
    float tau;                        // how many seconds to catch up

    if (brake > 0.05f)          tau = SPEED_BRAKE_TAU;
    else if (target > speedEst) tau = SPEED_RISE_TAU;
    else                        tau = SPEED_FALL_TAU;

    if (brake > 0.05f) target = 0.0f;

    // Move part of the way toward the target each loop. Small tau means it
    // catches up quickly.
    float alpha = LOOP_DT / (tau + LOOP_DT);
    speedEst += (target - speedEst) * alpha;

    if (speedEst < 0.0f) speedEst = 0.0f;
    if (speedEst > 1.0f) speedEst = 1.0f;
}

// TORQUE VECTORING
// The gyro only tells us how fast the car IS turning. It knows nothing about
// what you wanted. So we work out the target ourselves:
//
//     target = steering x speed x gain
//
// Speed is in there because it matters as much as steering. Hold the same
// steering at walking pace and the car barely turns; hold it at speed and it
// swings around fast.
//
// Then we subtract:
//     target higher than actual -> not turning enough -> push the OUTSIDE wheels
//     target lower than actual  -> turning too much   -> push the INSIDE wheels
//
// Returns how much power to shift from one side to the other.
static float torqueVectoring(float& tgt, float& act, float& err) {
    tgt = steer * speedEst * YAW_TARGET_GAIN;
    act = yawRateDps;
    err = tgt - act;

    // No gyro means no way to know what the car is doing, so fall back to the
    // simple old behaviour instead of failing.
    if (!gyroOK) { tgt = act = err = 0.0f; return steer; }

    // Bigger gap, bigger correction. That's the whole controller.
    float correction = YAW_KP * err;

    // On a slippery floor the gap never closes no matter how hard we push, so
    // without this limit the correction grows until the car is commanding
    // full opposite lock and just spinning. The limit means the worst case is
    // "the help stops helping" rather than "the help makes it worse".
    if (correction >  YAW_CORRECTION_LIMIT) correction =  YAW_CORRECTION_LIMIT;
    if (correction < -YAW_CORRECTION_LIMIT) correction = -YAW_CORRECTION_LIMIT;

    // Your steering is still the main input. This only nudges it.
    return steer + correction;
}

// BRAKING
// Three of the motors are on the slow chip and can only brake by turning the
// energy into heat. The fourth is on the fast driver and can actually put
// energy back in the battery. That split is a hardware limit, not a choice -
// more driver boards would mean more wheels recovering.
static void applyBraking() {
    motorsBrake((int)(brake * MOTOR_MAX));

    // A full battery has no room for more charge, and pushing anyway makes
    // the voltage climb. So check before we charge.
    bool canAccept = !(inaOK && packV > PACK_REGEN_CUTOFF_VOLTS);

    if (canAccept) regenSetDuty(brake * 0.7f);   // capped, see regenSetDuty
    else           regenSetDuty(brake > 0.5f ? 1.0f : 0.0f);  // brake as heat
}

// THE LOOP
static void controlTask(void* param) {
    // vTaskDelayUntil waits until a specific moment, so the loop runs at a
    // steady rate. A plain delay() would drift, because it doesn't count the
    // time the work itself took.
    TickType_t lastWake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000 / LOOP_HZ);

    for (;;) {
        uint32_t cycleStart = micros();

        // --- 1. get the latest key presses ---
        Input in;
        portENTER_CRITICAL(&stateMux);
        in = sharedInput;
        portEXIT_CRITICAL(&stateMux);

        // --- 2. failsafe ---
        // We check for silence rather than waiting to be told goodbye,
        // because the ways you actually lose control are all silent: phone
        // locks, tab closes, you walk out of range. None of those send a
        // message saying so.
        bool failsafe = (millis() - in.lastSeenMs) > INPUT_TIMEOUT_MS;
        if (failsafe) {
            in = Input();
            steer = throttle = brake = 0.0f;
            motorsBrake(MOTOR_MAX / 2);      // stop it, don't just let it roll
            regenCoast();
        }

        // --- 3. read the sensors ---
        sensorsUpdate();

        // --- 4. work out what the driver is asking for ---
        if (!failsafe) updateInputs(in);
        updateSpeedEstimate();

        // --- 5. drive or brake ---
        float yawTgt = 0, yawAct = 0, yawErr = 0, mix = 0;

        if (!failsafe) {
            if (brake > 0.02f) {
                // Braking always wins. Doing both at once properly would need
                // something that decides how to split the limited grip
                // between slowing down and turning, and this car doesn't have
                // that yet. It's the next thing to build.
                applyBraking();
            } else {
                regenCoast();

                mix = torqueVectoring(yawTgt, yawAct, yawErr);

                // A positive mix means turn right, so the left wheels get the
                // bigger number.
                int l = (int)((throttle + mix) * MOTOR_MAX);
                int r = (int)((throttle - mix) * MOTOR_MAX);

                motorsMove(r, r, l, l);      // m1,m2 right; m3,m4 left
            }
        }

        // --- 6. publish everything for the dashboard ---
        Telemetry t;
        t.steer = steer;   t.throttle = throttle;  t.brake = brake;
        t.speedEst = speedEst;
        t.yawTarget = yawTgt; t.yawActual = yawAct; t.yawError = yawErr;
        t.correction = mix - steer;
        t.volts = packV;   t.amps = packA;         t.watts = packW;
        t.joulesOut = joulesOut; t.joulesIn = joulesIn;
        t.overruns = overrunCount;
        t.failsafe = failsafe;

        portENTER_CRITICAL(&stateMux);
        sharedTelem = t;
        portEXIT_CRITICAL(&stateMux);

        // --- 7. did we finish in time? ---
        // If this number keeps climbing while driving, the loop is doing more
        // work than 5ms allows. Usually that's too much chip chatter - drop
        // LOOP_HZ to 100. Worth measuring rather than assuming, because a
        // late loop still looks fine from the outside.
        if (micros() - cycleStart > (1000000UL / LOOP_HZ)) overrunCount++;

        vTaskDelayUntil(&lastWake, period);
    }
}

// ===========================================================================
// 6  WEB
// ===========================================================================

static WebServer server(80);

static void handleRoot() {
    server.send_P(200, "text/html", PAGE_HTML);
}

// This replaced the old /drive?left=&right= address. That one received motor
// commands the browser had already worked out. This one just receives which
// keys are down and lets the ESP32 do the thinking.
static void handleInput() {
    portENTER_CRITICAL(&stateMux);
    sharedInput.w     = server.arg("w")     == "1";
    sharedInput.a     = server.arg("a")     == "1";
    sharedInput.s     = server.arg("s")     == "1";
    sharedInput.d     = server.arg("d")     == "1";
    sharedInput.drift = server.arg("drift") == "1";
    sharedInput.lastSeenMs = millis();       // proof we're still connected
    portEXIT_CRITICAL(&stateMux);

    server.send(200, "text/plain", "ok");
}

static void handleTelemetry() {
    Telemetry t;
    portENTER_CRITICAL(&stateMux);
    t = sharedTelem;
    portEXIT_CRITICAL(&stateMux);

    // Building the text by hand instead of using a JSON library. This runs 10
    // times a second and a library would be doing memory allocation each time.
    char buf[420];
    snprintf(buf, sizeof(buf),
        "{\"steer\":%.3f,\"throttle\":%.3f,\"brake\":%.3f,\"speedEst\":%.3f,"
        "\"yawTarget\":%.1f,\"yawActual\":%.1f,\"yawError\":%.1f,"
        "\"correction\":%.3f,\"volts\":%.2f,\"amps\":%.3f,\"watts\":%.2f,"
        "\"joulesOut\":%.2f,\"joulesIn\":%.2f,\"overruns\":%lu,"
        "\"failsafe\":%s}",
        t.steer, t.throttle, t.brake, t.speedEst,
        t.yawTarget, t.yawActual, t.yawError, t.correction,
        t.volts, t.amps, t.watts, t.joulesOut, t.joulesIn,
        (unsigned long)t.overruns, t.failsafe ? "true" : "false");

    server.send(200, "application/json", buf);
}

// Resets the energy counters so you can do a clean run. Drive a lap, hit
// this, drive the same lap again with something changed, compare the numbers.
// That comparison is the actual experiment.
static void handleResetEnergy() {
    joulesOut = joulesIn = 0.0f;
    lastEnergyUs = micros();
    server.send(200, "text/plain", "reset");
}

// ===========================================================================

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[boot] BMW M2 RC car");

    // Order matters here: motorsBegin() switches on the two shared wires that
    // the sensors then join.
    motorsBegin();
    regenBegin();
    sensorsBegin();

    // Only works if the car is sitting still.
    calibrateGyro();

    // gyroSignTest();   // uncomment once to check which way round the gyro is

    if (AP_PASS[0] == '\0') WiFi.softAP(AP_SSID);
    else                    WiFi.softAP(AP_SSID, AP_PASS);

    Serial.print("[boot] join \"");
    Serial.print(AP_SSID);
    Serial.print("\" then open http://");
    Serial.println(WiFi.softAPIP());

    server.on("/", handleRoot);
    server.on("/input", handleInput);
    server.on("/telemetry", handleTelemetry);
    server.on("/reset-energy", handleResetEnergy);
    server.begin();

    // The ESP32 has two processors. Wi-Fi runs on the first one and randomly
    // hogs it, which is exactly the unpredictable timing we're trying to
    // escape, so the control loop goes on the second one. Priority 5 puts it
    // above the web server, so a slow web page can never delay the motors.
    xTaskCreatePinnedToCore(controlTask, "control", 4096, nullptr, 5, nullptr, 1);
    Serial.println("[boot] control loop running at 200Hz");
}

void loop() {
    // Web server only. Deliberately no motor code here - the driving happens
    // in the control loop on the other processor at a steady rate, so
    // whatever the web server is busy with can't affect how the car behaves.
    server.handleClient();
}
