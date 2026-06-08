# ESP32-C6 SHT31-Sensortest

Ein kleines, vollständig offline laufendes Demoprojekt: Ein per I2C
angeschlossener **SHT31** (Sensirion Temperatur-/Feuchtesensor) wird
zyklisch ausgelesen, und **Temperatur sowie relative Feuchte** werden auf
einem **SSD1306-OLED** angezeigt.

## Funktion

Der Sensor wird im Single-Shot-Modus betrieben: Etwa einmal pro Sekunde
sendet die Firmware einen Messbefehl, wartet die Messzeit ab und liest das
Ergebnis inklusive Prüfsumme aus. Die Werte erscheinen sowohl im seriellen
Log als auch in zwei Zeilen auf dem Display, zum Beispiel:

```
23.4 C
45.2 %
```

Schlägt eine Messung fehl (I2C-Fehler oder CRC stimmt nicht), zeigt das
Display `SHT31 / FEHLER` und der Task läuft beim nächsten Intervall normal
weiter.

## Hardware

Board: **ESP32-C6-DevKitC-1** (8 MB Flash)

SHT31-Sensor und OLED-Display teilen sich denselben I2C-Bus:

| Funktion        | Pin     | Hinweis                                      |
|-----------------|---------|----------------------------------------------|
| I2C SDA         | GPIO 5  | gemeinsam: SHT31 + SSD1306                    |
| I2C SCL         | GPIO 6  | 100 kHz (Standard Mode)                      |
| SHT31           | Addr 0x44 | ADDR-Pin auf GND (0x45 bei ADDR auf VDD)   |
| SSD1306 OLED    | Addr 0x3C | 128×32                                     |

Verdrahtung des SHT31-Breakouts: `VCC → 3V3`, `GND → GND`, `SDA → GPIO 5`,
`SCL → GPIO 6`. Die meisten Breakouts bringen eigene Pull-up-Widerstände
mit; zusätzlich sind die internen Pull-ups des ESP32-C6 aktiviert.

## Messprinzip SHT31

Verwendet wird die Single-Shot-Messung mit High Repeatability **ohne
Clock-Stretching** (Befehl `0x24 0x00`):

1. Den 2-Byte-Messbefehl senden.
2. Die Messzeit abwarten (laut Datenblatt max. 15 ms, hier 20 ms mit Reserve).
3. 6 Bytes lesen: `[T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC]`.
4. Beide CRC-8-Prüfsummen kontrollieren (Polynom `0x31`, Init `0xFF`) und die
   Rohwerte umrechnen:
   - `T[°C] = -45 + 175 · S_T / 65535`
   - `RH[%] = 100 · S_RH / 65535`

## Projektstruktur

| Datei                   | Aufgabe                                            |
|-------------------------|----------------------------------------------------|
| `main/main.c`           | Einstiegspunkt: I2C → Display → Sensor → Task      |
| `main/app_config.h`     | Pins, Adressen, Timings (zentral)                  |
| `main/sht31.c/.h`       | SHT31-Treiber inkl. CRC-Prüfung und Umrechnung     |
| `main/sensor_task.c/.h` | FreeRTOS-Task: zyklisch messen, loggen, anzeigen   |
| `main/display.c/.h`     | SSD1306-Treiber mit skalierbarer Schrift           |

## Konfiguration

Alle wesentlichen Parameter stehen in `main/app_config.h`, u. a.:

- `SHT31_I2C_ADDR` – Sensoradresse (`0x44` Standard, `0x45` bei ADDR=VDD)
- `SENSOR_INTERVAL_MS` – Abstand zwischen zwei Messungen (Standard 1000 ms)
- `APP_I2C_SDA_PIN` / `APP_I2C_SCL_PIN` – I2C-Pins
- `APP_I2C_FREQ_HZ` – I2C-Taktfrequenz (Standard 100 kHz)

## Bauen und Flashen

Voraussetzung ist eine eingerichtete **ESP-IDF v6.x**-Umgebung.

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

Unter Windows ist der Port z. B. `COM5`.
