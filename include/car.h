#pragma once
#include <Arduino.h>

namespace Car {

// Valid speed range (same as Freenove: -4095 .. +4095)
constexpr int MOTOR_SPEED_MIN = -4095;
constexpr int MOTOR_SPEED_MAX =  4095;

// Call once in setup()
void begin();

// Low-level: set each motor speed directly
// m1..m4 in [-4095, 4095]
void move(int m1, int m2, int m3, int m4);

// Helpers
void stop();
void forward(int speed);
void backward(int speed);
void turnLeft(int speed);
void turnRight(int speed);

} // namespace Car
