// ============================================================================
// config.h — Central configuration for the ESP32 LoRa Telemetry Sender
// ============================================================================
#ifndef CONFIG_H
#define CONFIG_H

// ── Device ──────────────────────────────────────────────────────────────────
#define DEVICE_ID         1        // Unique ID for this sender (0-255)

// ── Timing ──────────────────────────────────────────────────────────────────
#define SEND_INTERVAL_MS  1000     // How often to transmit (milliseconds)

// ── LoRa Module (SX1276 / SX1278 via SPI) ───────────────────────────────────
#define LORA_SCK          18
#define LORA_MISO         19
#define LORA_MOSI         23
#define LORA_NSS          5
#define LORA_RST          14
#define LORA_DIO0         2

#define LORA_FREQUENCY    433E6    // 433 MHz (change to 868E6 or 915E6 if needed)
#define LORA_SF           7        // Spreading factor (6-12)
#define LORA_BW           125E3    // Bandwidth in Hz
#define LORA_TX_POWER     17       // dBm (2-20)

// ── NTC Thermistor ──────────────────────────────────────────────────────────
#define NTC_PIN           34       // ADC input (GPIO 34 — input-only on ESP32)
#define SERIES_RESISTOR   10000.0  // 10 kΩ fixed resistor in the voltage divider
#define NOMINAL_RESISTANCE 10000.0 // NTC resistance at NOMINAL_TEMP
#define NOMINAL_TEMP      25.0     // °C — reference temperature
#define B_COEFFICIENT     3950.0   // Beta coefficient (check your NTC datasheet)
#define ADC_MAX           4095.0   // ESP32 12-bit ADC
#define V_REF             3.3      // ADC reference voltage

// ── HW-006 Line Tracker Sensor (speed / RPM) ───────────────────────────────
#define TRACKER_PIN       27       // Digital input (interrupt-capable GPIO)
#define TAPE_SECTIONS     1        // Number of black tape strips on the axle
                                   //   1 = one strip → 1 pulse per revolution
                                   //   2 = two strips → 2 pulses per revolution

// ── ACS712 Current Sensor ───────────────────────────────────────────────────
//  Formula: I = (V_out − V_offset) / Sensitivity
//  V_out    = ADC reading converted to volts
//  V_offset = quiescent voltage (VCC / 2 when no current flows)
//  Sensitivity depends on variant:
//    ACS712-05B → 185 mV/A  (±5 A)
//    ACS712-20A → 100 mV/A  (±20 A)
//    ACS712-30A →  66 mV/A  (±30 A)
#define ACS712_PIN        35       // ADC input (GPIO 35 — input-only on ESP32)
#define ACS712_SENSITIVITY 0.185   // V/A — 185 mV/A for the 5 A variant
#define ACS712_V_OFFSET   1.65     // Quiescent output voltage (3.3 V / 2)
                                   // If powered from 5 V via level-shift, use 2.5

// ── Packet ──────────────────────────────────────────────────────────────────
#define PACKET_HEADER     0xAA     // Magic byte
#define PACKET_SIZE       11       // Total bytes in a LoRa packet

#endif // CONFIG_H
