# Wiring Guide

## ESP32 Sender

### LoRa Module (SX1276 / SX1278) — SPI

| SX127x Pin | ESP32 Pin | Notes              |
|------------|-----------|--------------------|
| VCC        | 3.3 V     | **3.3 V only!**    |
| GND        | GND       |                    |
| SCK        | GPIO 18   | SPI Clock          |
| MISO       | GPIO 19   | SPI MISO           |
| MOSI       | GPIO 23   | SPI MOSI           |
| NSS (CS)   | GPIO 5    | SPI Chip Select    |
| RST        | GPIO 14   | Module Reset       |
| DIO0       | GPIO 2    | Interrupt (RxDone) |

> The default SPI bus on ESP32 (VSPI) uses GPIO 18/19/23. NSS, RST, and DIO0
> can be any available GPIO — the values above are defaults in `config.h`.

### NTC Thermistor (Analog)

| Component        | ESP32 Pin | Notes                             |
|------------------|-----------|-----------------------------------|
| NTC → 3.3 V      | 3.3 V     | One leg of the NTC to VCC         |
| NTC → Series R   | GPIO 34   | Junction of NTC + series resistor |
| Series R → GND   | GND       | 10 kΩ series resistor to ground   |

**Voltage divider**: NTC on the high side, 10 kΩ fixed resistor on the low
side. The ADC reads the voltage at the midpoint.

```
  3.3 V ──┬── NTC ──┬── 10 kΩ ──┬── GND
                     │
                  GPIO 34
```

### HW-006 Line Tracker Sensor

| HW-006 Pin | ESP32 Pin | Notes                        |
|-------------|-----------|------------------------------|
| VCC         | 3.3 V     | Or 5 V (check module spec)   |
| GND         | GND       |                              |
| D0          | GPIO 27   | Digital out (interrupt-capable) |

The HW-006 outputs LOW when it detects the black tape and HIGH otherwise (or
the inverse, depending on the module revision). The firmware uses a hardware
interrupt on the rising or falling edge to count transitions.

### ACS712 Current Sensor

| ACS712 Pin | ESP32 Pin | Notes                        |
|------------|-----------|------------------------------|
| VCC        | 5 V       | Requires 5 V supply          |
| GND        | GND       |                              |
| OUT        | GPIO 35   | Analog output (0–3.3 V range)|

> **Important**: The ACS712 is powered from 5 V but its output voltage at
> zero current (quiescent) is VCC/2 = 2.5 V if powered at 5 V. Since the
> ESP32 ADC only accepts 0–3.3 V, you must either:
> 1. Use a voltage divider on the OUT pin (e.g. 10 kΩ + 5.6 kΩ), **or**
> 2. Power the ACS712 from 3.3 V (quiescent = 1.65 V, within ADC range).
>
> The default `config.h` assumes 3.3 V supply (`V_OFFSET = 1.65 V`).

**Formula** (Ohm's Law applied to the Hall-effect sensor):

```
  I = (V_out − V_offset) / Sensitivity
```

Where:
- `V_out` = ADC reading converted to volts
- `V_offset` = quiescent voltage (1.65 V at 3.3 V supply, 2.5 V at 5 V)
- `Sensitivity` = 185 mV/A (5 A), 100 mV/A (20 A), or 66 mV/A (30 A)

```
  5 V / 3.3 V ──┬── ACS712 VCC
                 │
  GND ───────────┤── ACS712 GND
                 │
  GPIO 35 ───────┘── ACS712 OUT
```

### Power

- Power the ESP32 via USB during development.
- For field deployment, use a LiPo battery with a voltage regulator or a
  battery shield.

---

## Gateway (Arduino / ESP32 Receiver)

The gateway is a second board with the same LoRa module, connected to the
server PC via USB.

### LoRa Module Wiring

Same pinout as the sender table above. If using an Arduino Uno/Nano instead
of an ESP32, adjust SPI pins:

| SX127x Pin | Arduino Uno | Arduino Nano |
|------------|-------------|--------------|
| SCK        | D13         | D13          |
| MISO       | D12         | D12          |
| MOSI       | D11         | D11          |
| NSS (CS)   | D10         | D10          |
| RST        | D9          | D9           |
| DIO0       | D2          | D2           |

The gateway's only other connection is USB to the server PC (serial at
115 200 baud).
