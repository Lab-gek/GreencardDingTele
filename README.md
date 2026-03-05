# GreencardDingTele — LoRa Telemetry System

A complete telemetry pipeline that reads **temperature**, **RPM**, and
**current (Amps)** on an ESP32, transmits the data over **LoRa** to a
**USB LoRa receiver** on the server, and displays it in real-time on a
**Grafana** dashboard backed by **InfluxDB**. The Python ingestion script
reads raw binary packets directly from the receiver, validates CRC-8, and
writes decoded telemetry to the database — no separate gateway
microcontroller is needed.

```
┌─────────────────────┐     LoRa 433 MHz     ┌──────────────────────────┐
│  ESP32 Sender       │ ──────────────────▶   │  Server (PC)             │
│  • NTC thermistor   │    11-byte binary     │  • USB LoRa receiver     │
│  • HW-006 tracker   │    packets            │  • ingest.py (binary →   │
│  • ACS712 current   │                       │    CRC check → decode)   │
│  • SX1276/SX1278    │                       │  • InfluxDB 2.x          │
└─────────────────────┘                       │  • Grafana               │
                                              └──────────────────────────┘
```

## Repository Structure

```
firmware/               ESP32 sender firmware (Arduino .ino)
  ├── config.h          Pin assignments, LoRa settings, calibration constants
  └── firmware.ino      Main sketch: read sensors → pack → transmit

server/                 Server-side data pipeline
  ├── config.yaml       Serial port & InfluxDB connection settings
  ├── requirements.txt  Python dependencies
  ├── ingest.py         USB LoRa → binary decode → InfluxDB bridge
  ├── Dockerfile        Container image for the ingest service
  ├── docker-compose.yml  InfluxDB + Grafana + ingest containers
  └── grafana/
      └── provisioning/
          ├── datasources/influxdb.yml   Auto-configured datasource
          └── dashboards/
              ├── dashboard.yml          Dashboard provider config
              └── telemetry.json         Pre-built Grafana dashboard

docs/                   Documentation
  ├── protocol.md       Binary packet format & CRC specification
  └── wiring.md         Hardware wiring diagrams & pin tables
```

## Hardware Requirements

| Component | Description |
|---|---|
| ESP32 Dev Board | Any variant (DevKit v1, NodeMCU-32S, etc.) |
| SX1276 or SX1278 | LoRa module (433 MHz) connected via SPI |
| NTC Thermistor | 10 kΩ @ 25 °C with a 10 kΩ series resistor |
| HW-006 v1.3 | Line tracker sensor for RPM detection |
| ACS712 | Hall-effect current sensor (5 A / 20 A / 30 A variant) |
| USB LoRa Receiver | USB module with SX1276/SX1278, plugs into server PC |
| Black tape | One or more strips on the rotating axle |

See [docs/wiring.md](docs/wiring.md) for complete wiring diagrams.

## Quick Start

### 1. Flash the Sender (ESP32)

1. Open `firmware/firmware.ino` in the Arduino IDE.
2. Install the **LoRa** library by Sandeep Mistry (Library Manager → search "LoRa").
3. Select board **ESP32 Dev Module**.
4. Edit `firmware/config.h` to match your pin wiring and NTC parameters.
5. Upload.

### 2. Connect the USB LoRa Receiver

1. Plug the USB LoRa receiver module into the server PC.
2. Note the serial port (e.g. `/dev/ttyUSB0` or `COM3`).
3. Ensure the receiver's LoRa frequency, spreading factor, and bandwidth
   match the sender's settings in `firmware/config.h`.

### 3. Start the Server Stack

```bash
cd server

# Start InfluxDB and Grafana
docker compose up -d

# Install Python dependencies
pip install -r requirements.txt

# Edit config.yaml — set the correct serial port
nano config.yaml

# Run the ingestion script
python ingest.py
```

### 4. Open Grafana

- Navigate to **http://localhost:3000**
- Login: `admin` / `admin`
- The **LoRa Telemetry Dashboard** is pre-loaded with:
  - Temperature time-series graph
  - RPM time-series graph
  - Current (Amps) time-series graph
  - Current temperature, RPM & Amps stat panels
  - Packets-lost counter

## Protocol

The LoRa packet is a compact 11-byte binary frame:

| Byte | Field | Type |
|---|---|---|
| 0 | Header (`0xAA`) | uint8 |
| 1 | Device ID | uint8 |
| 2–3 | Sequence number | uint16 LE |
| 4–5 | Temperature × 100 | int16 LE |
| 6–7 | RPM | uint16 LE |
| 8–9 | Current × 100 (A) | int16 LE |
| 10 | CRC-8/MAXIM | uint8 |

Full specification: [docs/protocol.md](docs/protocol.md)

## Configuration

All tunable parameters are in two places:

- **Firmware**: `firmware/config.h` — pin assignments, LoRa frequency,
  transmit interval, NTC calibration, tape sections, ACS712 sensitivity.
- **Server**: `server/config.yaml` — serial port, InfluxDB credentials.

## Troubleshooting

| Symptom | Check |
|---|---|
| `LoRa init FAILED` | Wiring (SPI pins, NSS, RST, DIO0). Ensure 3.3 V power to module. |
| No data in Grafana | Is `ingest.py` running? Check serial port in `config.yaml`. |
| Temperature reads wrong | Verify `B_COEFFICIENT`, `SERIES_RESISTOR`, `NOMINAL_RESISTANCE` for your NTC. |
| RPM always 0 | Check HW-006 orientation. Try swapping `FALLING` to `RISING` in `attachInterrupt`. |
| Current reads ~0 always | Verify ACS712 supply voltage matches `ACS712_V_OFFSET` in `config.h`. |
| Current value drifts | ACS712 offset can vary ± 25 mV. Calibrate `ACS712_V_OFFSET` with no load. |
| High packet loss | Reduce distance, check antenna, try lower spreading factor. |

## License

MIT

