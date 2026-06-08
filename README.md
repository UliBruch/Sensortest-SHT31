# ESP32-C6 Reaktionstest

Ein kleines, vollständig offline laufendes Demoprojekt für die
Vorlesung: Es misst die menschliche Reaktionszeit und zeigt sie groß
auf einem OLED-Display an.

## Funktion

Nach einem Tastendruck vergeht eine **zufällige Wartezeit** von 2–5
Sekunden. Danach leuchtet die **LED grün** auf, und ab diesem Moment
läuft eine Zeitmessung. Sobald der Taster erneut gedrückt wird, stoppt
die Messung, und die vergangene Zeit erscheint in Millisekunden in
großer Schrift auf dem Display.

Zwei Sonderfälle werden abgefangen:

- Wird schon **während der Wartezeit** (vor dem LED-Signal) gedrückt,
  gilt das als Fehlstart und das Display zeigt **`ZU FRUEH`**.
- Erfolgt nach dem LED-Signal **innerhalb von 2 Sekunden kein**
  Tastendruck, läuft die Messung in einen Timeout und das Display
  zeigt **`ZU SPAET`**. Anschließend geht das System wieder in
  Standby.

## Hardware

Board: **ESP32-C6-DevKitC-1** (8 MB Flash)

| Funktion        | Pin     | Hinweis                                      |
|-----------------|---------|----------------------------------------------|
| Taster          | GPIO 9  | BOOT-Taster, aktiv-low, interner Pull-up     |
| Status-LED      | GPIO 8  | Onboard-WS2812 (adressierbare RGB-LED)       |
| OLED SDA        | GPIO 5  | I2C, SSD1306 128×32, Adresse 0x3C            |
| OLED SCL        | GPIO 6  | I2C, 100 kHz                                  |

Es ist keine zusätzliche Verkabelung nötig – Taster und LED sind
bereits auf dem DevKit vorhanden.

## Zustandsautomat

Der Reaktionstest läuft als FreeRTOS-Task mit folgenden Zuständen
(siehe `main/reaction_task.c`):

```
IDLE  ──Taste──▶  WAITING ──Zufallszeit──▶ REACTING ──Taste──▶ RESULT ──▶ IDLE
                     │ Taste                  │ Timeout 2 s
                     ▼                        ▼
                  "ZU FRUEH"               "ZU SPAET"
```

## Projektstruktur

| Datei                     | Aufgabe                                            |
|---------------------------|----------------------------------------------------|
| `main/main.c`             | Einstiegspunkt: I2C → Display → Reaktionstest      |
| `main/app_config.h`       | Pins, Timings und Task-Parameter (zentral)         |
| `main/reaction_task.c/.h` | State Machine, Taster-Interrupt, LED, Zeitmessung  |
| `main/display.c/.h`       | SSD1306-Treiber mit skalierbarer Schrift           |

Die Zeitmessung nutzt `esp_timer_get_time()` (Mikrosekunden, Ausgabe
in Millisekunden), der Taster wird per GPIO-Interrupt mit Entprellung
ausgewertet, und die WS2812-LED wird über das `led_strip`-Component
angesteuert.

## Konfiguration

Alle wesentlichen Parameter stehen in `main/app_config.h` und lassen
sich dort anpassen, u. a.:

- `WAIT_MIN_MS` / `WAIT_MAX_MS` – Bereich der Zufallswartezeit
- `REACTION_TIMEOUT_MS` – Timeout in der Mess-Phase (Standard 2000 ms)
- `RESULT_DISPLAY_MS` – Anzeigedauer des Ergebnisses
- `LED_BRIGHTNESS` – Helligkeit der LED (0–255 pro Kanal)
- `BUTTON_DEBOUNCE_MS` – Entprellzeit des Tasters

## Bauen und Flashen

Voraussetzung ist eine eingerichtete **ESP-IDF v6.x**-Umgebung.

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

Unter Windows ist der Port z. B. `COM5`. Das `led_strip`-Component
wird beim ersten Build automatisch vom Component Manager geladen.
