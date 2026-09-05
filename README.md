# STICKCPLUS2-Thermometer

**English** | [日本語](README.ja.md)

A non-contact infrared thermometer built with an M5StickC Plus2 and a GY-906 (MLX90614).

In normal operation, the device displays the target temperature in real time. Hold BtnA to measure continuously, then release the button to calculate the median of the latest samples and HOLD the result on screen.

## Photos

| | | |
|---|---|---|
| ![](assets/IMG_9556.jpg) | ![](assets/IMG_9557.jpg) | ![](assets/IMG_9558.jpg) |

## Hardware

- M5StickC Plus2
- GY-906 / MLX90614 infrared temperature sensor
  - The module used in this project is a narrow-FOV DCI-type version
- Grove wiring

## Wiring

Connect the GY-906 to the Grove port on the M5StickC Plus2 as follows.

| GY-906 | M5StickC Plus2 |
|---|---|
| VIN | 5V |
| GND | GND |
| SCL | G32 |
| SDA | G33 |

I2C configuration:

```cpp
constexpr int SDA_PIN = 33;
constexpr int SCL_PIN = 32;
constexpr uint8_t MLX_ADDR = 0x5A;
```

> This configuration uses GPIO32 / GPIO33 on the M5StickC Plus2 Grove port for I2C.

## Required Libraries

The sketch uses the following libraries with Arduino IDE:

- M5StickCPlus2
- Adafruit MLX90614
- Wire

Sketch:

```text
STICKCPLUS2-Thermometer.ino
```

## Operation

The device uses three operating states.

```text
LIVE
  │
  │ Press BtnA
  ▼
MEASURING
  │
  │ Continue measuring while BtnA is held
  │
  │ Release BtnA
  ▼
HOLD
  │
  │ After 60 seconds
  ▼
LIVE
```

### LIVE

The normal state after startup.

- Reads the temperature approximately every 150 ms
- Displays the target temperature in real time
- Smooths the displayed value using an EMA
- Pressing BtnA enters `MEASURING`

Example:

```text
       26.7

LIVE  Ambient 25.4 C
```

### MEASURING

Active while BtnA is held down.

- Continues taking temperature measurements
- Updates the displayed temperature in real time
- Stores raw object-temperature samples for the final HOLD result
- Keeps up to the latest five samples
- Releasing BtnA finalizes the measurement

Example:

```text
       26.8

MEAS  Ambient 25.4 C
```

### HOLD

Entered immediately when BtnA is released.

The device calculates the median of up to the latest five raw samples collected during MEASURING and displays that value as the fixed measurement result.

Example:

```text
       26.8

HOLD  Ambient 25.4 C
```

The result remains in HOLD for 60 seconds, after which the device automatically returns to LIVE.

Pressing BtnA while in HOLD cancels HOLD immediately and returns to LIVE.

## Measurement Processing

### Realtime display

The LIVE and MEASURING displays use an exponential moving average (EMA):

```text
filtered = 0.35 × raw + 0.65 × previous
```

Configuration:

```cpp
constexpr float EMA_ALPHA = 0.35f;
```

This reduces small fluctuations in the displayed value when the sensor is handheld.

### HOLD result

The HOLD result does not use the EMA value.

Instead, it uses the **median of up to the latest five raw object-temperature samples** collected during MEASURING.

Example:

```text
26.7
26.8
27.5
26.9
26.8
```

Sorted:

```text
26.7
26.8
26.8
26.9
27.5
```

HOLD result:

```text
26.8 C
```

Using the median reduces the effect of transient outliers, such as a brief aiming error.

If fewer than five samples are collected during a very short button press, the median is calculated from the available samples.

## Timing

| Item | Setting |
|---|---:|
| Temperature sampling interval | 150 ms |
| HOLD duration | 60 seconds |
| Automatic power off | 300 seconds after the last button operation |
| HOLD samples | Latest 5 |
| EMA coefficient | 0.35 |

## Display

The 240 × 135 LCD on the M5StickC Plus2 is used in landscape orientation.

- Object temperature: large central display
- LIVE: cyan
- MEAS: green
- HOLD: yellow
- Ambient temperature: small text at the bottom

Rendering uses an `M5Canvas` sprite. The complete frame is drawn to the buffer before being transferred to the LCD.

## Sensor Initialization

The MLX90614 uses I2C address `0x5A`.

At startup, sensor initialization is retried up to 10 times.

```text
Attempt 1/10: 0x5A found, MLX90614 OK
```

If the sensor cannot be detected, an error message is displayed on the screen.

## Power Management

The device automatically powers off 300 seconds after the last BtnA operation.

Normal LIVE measurements and temperature changes are not treated as user activity.

## Serial Monitor

For debugging, use the Serial Monitor at 115200 bps.

Example output:

```text
STATE -> LIVE

BTN A: PRESS
STATE -> MEASURING

Raw: 26.71 C  Filtered: 26.68 C  Ambient: 25.40 C
Raw: 26.83 C  Filtered: 26.73 C  Ambient: 25.40 C
Raw: 26.90 C  Filtered: 26.79 C  Ambient: 25.41 C

BTN A: RELEASE
MEASURING -> HOLD
STATE -> HOLD result=26.83 C samples=5 read=OK
```

## Notes

The MLX90614 is a non-contact infrared sensor that measures infrared radiation emitted by a target.

Measurements are affected by factors including:

- Distance to the target
- Sensor field of view (FOV)
- Target emissivity
- Whether the target sufficiently fills the sensor's field of view
- Measurement through materials such as glass that do not transmit the relevant infrared wavelengths well

Even with a narrow-FOV sensor, the measurement area becomes larger as the distance increases. For small targets, shorter measurement distances generally provide more stable results.

## Future Ideas

Possible future extensions:

- Wi-Fi connectivity
- MQTT publish when a HOLD result is finalized
- Home Assistant integration
- NTP time synchronization
- Measurement history
- Battery level display
- OTA firmware updates

If network features are added, the thermometer should remain fully usable without network connectivity. A suitable design is to keep measurement local and only transmit data after a HOLD result has been finalized.

## License

This project is licensed under the [MIT License](LICENSE).

Copyright (c) 2026 omiya-bonsai
