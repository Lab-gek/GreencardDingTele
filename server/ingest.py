#!/usr/bin/env python3
"""
ingest.py — Serial-to-InfluxDB ingestion bridge

Reads JSON lines from the LoRa gateway's serial port and writes telemetry
points to InfluxDB.  Handles reconnects for both serial and InfluxDB.

Usage:
    python ingest.py                        # uses config.yaml in CWD
    python ingest.py --config /path/to.yaml
"""

import argparse
import json
import logging
import sys
import time
from pathlib import Path

import serial
import yaml
from influxdb_client import InfluxDBClient, Point, WritePrecision
from influxdb_client.client.write_api import SYNCHRONOUS

# ── Logging ──────────────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
log = logging.getLogger("ingest")

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


def process_line(line: str, write_api, bucket: str, org: str, measurement: str):
    """Parse one JSON line and write to InfluxDB if valid."""
    try:
        data = json.loads(line)
    except json.JSONDecodeError:
        log.warning("Invalid JSON: %s", line.strip())
        return

    # Skip non-data messages (status, errors)
    if "error" in data:
        log.warning("Gateway error: %s", data)
        return
    if "status" in data:
        log.info("Gateway status: %s", data.get("status"))
        return

    # Extract fields
    device_id = data.get("dev", 0)
    seq       = data.get("seq", 0)
    temp      = data.get("temp")
    rpm       = data.get("rpm")
    amps      = data.get("amps")
    rssi      = data.get("rssi")

    if temp is None or rpm is None:
        log.warning("Incomplete data: %s", data)
        return

    # Build InfluxDB point
    point = (
        Point(measurement)
        .tag("device_id", str(device_id))
        .field("temperature", float(temp))
        .field("rpm", int(rpm))
        .field("current", float(amps) if amps is not None else 0.0)
        .field("rssi", int(rssi) if rssi is not None else 0)
        .field("seq", int(seq))
        .time(time.time_ns(), WritePrecision.NS)
    )

    try:
        write_api.write(bucket=bucket, org=org, record=point)
        log.debug("Written: dev=%s seq=%s temp=%.2f rpm=%d amps=%.2f", device_id, seq, temp, rpm, amps or 0)
    except Exception as e:
        log.error("InfluxDB write failed: %s", e)


# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="LoRa → InfluxDB ingestion")
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

    log.info("Ingestion running — reading from %s, writing to %s/%s",
             ser_cfg["port"], db_cfg["url"], bucket)

    try:
        while True:
            try:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                log.info("RX: %s", line)
                process_line(line, write_api, bucket, org, measurement)

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
