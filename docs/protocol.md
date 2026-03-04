# Packet Protocol Specification

## Overview

The ESP32 sender transmits telemetry data over LoRa using a compact binary
packet format. The gateway receiver validates each packet and forwards the
decoded values as a JSON line over USB serial to the server.

## Binary Packet Format (LoRa)

Total size: **11 bytes**

| Offset | Size | Field       | Type   | Description                                      |
|--------|------|-------------|--------|--------------------------------------------------|
| 0      | 1    | Header      | uint8  | Magic byte `0xAA` — marks start of packet        |
| 1      | 1    | Device ID   | uint8  | Unique sender ID (0–255)                         |
| 2      | 2    | Sequence    | uint16 | Packet sequence number (little-endian, wraps)     |
| 4      | 2    | Temperature | int16  | Temperature × 100 in °C (e.g. 23.45 °C → 2345)  |
| 6      | 2    | RPM         | uint16 | Revolutions per minute (little-endian)            |
| 8      | 2    | Current     | int16  | Current × 100 in A (e.g. 1.25 A → 125)           |
| 10     | 1    | CRC-8       | uint8  | CRC-8/MAXIM over bytes 0–9                        |

### Notes

- **Byte order**: little-endian for all multi-byte fields (native ESP32 order).
- **Temperature**: signed 16-bit allows −327.68 °C to +327.67 °C. Divide by
  100 on the receiver side to get the real value.
- **RPM**: unsigned 16-bit allows 0–65 535 RPM.
- **Current**: signed 16-bit allows −327.68 A to +327.67 A. Divide by 100 on
  the receiver side. Measured via an ACS712 Hall-effect sensor using the
  formula: `I = (V_out − V_offset) / Sensitivity`.
- **Sequence number**: increments with each transmitted packet. The receiver
  uses it to detect lost packets. Wraps around from 65 535 → 0.

## CRC-8/MAXIM

Polynomial: `0x31` (x⁸ + x⁵ + x⁴ + 1)  
Initial value: `0x00`  
Reflect in/out: yes  

Reference implementation (C):

```c
uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x01)
                crc = (crc >> 1) ^ 0x8C;  // reflected 0x31
            else
                crc >>= 1;
        }
    }
    return crc;
}
```

## Serial JSON Format (Gateway → Server)

The gateway decodes each valid LoRa packet and prints a single JSON line over
serial at 115 200 baud:

### Successful packet

```json
{"dev":1,"seq":1234,"temp":23.45,"rpm":1200,"amps":1.25,"rssi":-67}
```

| Key    | Type  | Description                          |
|--------|-------|--------------------------------------|
| `dev`  | int   | Device ID                            |
| `seq`  | int   | Sequence number                      |
| `temp` | float | Temperature in °C (2 decimal places) |
| `rpm`  | int   | Revolutions per minute               |
| `amps` | float | Current in Amperes (2 decimal places)|
| `rssi` | int   | LoRa RSSI in dBm                     |

### CRC error

```json
{"error":"crc","raw":"AA01D204...","rssi":-70}
```

- `raw` contains the hex-encoded received bytes for debugging.
