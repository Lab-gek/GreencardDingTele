// ============================================================================
// firmware.ino — ESP32 LoRa Telemetry Sender
//
// Reads temperature from an NTC thermistor, RPM from a HW-006 line-tracker
// sensor, and current from an ACS712 Hall-effect sensor et al. Packs the
// values into a compact 11-byte binary packet and transmits via LoRa.
//
// Required library: LoRa by Sandeep Mistry (install via Arduino Library Manager)
// Board:            ESP32 Dev Module (or compatible)
// ============================================================================

#include <SPI.h>
#include <LoRa.h>
#include "config.h"

// ── Global State ────────────────────────────────────────────────────────────

// Pulse counter — modified inside ISR, read in loop
volatile unsigned long pulseCount = 0;

// Sequence number for each transmitted packet (wraps at 65 535)
uint16_t seqNumber = 0;

// Timestamp of last transmission
unsigned long lastSendTime = 0;

// ── ISR: Count black-tape transitions ───────────────────────────────────────

void IRAM_ATTR onTapeEdge() {
    pulseCount++;
}

// ── CRC-8/MAXIM ────────────────────────────────────────────────────────────

uint8_t crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x01)
                crc = (crc >> 1) ^ 0x8C;   // reflected polynomial 0x31
            else
                crc >>= 1;
        }
    }
    return crc;
}

// ── Temperature Reading ─────────────────────────────────────────────────────

float readTemperature() {
    int raw = analogRead(NTC_PIN);
    if (raw == 0) raw = 1;                       // avoid division by zero

    // Voltage-divider: NTC on high side, fixed resistor on low side
    // V_out = V_ref * R_series / (R_ntc + R_series)
    // → R_ntc = R_series * (ADC_MAX / raw − 1)
    float resistance = SERIES_RESISTOR * ((ADC_MAX / (float)raw) - 1.0);

    // Simplified Steinhart-Hart (B-parameter equation):
    //   1/T = 1/T0 + (1/B) * ln(R/R0)
    float steinhart = resistance / NOMINAL_RESISTANCE;   // R / R0
    steinhart = log(steinhart);                           // ln(R/R0)
    steinhart /= B_COEFFICIENT;                           // (1/B) * ln(R/R0)
    steinhart += 1.0 / (NOMINAL_TEMP + 273.15);          // + 1/T0
    steinhart = 1.0 / steinhart;                          // invert → T in Kelvin
    steinhart -= 273.15;                                  // convert to °C

    return steinhart;
}

// ── RPM Calculation ─────────────────────────────────────────────────────────

uint16_t computeRPM(unsigned long pulses, unsigned long elapsedMs) {
    if (elapsedMs == 0) return 0;

    // RPM = (pulses / TAPE_SECTIONS) * (60000 / elapsedMs)
    float rpm = ((float)pulses / TAPE_SECTIONS) * (60000.0 / elapsedMs);
    if (rpm > 65535.0) rpm = 65535.0;   // clamp to uint16_t max
    return (uint16_t)rpm;
}

// ── Current Reading (ACS712) ────────────────────────────────────────────────

float readCurrent() {
    // Take multiple samples and average for a stable reading
    const int SAMPLES = 20;
    long total = 0;
    for (int i = 0; i < SAMPLES; i++) {
        total += analogRead(ACS712_PIN);
    }
    float avgRaw = (float)total / SAMPLES;

    // Convert ADC value to voltage:  V_out = raw * (V_REF / ADC_MAX)
    float voltage = avgRaw * (V_REF / ADC_MAX);

    // Apply Ohm's Law for the ACS712:
    //   I = (V_out − V_offset) / Sensitivity
    float current = (voltage - ACS712_V_OFFSET) / ACS712_SENSITIVITY;

    return current;   // Amperes (positive = forward, negative = reverse)
}

// ── Build & Send Packet ─────────────────────────────────────────────────────

void sendPacket(int16_t tempX100, uint16_t rpm, int16_t currentX100) {
    uint8_t buf[PACKET_SIZE];

    buf[0] = PACKET_HEADER;
    buf[1] = DEVICE_ID;
    buf[2] = (uint8_t)(seqNumber & 0xFF);          // seq low byte
    buf[3] = (uint8_t)((seqNumber >> 8) & 0xFF);   // seq high byte
    buf[4] = (uint8_t)(tempX100 & 0xFF);            // temp low byte
    buf[5] = (uint8_t)((tempX100 >> 8) & 0xFF);    // temp high byte
    buf[6] = (uint8_t)(rpm & 0xFF);                 // rpm low byte
    buf[7] = (uint8_t)((rpm >> 8) & 0xFF);          // rpm high byte
    buf[8] = (uint8_t)(currentX100 & 0xFF);         // current low byte
    buf[9] = (uint8_t)((currentX100 >> 8) & 0xFF);  // current high byte
    buf[10] = crc8(buf, 10);                         // CRC over bytes 0-9

    LoRa.beginPacket();
    LoRa.write(buf, PACKET_SIZE);
    LoRa.endPacket();

    seqNumber++;
}

// ── Setup ───────────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("[sender] Booting...");

    // --- ADC pins ---
    analogReadResolution(12);   // 12-bit ADC (0-4095)
    pinMode(NTC_PIN, INPUT);
    pinMode(ACS712_PIN, INPUT);

    // --- HW-006 tracker sensor (interrupt) ---
    pinMode(TRACKER_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(TRACKER_PIN), onTapeEdge, FALLING);

    // --- LoRa ---
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_NSS);
    LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);

    if (!LoRa.begin(LORA_FREQUENCY)) {
        Serial.println("[sender] LoRa init FAILED — check wiring!");
        while (true);   // halt
    }

    LoRa.setSpreadingFactor(LORA_SF);
    LoRa.setSignalBandwidth(LORA_BW);
    LoRa.setTxPower(LORA_TX_POWER);

    Serial.println("[sender] LoRa init OK");
    Serial.print("[sender] Frequency: ");
    Serial.println(LORA_FREQUENCY);
    Serial.print("[sender] TX interval: ");
    Serial.print(SEND_INTERVAL_MS);
    Serial.println(" ms");

    lastSendTime = millis();
}

// ── Main Loop ───────────────────────────────────────────────────────────────

void loop() {
    unsigned long now = millis();
    unsigned long elapsed = now - lastSendTime;

    if (elapsed >= SEND_INTERVAL_MS) {
        // ---- Snapshot & reset pulse counter (atomic) ----
        noInterrupts();
        unsigned long pulses = pulseCount;
        pulseCount = 0;
        interrupts();

        // ---- Read sensors ----
        float tempC   = readTemperature();
        int16_t tempX100 = (int16_t)(tempC * 100.0);
        uint16_t rpm  = computeRPM(pulses, elapsed);
        float amps    = readCurrent();
        int16_t ampsX100 = (int16_t)(amps * 100.0);

        // ---- Transmit ----
        sendPacket(tempX100, rpm, ampsX100);

        // ---- Debug print ----
        Serial.print("[sender] seq=");
        Serial.print(seqNumber - 1);
        Serial.print("  temp=");
        Serial.print(tempC, 2);
        Serial.print(" °C  rpm=");
        Serial.print(rpm);
        Serial.print("  amps=");
        Serial.print(amps, 2);
        Serial.print(" A  pulses=");
        Serial.println(pulses);

        lastSendTime = now;
    }
}
