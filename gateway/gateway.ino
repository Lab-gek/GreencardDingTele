// ============================================================================
// gateway.ino — LoRa Gateway / Serial Forwarder
//
// Receives binary telemetry packets via LoRa, validates CRC-8, decodes the
// fields, and forwards them as JSON lines over USB serial (115 200 baud) for
// consumption by the server-side Python ingestion script.
//
// Required library: LoRa by Sandeep Mistry
// Board:            ESP32 Dev Module / Arduino Uno / Nano (adjust pins below)
// ============================================================================

#include <SPI.h>
#include <LoRa.h>

// ── Pin Configuration ───────────────────────────────────────────────────────
// Adjust these if using an Arduino Uno/Nano instead of a second ESP32.
// See docs/wiring.md for the full pinout table.

#ifdef ESP32
  #define LORA_SCK    18
  #define LORA_MISO   19
  #define LORA_MOSI   23
  #define LORA_NSS    5
  #define LORA_RST    14
  #define LORA_DIO0   2
#else
  // Arduino Uno / Nano defaults
  #define LORA_NSS    10
  #define LORA_RST    9
  #define LORA_DIO0   2
#endif

// ── LoRa Settings (must match the sender!) ──────────────────────────────────
#define LORA_FREQUENCY  433E6
#define LORA_SF         7
#define LORA_BW         125E3

// ── Packet constants ────────────────────────────────────────────────────────
#define PACKET_HEADER   0xAA
#define PACKET_SIZE     11

// ── CRC-8/MAXIM (identical to sender) ──────────────────────────────────────

uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x01)
                crc = (crc >> 1) ^ 0x8C;
            else
                crc >>= 1;
        }
    }
    return crc;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

// Convert a byte array to a hex string (for error reporting)
void bytesToHex(const uint8_t *data, size_t len, char *out) {
    const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        out[i * 2]     = hex[(data[i] >> 4) & 0x0F];
        out[i * 2 + 1] = hex[data[i] & 0x0F];
    }
    out[len * 2] = '\0';
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial);

    #ifdef ESP32
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    #endif

    LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.println("{\"error\":\"lora_init_failed\"}");
        while (true);
    }

    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW);

    // Print a startup message (parseable as JSON for the server)
    Serial.println("{\"status\":\"gateway_ready\"}");
}

// ── Main Loop ───────────────────────────────────────────────────────────────

void loop() {
    int packetSize = LoRa.parsePacket();
    if (packetSize == 0) return;       // no packet received

    // ---- Read raw bytes ----
    uint8_t buf[PACKET_SIZE];
    size_t idx = 0;
    while (LoRa.available() && idx < PACKET_SIZE) {
        buf[idx++] = (uint8_t)LoRa.read();
    }
    // Drain any extra bytes (shouldn't happen, but be safe)
    while (LoRa.available()) LoRa.read();

    int rssi = LoRa.packetRssi();

    // ---- Validate size ----
    if (idx != PACKET_SIZE) {
        Serial.print("{\"error\":\"bad_size\",\"got\":");
        Serial.print(idx);
        Serial.print(",\"rssi\":");
        Serial.print(rssi);
        Serial.println("}");
        return;
    }

    // ---- Validate header ----
    if (buf[0] != PACKET_HEADER) {
        char hexStr[PACKET_SIZE * 2 + 1];
        bytesToHex(buf, PACKET_SIZE, hexStr);
        Serial.print("{\"error\":\"bad_header\",\"raw\":\"");
        Serial.print(hexStr);
        Serial.print("\",\"rssi\":");
        Serial.print(rssi);
        Serial.println("}");
        return;
    }

    // ---- Validate CRC ----
    uint8_t computed = crc8(buf, 10);
    if (computed != buf[10]) {
        char hexStr[PACKET_SIZE * 2 + 1];
        bytesToHex(buf, PACKET_SIZE, hexStr);
        Serial.print("{\"error\":\"crc\",\"raw\":\"");
        Serial.print(hexStr);
        Serial.print("\",\"rssi\":");
        Serial.print(rssi);
        Serial.println("}");
        return;
    }

    // ---- Decode fields (little-endian) ----
    uint8_t  deviceId = buf[1];
    uint16_t seq      = buf[2] | ((uint16_t)buf[3] << 8);
    int16_t  tempRaw  = (int16_t)(buf[4] | ((uint16_t)buf[5] << 8));
    uint16_t rpm      = buf[6] | ((uint16_t)buf[7] << 8);
    int16_t  curRaw   = (int16_t)(buf[8] | ((uint16_t)buf[9] << 8));

    float tempC = tempRaw / 100.0;
    float amps  = curRaw  / 100.0;

    // ---- Output JSON line ----
    Serial.print("{\"dev\":");
    Serial.print(deviceId);
    Serial.print(",\"seq\":");
    Serial.print(seq);
    Serial.print(",\"temp\":");
    Serial.print(tempC, 2);
    Serial.print(",\"rpm\":");
    Serial.print(rpm);
    Serial.print(",\"amps\":");
    Serial.print(amps, 2);
    Serial.print(",\"rssi\":");
    Serial.print(rssi);
    Serial.println("}");
}
