# Wiring and bring-up

## The shared bus

GPIO 13 and 14 already run to the PCA9685. The gyro and current sensor join
those same two wires. You are not adding new pins for either sensor.

Each chip has a different address so they don't collide:

| Chip | Address |
|---|---|
| PCA9685 (motors) | 0x5F |
| MPU6050 (gyro) | 0x68 |
| INA226 (current) | 0x40 |

## MPU6050 gyro

| Pin | To |
|---|---|
| VCC | **3.3V** — not 5V |
| GND | GND |
| SDA | GPIO 13 |
| SCL | GPIO 14 |
| AD0 | GND, or leave it |

Mount it flat, near the middle of the car, Z axis pointing up, and screw it
down properly. Foam tape lets it vibrate and motor buzz shows up as fake
rotation in the readings.

**Before trusting it:** uncomment `Sensors::gyroSignTest()` in `setup()`,
flash, and rotate the car by hand. One direction should be consistently
positive, the other consistently negative. If it's backwards, change
`GYRO_Z_SIGN` to `-1.0f` in `config.h`. Don't remount the chip.

## INA226 current sensor

This one goes *in* the power path, not on a header. Cut the positive battery
wire and put the sensor in the gap:

```
Battery + ──→ [IN+]  INA226  [IN−] ──→ board power in
```

IN+ faces the battery, IN− faces the car. Backwards and charging reads as
draining.

| Pin | To |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 13 |
| SCL | GPIO 14 |

**Check your shunt.** Lots of cheap boards ship with a 0.1 ohm shunt rated
under 1A, which four motors will cook. Get 0.01 ohm or lower, then put the
real value in `SHUNT_OHMS` in `config.h`. Wrong value means every reading is
wrong by the same factor, silently.

## DRV8871 regen driver

One motor only. Leave the other three on the PCA9685 so the car still drives
normally while you experiment.

| Pin | To |
|---|---|
| IN1 | GPIO 25 |
| IN2 | GPIO 26 |
| VM | Battery + (downstream of the INA226) |
| GND | GND |
| OUT1 / OUT2 | The motor's two wires |

Unplug that motor from the old driver first. One motor, one driver, never both.

Avoid GPIO 0, 2, 12 and 15 if you change pins — the ESP32 reads those at boot
and pulling them wrong stops it starting. 25, 26, 27, 32 are safe.

## Bring-up order

Flash and test after each step rather than doing it all at once.

1. **Code only, no new parts.** Motor braking and 1000Hz PWM work with the
   hardware you already have. Confirm the car stops hard instead of lurching
   into reverse.
2. **Gyro.** Run the sign test. Then set `YAW_KP = 0` in `config.h` and drive —
   watch target vs actual on the dashboard without any correction happening.
   Only once those numbers look sane, raise KP.
3. **Current sensor.** Watch live watts while driving. Accelerating should be
   a big positive number, coasting near zero. Confirm the sign before you
   trust the energy totals.
4. **Regen driver.** Test at low speed, at about half battery charge, watching
   pack voltage. If it climbs past 8.3V, stop.

## Tuning YAW_KP

Start at 0. Raise it in steps of about 0.001. When the car starts weaving side
to side on a straight, you've gone too far — drop back about 30%.

`YAW_TARGET_GAIN` is the other knob. If actual yaw never gets anywhere near
target even at full correction, the target is unrealistic — lower it.

## Watch the overrun counter

The dashboard shows how many times the loop missed its 5ms slot. A handful at
startup is fine. If it climbs steadily while driving, the loop is doing more
work than it has time for — usually I2C traffic. Drop `LOOP_HZ` to 100 or read
the gyro every other cycle.

## Safety

- Regen testing at half charge, never straight off the charger.
- Watch pack voltage. Two full cells sit at 8.4V and pushing above that is
  not safe.
- The failsafe stops the car if the browser goes quiet for 300ms. Test it
  early — drive slowly, close the tab, confirm the car stops.
