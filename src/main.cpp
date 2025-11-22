#include <Arduino.h>
#include "car.h"

const int BUZZER_PIN = 2;

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // keep buzzer off

    Serial.println("Car.begin()...");
    Car::begin();
    Serial.println("Car ready, starting test...");
}

void loop() {
    int speed = 2000; // between 0 and 4095

    Serial.println("Forward");
    Car::forward(speed);
    delay(1500);

    Serial.println("Stop");
    Car::stop();
    delay(800);

    Serial.println("Backward");
    Car::backward(speed);
    delay(1500);

    Serial.println("Stop");
    Car::stop();
    delay(800);

    Serial.println("Turn Left");
    Car::turnLeft(speed);
    delay(1200);

    Serial.println("Stop");
    Car::stop();
    delay(800);

    Serial.println("Turn Right");
    Car::turnRight(speed);
    delay(1200);

    Serial.println("Stop");
    Car::stop();
    delay(2000);
}
