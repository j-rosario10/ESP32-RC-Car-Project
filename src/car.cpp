#include "car.h"

#include <Wire.h>
#include <PCA9685.h>

namespace Car {

// ==== PCA9685 + wiring config (from Freenove) ====
static constexpr int PCA9685_SDA      = 13;
static constexpr int PCA9685_SCL      = 14;
static constexpr uint8_t PCA9685_ADDR = 0x5F;
static constexpr int SERVO_FREQUENCY  = 50;   // Hz

// Motor speed range (same as header)
static constexpr int SPEED_MIN = MOTOR_SPEED_MIN;
static constexpr int SPEED_MAX = MOTOR_SPEED_MAX;

// PCA9685 channel mapping for motors (from Freenove_4WD_Car_For_ESP32.cpp)
// These numbers are PCA9685 output channels, NOT ESP32 GPIO.
static constexpr uint8_t PIN_MOTOR_M1_IN1 = 15;
static constexpr uint8_t PIN_MOTOR_M1_IN2 = 14;
static constexpr uint8_t PIN_MOTOR_M2_IN1 = 9;
static constexpr uint8_t PIN_MOTOR_M2_IN2 = 8;
static constexpr uint8_t PIN_MOTOR_M3_IN1 = 12;
static constexpr uint8_t PIN_MOTOR_M3_IN2 = 13;
static constexpr uint8_t PIN_MOTOR_M4_IN1 = 10;
static constexpr uint8_t PIN_MOTOR_M4_IN2 = 11;

// Direction (1 or -1) in case you want to flip later
static constexpr int MOTOR_1_DIRECTION = 1;
static constexpr int MOTOR_2_DIRECTION = 1;
static constexpr int MOTOR_3_DIRECTION = 1;
static constexpr int MOTOR_4_DIRECTION = 1;

static PCA9685 pca9685;

static int clampSpeed(int s) {
    if (s < SPEED_MIN) return SPEED_MIN;
    if (s > SPEED_MAX) return SPEED_MAX;
    return s;
}

// Low-level: set a single motor via its two PCA9685 channels
static void setMotorChannels(uint8_t ch_in1, uint8_t ch_in2, int speed) {
    speed = clampSpeed(speed);

    if (speed >= 0) {
        // positive direction: IN1 = speed, IN2 = 0
        pca9685.setChannelPulseWidth(ch_in1, speed);
        pca9685.setChannelPulseWidth(ch_in2, 0);
    } else {
        // negative direction: IN1 = 0, IN2 = |speed|
        speed = -speed;
        pca9685.setChannelPulseWidth(ch_in1, 0);
        pca9685.setChannelPulseWidth(ch_in2, speed);
    }
}

void begin() {
    // Initialize I2C on the same pins as Freenove
    Wire.begin(PCA9685_SDA, PCA9685_SCL);

    // Set MODE1 to 0x00 (normal mode), like Freenove does
    Wire.beginTransmission(PCA9685_ADDR);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.endTransmission();

    // Initialize the PCA9685 driver
    pca9685.setupSingleDevice(Wire, PCA9685_ADDR);
    pca9685.setToFrequency(SERVO_FREQUENCY);

    stop();
}

void move(int m1, int m2, int m3, int m4) {
    // Apply motor direction constants + clamp
    m1 = MOTOR_1_DIRECTION * clampSpeed(m1);
    m2 = MOTOR_2_DIRECTION * clampSpeed(m2);
    m3 = MOTOR_3_DIRECTION * clampSpeed(m3);
    m4 = MOTOR_4_DIRECTION * clampSpeed(m4);

    // Use same pattern as Freenove's Motor_Move
    setMotorChannels(PIN_MOTOR_M1_IN1, PIN_MOTOR_M1_IN2, m1);
    setMotorChannels(PIN_MOTOR_M2_IN1, PIN_MOTOR_M2_IN2, m2);
    setMotorChannels(PIN_MOTOR_M3_IN1, PIN_MOTOR_M3_IN2, m3);
    setMotorChannels(PIN_MOTOR_M4_IN1, PIN_MOTOR_M4_IN2, m4);
}

void stop() {
    move(0, 0, 0, 0);
}

void forward(int speed) {
    speed = clampSpeed(speed);
    move(speed, speed, speed, speed);
}

void backward(int speed) {
    speed = clampSpeed(speed);
    move(-speed, -speed, -speed, -speed);
}

void turnLeft(int speed) {
    speed = clampSpeed(speed);
    move(-speed, -speed, speed, speed);
}

void turnRight(int speed) {
    speed = clampSpeed(speed);
    move(speed, speed, -speed, -speed);
}

} // namespace Car
