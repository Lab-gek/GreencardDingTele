#!/usr/bin/env python3
"""
ingest.py — LoRa Binary Packet → InfluxDB ingestion bridge

Reads raw binary telemetry packets from a USB LoRa receiver module,
validates CRC-8, decodes the fields, and writes telemetry points to InfluxDB.
Handles reconnects for both serial and InfluxDB.

Packet format (11 bytes):
  [0]    0xAA header
  [1]    device ID
  [2-3]  sequence number  (uint16 LE)
  [4-5]  temperature×100  (int16  LE)
  [6-7]  RPM              (uint16 LE)
  [8-9]  current×100      (int16  LE)
  [10]   CRC-8/MAXIM over bytes 0-9

Usage:
    python ingest.py                        # uses config.yaml in CWD
    python ingest.py --config /path/to.yaml
"""

import argparse
import logging
import struct
import time

import serial
import yaml
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

# ── Constants ────────────────────────────────────────────────────────────────
PACKET_HEADER = 0xAA
PACKET_SIZE   = 11

# ── Logging ──────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("ingest")

# ── CRC-8/MAXIM (identical to firmware) ──────────────────────────────────────

def crc8(data: bytes) -> int:
    """Compute CRC-8/MAXIM over the given bytes."""
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x01:
                crc = (crc >> 1) ^ 0x8C   # reflected polynomial 0x31
            else:
                crc >>= 1
    return crc

# ── Helpers ──────────────────────────────────────────────────────────────────

def load_config(path: str) -> dict:
    """Load and return YAML configuration."""
    with open(path, "r") as f:
        cfg = yaml.safe_load(f)
    log.info("Loaded config from %s", path)
    return cfg


def open_serial(cfg: dict) -> serial.Serial:
    """Open serial port with retry."""
    while True:
        try:
            ser = serial.Serial(
                port=cfg["port"],
                baudrate=cfg["baudrate"],
                timeout=cfg.get("timeout", 2),
            )
            log.info("Serial port %s opened", cfg["port"])
            return ser
        except serial.SerialException as e:
            log.warning("Serial open failed (%s) — retrying in 3 s", e)
            time.sleep(3)


def open_influx(cfg: dict):
    """Return (client, write_api, bucket, org)."""
    client = InfluxDBClient(
        url=cfg["url"],
        token=cfg["token"],
        org=cfg["org"],
    )
    write_api = client.write_api(write_options=SYNCHRONOUS)
    log.info("Connected to InfluxDB at %s", cfg["url"])
    return client, write_api, cfg["bucket"], cfg["org"]


def read_packet(ser: serial.Serial) -> bytes | None:
    """
    Read from serial until the 0xAA header byte is found, then read the
    remaining 10 bytes to form a complete 11-byte packet.
    Returns the packet bytes or None on timeout / short read.
    """
    # Scan for header byte
    while True:
        b = ser.read(1)
        if not b:
            return None          # timeout
        if b[0] == PACKET_HEADER:
            break

    # Read remaining bytes
    rest = ser.read(PACKET_SIZE - 1)
    if len(rest) < PACKET_SIZE - 1:
        log.warning("Short read: got %d bytes after header", len(rest))
        return None

    return b + rest


def decode_packet(buf: bytes) -> dict | None:
    """
    Validate and decode an 11-byte binary packet.
    Returns a dict with decoded fields or None on error.
    """
    if len(buf) != PACKET_SIZE:
        log.warning("Bad packet size: %d", len(buf))
        return None

    if buf[0] != PACKET_HEADER:
        log.warning("Bad header: 0x%02X (expected 0xAA)", buf[0])
        return None

    # CRC check over bytes 0-9
    computed = crc8(buf[:10])
    if computed != buf[10]:
        log.warning("CRC mismatch: computed 0x%02X, got 0x%02X  raw=%s",
                     computed, buf[10], buf.hex().upper())
        return None

    # Decode fields (little-endian)
    #   B = uint8, H = uint16 LE, h = int16 LE
    device_id = buf[1]
    seq       = struct.unpack_from("<H", buf, 2)[0]
    temp_raw  = struct.unpack_from("<h", buf, 4)[0]
    rpm       = struct.unpack_from("<H", buf, 6)[0]
    cur_raw   = struct.unpack_from("<h", buf, 8)[0]

    return {
        "dev":  device_id,
        "seq":  seq,
        "temp": temp_raw / 100.0,
        "rpm":  rpm,
        "amps": cur_raw / 100.0,
    }


def write_point(data: dict, write_api, bucket: str, org: str, measurement: str):
    """Write a decoded telemetry dict to InfluxDB."""
    point = (
        Point(measurement)
        .tag("device_id", str(data["dev"]))
        .field("temperature", float(data["temp"]))
        .field("rpm", int(data["rpm"]))
        .field("current", float(data["amps"]))
        .field("seq", int(data["seq"]))
        .time(time.time_ns(), WritePrecision.NS)
    )
    try:
        write_api.write(bucket=bucket, org=org, record=point)
        log.debug("Written: dev=%s seq=%s temp=%.2f rpm=%d amps=%.2f",
                  data["dev"], data["seq"], data["temp"], data["rpm"], data["amps"])
    except Exception as e:
        log.error("InfluxDB write failed: %s", e)


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="USB LoRa receiver → InfluxDB ingestion")
    parser.add_argument(
        "--config", default="config.yaml",
        help="Path to YAML config file (default: config.yaml)",
    )
    args = parser.parse_args()

    cfg = load_config(args.config)
    ser_cfg = cfg["serial"]
    db_cfg  = cfg["influxdb"]
    measurement = db_cfg.get("measurement", "telemetry")

    # Open InfluxDB connection
    client, write_api, bucket, org = open_influx(db_cfg)

    # Open serial port (with retry)
    ser = open_serial(ser_cfg)

    log.info("Ingestion running — reading binary packets from %s, writing to %s/%s",
             ser_cfg["port"], db_cfg["url"], bucket)

    try:
        while True:
            try:
                buf = read_packet(ser)
                if buf is None:
                    continue

                data = decode_packet(buf)
                if data is None:
                    continue

                log.info("RX: dev=%d seq=%d temp=%.2f°C rpm=%d amps=%.2fA",
                         data["dev"], data["seq"], data["temp"], data["rpm"], data["amps"])
                write_point(data, write_api, bucket, org, measurement)

            except serial.SerialException as e:
                log.error("Serial error: %s — reconnecting", e)
                ser.close()
                time.sleep(2)
                ser = open_serial(ser_cfg)

    except KeyboardInterrupt:
        log.info("Shutting down (Ctrl+C)")
    finally:
        ser.close()
        client.close()
        log.info("Cleanup complete")


if __name__ == "__main__":
    main()
