# Migration: PlatformIO → reines ESP-IDF

Dieses Dokument beschreibt Schritt für Schritt, wie ein bestehendes
PlatformIO-Projekt für den ESP32 in ein reines **ESP-IDF**-Projekt
überführt wird (am konkreten Beispiel des Vorlesungsprojekts
`esp32_vorlesung_code`). Es richtet sich an Studierende, die diese Art
der Migration auch auf eigene Projekte anwenden wollen — daher wird
nicht nur *was* gemacht wird beschrieben, sondern auch *warum* jeder
Schritt nötig ist und welche Konzepte dahinterstehen.

---

## 1. Warum überhaupt migrieren?

PlatformIO und ESP-IDF sind keine Konkurrenten im engeren Sinne:
PlatformIO ist ein **Wrapper** um diverse Embedded-Frameworks, der
Build-Konfiguration, Toolchain-Installation und Library-Management
vereinheitlicht. Für den ESP32 ruft PlatformIO unter der Haube
ebenfalls ESP-IDF auf — es schiebt nur eine eigene Konfigurationsschicht
darüber.

Diese Wrapper-Schicht hat klare **Vorteile**:

- **Sehr niedrige Einstiegshürde.** Man legt eine `platformio.ini` an,
  drückt auf "Build", PlatformIO lädt die richtige Toolchain, das
  Framework und alle Libraries aus einem Registry-Server herunter.
- **Einheitliche Bedienung über viele Boards und Frameworks hinweg.**
  Wer Arduino, STM32CubeIDE, ESP-IDF und Mbed parallel nutzt, hat
  überall dieselbe `pio run`-Schnittstelle.
- **Library-Manager mit Versionierung** über `lib_deps`.

Dem stehen **Nachteile** gegenüber, die in einer Vorlesung wie unserer
zunehmend stören:

- **Verzögerung bei neuen ESP-IDF-Versionen.** PlatformIO supportet oft
  erst eine ESP-IDF-Version später als Espressif sie veröffentlicht —
  manche neuen APIs sind dann nicht verfügbar.
- **Versteckte Abstraktion.** Bugs an der Schnittstelle PlatformIO ↔
  ESP-IDF sind schwer zu debuggen, weil zwei Build-Systeme
  übereinanderliegen.
- **Berufsrelevanz.** In professionellen Espressif-Projekten ist reines
  ESP-IDF (mit `idf.py`, CMake und `menuconfig`) der De-facto-Standard.
  Wer das einmal selbst eingerichtet hat, versteht, was vorher in der
  Black Box passiert ist.
- **Kleinere Toolchain-Footprints im CI/Container.** Eine
  ESP-IDF-Installation ist oft schlanker und schneller reproduzierbar
  als ein vollständiges PlatformIO-Setup.

Für unser Vorlesungsprojekt sprechen vor allem die letzten beiden
Punkte für den Umzug: Ihr sollt **wissen, was passiert**, statt es
PlatformIO blind anzuvertrauen.

---

## 2. Konzeptueller Vergleich der Build-Systeme

Bevor wir Dateien hin- und herschieben, lohnt es sich, die Konzepte
nebeneinanderzulegen. Beide Systeme müssen letztlich dieselben Fragen
beantworten: *Welche Quellen werden kompiliert? Mit welchem Compiler?
Welche Bibliotheken werden gelinkt? Auf welchen Chip wird geflasht?
Welche Partition-Tabelle hat das Flash?* Sie tun es nur unterschiedlich.

| Aufgabe | PlatformIO | reines ESP-IDF |
|---|---|---|
| Hauptkonfigdatei | `platformio.ini` | `CMakeLists.txt` (top-level) + `sdkconfig` |
| Zielchip festlegen | `board = esp32-c6-devkitc-1` | `idf.py set-target esp32c6` (schreibt `CONFIG_IDF_TARGET` in sdkconfig) |
| Framework | `framework = espidf` | implizit (ist das Framework selbst) |
| Externe C-Libraries | `lib_deps = …` | `idf_component.yml` (Component Manager) **oder** Unterordner unter `components/` |
| Pro-Komponente Build-Regeln | nicht trivial | `main/CMakeLists.txt` mit `idf_component_register(...)` |
| Partitionstabelle | `board_build.partitions = partitions.csv` | `CONFIG_PARTITION_TABLE_CUSTOM=y` + Pfad in sdkconfig |
| Konfigurations-UI | `menuconfig` (über Umweg) | `idf.py menuconfig` (Erstklassen-Tool) |
| Defaults persistieren | `sdkconfig.defaults` (kennt PIO auch) | `sdkconfig.defaults` |
| Build / Flash / Monitor | `pio run`, `pio run -t upload`, `pio device monitor` | `idf.py build`, `idf.py -p COMx flash`, `idf.py -p COMx monitor` |

**Kernaussage**: PlatformIO bündelt Konfiguration, die in ESP-IDF an
mehreren Stellen lebt (CMake, sdkconfig, Component Manager), in einer
einzigen INI-Datei. Beim Umstieg verteilen wir diese Konfiguration
wieder auf ihre "natürlichen" Plätze.

### Verzeichnisstruktur — der wichtigste Unterschied

PlatformIO erwartet typischerweise:

```
projekt/
├── platformio.ini
├── src/                  ← .c/.cpp-Dateien
├── include/ oder inc/    ← Header
├── lib/                  ← lokale Libraries
└── test/
```

Reines ESP-IDF erwartet stattdessen:

```
projekt/
├── CMakeLists.txt        ← Top-Level
├── sdkconfig.defaults    ← optionale Defaults
├── partitions.csv        ← optional, eigene Partitionstabelle
├── main/                 ← die "main"-Komponente (Pflicht)
│   ├── CMakeLists.txt
│   ├── *.c, *.h
│   └── idf_component.yml ← optional, externe Komponenten
└── components/           ← optional, eigene zusätzliche Komponenten
    └── meine_lib/
        ├── CMakeLists.txt
        └── *.c, *.h
```

In ESP-IDF ist alles eine **Komponente** — auch `main/`. Eine
Komponente hat ihr eigenes `CMakeLists.txt`, das mit
`idf_component_register(...)` deklariert, was kompiliert werden soll
und welche anderen Komponenten man braucht.

---

## 3. Vorab-Analyse: Was hängt im PIO-Projekt drin?

Bevor man mit Kopieren anfängt, klärt man drei Fragen:

**Frage 1: Welche externen Libraries werden über `lib_deps` eingebunden?**

Diese müssen in ESP-IDF entweder über den Component Manager
(`idf_component.yml`) bezogen oder als manuelle Komponente unter
`components/` abgelegt werden. Im Bestfall existiert die Lib direkt im
ESP-Component-Registry (`https://components.espressif.com`), dann ist
es ein Einzeiler in `idf_component.yml`.

In **unserem** Vorlesungsprojekt:

```ini
; platformio.ini
[env:esp32-c6-devkitc-1]
platform = espressif32
board = esp32-c6-devkitc-1
framework = espidf
board_build.partitions = partitions.csv
upload_port = COM5
monitor_port = COM5
monitor_speed = 115200
```

Es gibt **keine** `lib_deps` — wir nutzen ausschließlich Komponenten,
die schon Teil von ESP-IDF sind. Die Sensor-Treiber (BME280, SHT31) und
der SSD1306-Display-Treiber sind selbst geschrieben. Das macht unsere
Migration deutlich einfacher als typische Arduino-PIO-Projekte, die
gerne mal ein Dutzend Libraries aus dem Registry ziehen.

**Frage 2: Welche internen ESP-IDF-Komponenten verwendet der Code?**

Dafür schaut man in das `idf_component_register(...)` der bisherigen
Build-Datei (`src/CMakeLists.txt`, falls eine existiert) — oder wenn
keine da ist, listet man, welche ESP-IDF-Header der Code includiert.

Bei uns:

```cmake
PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer esp-tls mbedtls
```

Diese Liste übernehmen wir später 1:1.

**Frage 3: Welche Spezial-Konfiguration braucht der Code?**

Hier hilft der Blick in `sdkconfig.defaults`. Bei uns sind die wichtigen
Punkte:

- `CONFIG_NEWLIB_NANO_FORMAT=n` — ohne diese Option druckt `printf("%f", x)`
  keine Floats, sondern nur leere Strings. Wer Sensorwerte loggt,
  braucht das.
- `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` und
  `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` — nur damit
  funktioniert TLS gegen HiveMQ Cloud, weil der ESP sonst keine
  Root-CAs kennt.
- `CONFIG_FREERTOS_HZ=1000` — Tick-Rate auf 1 kHz hochgestellt.
- `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10` — Watchdog-Timeout.

Wenn man diese drei Fragen beantwortet hat, ist die Migration zu großen
Teilen schon entschieden. Der Rest ist Mechanik.

---

## 4. Schritt-für-Schritt-Migration

Wir gehen davon aus, dass das Ziel-Verzeichnis bereits ein
ESP-IDF-Hello-World-Beispiel enthält (z. B. das `blink`-Beispiel aus
`$IDF_PATH/examples/get-started/blink`). Das gibt uns ein
funktionsfähiges Grundgerüst, aus dem wir das Beispiel-spezifische
Material entfernen und durch unseren Code ersetzen.

### 4.1 Altes Beispielprojekt aufräumen

Wir entfernen alle Dateien, die zum Blink-Beispiel gehören und uns nur
im Weg sind:

```bash
rm -f main/blink_example_main.c \
      main/Kconfig.projbuild \
      main/idf_component.yml \
      sdkconfig.defaults \
      sdkconfig.defaults.esp32* \
      sdkconfig \
      dependencies.lock \
      README.md \
      pytest_blink.py \
      .clangd
rm -rf build managed_components .devcontainer .vscode
```

**Warum jede dieser Dateien weg muss:**

- `main/blink_example_main.c` — enthält das alte `app_main()`. Davon
  kann es nur eines geben.
- `main/Kconfig.projbuild` — definiert die `CONFIG_BLINK_*`-Optionen,
  die der alte Beispielcode benötigt. Ohne ihn wären sie tote
  Konfigurationspunkte in `menuconfig`.
- `main/idf_component.yml` — listet die `espressif/led_strip`-
  Dependency, die wir nicht mehr brauchen. Wir schreiben gleich eine
  saubere neue Version (oder lassen sie ganz weg).
- `sdkconfig.defaults*` — ihre Inhalte stammen vom Beispiel und
  überschreiben sonst unsere eigenen Defaults beim ersten Build.
- `sdkconfig` — die "echte" aktuelle Konfiguration. Wird beim ersten
  `idf.py build` aus den `sdkconfig.defaults` neu generiert. Wenn man
  sie nicht löscht, bleibt das Target möglicherweise auf dem alten
  Chip (z. B. ESP32 statt ESP32-C6) hängen, weil `sdkconfig` Vorrang
  vor `sdkconfig.defaults` hat.
- `dependencies.lock`, `managed_components/` — vom Component Manager
  gelocktes Package-Set. Wird beim nächsten Build neu erzeugt.
- `build/` — alle Build-Artefakte; aus früheren Konfigurationen, würden
  sonst veraltete Objektdateien ins neue Image mischen.
- `.devcontainer/`, `.vscode/`, `.clangd` — IDE-spezifischer Kram, der
  hier nur stört. (Wer mag, kann ihn behalten und später anpassen.)

### 4.2 Quellcode umziehen

Die `.c`-Dateien aus `src/` und die `.h`-Dateien aus `inc/` des PIO-
Projekts werden direkt nach `main/` kopiert:

```bash
cp /pfad/zum/pio-projekt/src/*.c main/
cp /pfad/zum/pio-projekt/inc/*.h main/
```

**Warum nicht die `src/` + `inc/`-Trennung beibehalten?**

Man kann beides machen — aber das **idiomatische** ESP-IDF-Layout legt
Header und Source einer Komponente in dasselbe Verzeichnis. Das hat
zwei Vorteile:

1. Das `INCLUDE_DIRS "."` im `main/CMakeLists.txt` reicht aus, um die
   Header für die eigene Komponente sichtbar zu machen — man muss
   nicht mit relativen Pfaden wie `INCLUDE_DIRS "../inc"` jonglieren.
2. Wenn man später Teile des Codes in eine eigene Komponente
   ausgliedert (z. B. einen `bme280_driver` unter `components/`), ist
   die Konvention "Header und Source liegen zusammen" leichter
   anzuwenden.

Die `src/`+`inc/`-Trennung ist eher eine Konvention aus klassischen
C-Projekten und vielen Arduino-Libraries.

### 4.3 Komponenten-`CMakeLists.txt` (`main/CMakeLists.txt`)

Die zentrale Datei, mit der ESP-IDF erfährt, was in unserer
`main`-Komponente steckt:

```cmake
FILE(GLOB app_sources "*.c")

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    PRIV_REQUIRES esp_wifi esp_netif esp_event nvs_flash mqtt esp_timer esp-tls mbedtls
)
```

Was passiert hier?

- **`FILE(GLOB app_sources "*.c")`** sammelt alle `.c`-Dateien im
  aktuellen Verzeichnis ein. Bequem, aber mit einer Warnung verbunden
  (siehe unten).
- **`idf_component_register(...)`** ist die Funktion, mit der jede
  ESP-IDF-Komponente sich beim Build-System anmeldet. Die wichtigsten
  Argumente:
  - `SRCS` — Liste der zu kompilierenden Quelldateien.
  - `INCLUDE_DIRS` — **öffentliche** Include-Verzeichnisse (sichtbar
    für andere Komponenten, die diese Komponente nutzen).
  - `PRIV_REQUIRES` — **private** Abhängigkeiten dieser Komponente.
    Andere Komponenten "erben" sie nicht. Da `main` ohnehin am Ende
    der Abhängigkeitskette steht, ist `PRIV_REQUIRES` hier völlig
    ausreichend.

**Was bedeuten die einzelnen `PRIV_REQUIRES`-Einträge?**

| Eintrag | Wofür ESP-IDF-Komponente |
|---|---|
| `esp_wifi` | WiFi-Stack (Station/AP), `esp_wifi_init`, `esp_wifi_start` etc. |
| `esp_netif` | Netzwerk-Interface-Layer über lwIP, DHCP-Client, IP-Events |
| `esp_event` | Event-Loop-System; WiFi und MQTT melden Events darüber |
| `nvs_flash` | Nicht-flüchtiger Speicher; WiFi speichert dort z. B. Calibration |
| `mqtt` | `esp-mqtt`-Client, `esp_mqtt_client_init` etc. (in v6.0+ als managed component, siehe unten) |
| `esp_timer` | High-Resolution-Timer, `esp_timer_get_time()` |
| `esp-tls` | TLS-Wrapper (von `esp-mqtt` für `mqtts://` benutzt) |
| `mbedtls` | Krypto-Bibliothek; wir brauchen sie u. a. für das CA-Bundle |
| `esp_driver_gpio` | GPIO-API (`driver/gpio.h`) — in v5+ aus der alten Umbrella `driver` ausgegliedert |
| `esp_driver_i2c` | I2C-Master-API (`driver/i2c_master.h`) — ebenfalls aus `driver` ausgegliedert |

Wenn man eine dieser Komponenten vergisst, schlägt der Linker mit
`undefined reference to esp_wifi_init` o. ä. fehl — das ist die
typische Diagnose. Für die Driver-Komponenten kommt der Fehler
typischerweise schon beim *Compile* in Form von `driver/gpio.h: No
such file or directory`.

**Ein Wort zur alten `driver`-Komponente**: Bis ESP-IDF v4.x war
`driver` eine monolithische Komponente, die alle Hardware-Treiber
enthielt (GPIO, I2C, SPI, UART, RMT, LEDC, …). Ab v5.0 hat Espressif
sie in viele kleine `esp_driver_*`-Komponenten zerlegt, sodass man
nur das einbindet, was man wirklich nutzt. Die alte
`driver`-Sammelkomponente existiert in v6.0 immer noch als
"Umbrella" — wer `PRIV_REQUIRES driver` schreibt, bekommt alle
Subkomponenten transitiv. Das ist bequem, aber unsauber: man
verschleiert seine eigentlichen Abhängigkeiten und der Linker zieht
mehr Code rein als nötig. Sauberer ist die explizite Liste der
benötigten `esp_driver_*`-Komponenten.

**Warnung zu `FILE(GLOB)`**: CMake re-evaluiert das Glob nicht
automatisch, wenn man eine neue `.c`-Datei hinzufügt. Nach jeder neuen
Datei einmal `idf.py reconfigure` (oder `fullclean` + `build`) laufen
lassen. Ältere ESP-IDF-Doku rät stattdessen zu expliziten Listen
(`SRCS "main.c" "bme280_task.c" …`), das ist robuster, aber pflegeaufwändiger.
Für ein Lehrprojekt ist Glob bequem genug.

**Achtung — ESP-IDF v6.0 und der MQTT-Client**: Bis einschließlich
ESP-IDF v5.x lag die MQTT-Client-Implementierung als Core-Komponente
unter `components/mqtt/` im ESP-IDF-Tree und konnte einfach über
`PRIV_REQUIRES mqtt` eingebunden werden. Ab v6.0 hat Espressif viele
ehemalige Core-Komponenten (darunter `mqtt`) aus dem Hauptbaum
ausgelagert in die zentrale **Component Registry**
(`https://components.espressif.com`). Das ist Teil eines größeren
Trends — der Core-Tree soll schlank bleiben, einzelne Komponenten
sollen unabhängig vom IDF-Release-Zyklus weiterentwickelt werden.

Konkret heißt das: Wer auf v6.0+ baut und `PRIV_REQUIRES mqtt`
schreibt, bekommt einen CMake-Fehler:

```
Failed to resolve component 'mqtt' required by component 'main':
unknown name.
```

Lösung: in `main/idf_component.yml` die Komponente als Managed
Dependency aufnehmen — der Component Manager lädt sie beim nächsten
Build automatisch nach:

```yaml
dependencies:
  espressif/mqtt: "*"
```

Achtung beim Namen: Im **Quellcode** und in den Headern hieß die
Komponente schon immer "esp-mqtt", in der **Component Registry** ist
sie aber unter dem Namespace-Pfad `espressif/mqtt` gelistet (ohne
`esp-`-Präfix, weil sie unter dem Namespace `espressif/` schon
eindeutig genug ist). Wer den Namen falsch tippt, bekommt vom
Component Manager:

```
WARNING: Component "espressif/esp-mqtt" not found
ERROR: Version solving failed
```

Im Zweifelsfall lohnt sich ein Blick auf
`https://components.espressif.com/components?q=mqtt` — dort sind die
exakten Pfade gelistet.

Die `"*"`-Versionsspec überlässt dem Component Manager die Wahl der
neuesten kompatiblen Version; in einem produktiven Projekt sollte man
sie auf eine konkrete Range pinnen (z. B. `"^1.0.0"`), um
reproduzierbare Builds zu bekommen. Die Komponente registriert sich
weiterhin unter dem Namen `mqtt`, sodass die Zeile
`PRIV_REQUIRES … mqtt …` im `CMakeLists.txt` unverändert bleiben
kann — der einzige Unterschied ist die Bezugsquelle.

Andere Komponenten könnten in zukünftigen IDF-Versionen denselben Weg
gehen. Die generelle Diagnose lautet: Wenn `Failed to resolve
component 'X'` erscheint und in `components/X/` keine
`CMakeLists.txt` mehr liegt (sondern z. B. nur noch `test_apps/`),
ist X wahrscheinlich migriert — und gehört in `idf_component.yml`.

### 4.4 Top-Level `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_demo_projekt_sms2)
```

Diese Datei ist fast immer gleich aufgebaut. Wichtig sind drei Punkte:

1. **Genau in dieser Reihenfolge** — `cmake_minimum_required` zuerst,
   dann `include(...)`, dann `project(...)`. ESP-IDF braucht das so,
   weil das `include` Variablen setzt, die `project()` benötigt.
2. **Der Name in `project(esp32_demo_projekt_sms2)`** wird zum Namen
   der erzeugten Binary (`build/esp32_demo_projekt_sms2.bin`,
   `build/esp32_demo_projekt_sms2.elf`). Den kann man frei wählen.
3. **Was wir entfernt haben**: die Zeile
   `idf_build_set_property(MINIMAL_BUILD ON)`, die das Blink-Beispiel
   gesetzt hatte. Diese Option weist ESP-IDF an, nur die Komponenten
   zu bauen, die `main` direkt benötigt, und die übrigen wegzulassen.
   Das ist gut, wenn man wirklich nur eine LED blinken lässt — sobald
   wir aber WiFi, MQTT und TLS brauchen, schaltet `MINIMAL_BUILD`
   diverse transitive Abhängigkeiten ab und der Link schlägt fehl.

### 4.5 Partition-Table aktivieren

Der ESP32 unterteilt sein internes Flash in **Partitionen**: NVS,
PHY-Init-Daten, Bootloader, Anwendungs-Image (`app`), optionale OTA-
Slots, Filesystem-Partitionen usw. Welche Partitionen es gibt und wie
groß sie sind, beschreibt die *Partitionstabelle*.

Die **Standardpartitionstabelle** von ESP-IDF reserviert nur **1 MB**
für das App-Image. Sobald man `mbedtls` mit dem vollständigen CA-Bundle
hinzunimmt (für TLS gegen HiveMQ), wächst das Image deutlich über diese
1-MB-Grenze hinaus, und der Build bricht mit einer Fehlermeldung wie
*"Image is too large for the configured partition"* ab.

Lösung: eine eigene Partitionstabelle. Bei uns:

```csv
# partitions.csv
nvs,        data, nvs,     0x9000,   0x6000,
phy_init,   data, phy,     0xf000,   0x1000,
factory,    app,  factory, 0x10000,  0x300000,
```

Die Spalten sind: Name, Typ (`data` oder `app`), Subtyp, Offset im
Flash, Größe, Flags. Der entscheidende Eintrag ist `factory` mit
`0x300000` — also **3 MB**. Großzügig bemessen, damit wir nicht bei
jedem neuen Feature wieder daran ziehen müssen.

In **PlatformIO** würde man jetzt einfach in die `platformio.ini`
schreiben:

```ini
board_build.partitions = partitions.csv
```

In **reinem ESP-IDF** geht das über drei sdkconfig-Optionen:

```
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"
```

Die ersten beiden sind die wichtigen: "Verwende eine benutzerdefinierte
Partitionstabelle, sie heißt `partitions.csv`". Die dritte
(`CONFIG_PARTITION_TABLE_FILENAME`) wird ebenfalls gesetzt, damit
einige Tools sie korrekt finden.

**Stolperfalle: physische Flash-Größe**. Eine eigene Partitionstabelle
allein reicht nicht — ESP-IDF muss zusätzlich wissen, wie groß der
Flash-Chip auf dem Board *tatsächlich* ist. Der Default-Wert für
ESP32-C6 ist 2 MB. Steht der auf 2 MB, aber unsere
Partitionstabelle braucht 3 MB (3.0625 MB inkl. NVS und PHY-Init), bricht
der Build mit folgender Meldung ab:

```
Partitions tables occupies 3.1MB of flash (3211264 bytes) which does
not fit in configured flash size 2MB.
```

PlatformIO hat das im Hintergrund automatisch passend gesetzt, weil
das Board-Profil `esp32-c6-devkitc-1` weiß, dass dieses DevKit 8 MB
Flash hat. In reinem ESP-IDF muss man die Flash-Größe selbst angeben:

```
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHSIZE="8MB"
```

Beide Zeilen sind nötig: die erste ist die Auswahl-Option (welche
Größe?), die zweite der String, den `esptool` beim Flashen verwendet.
Wer nicht sicher ist, wie viel Flash sein Board hat, kann es per
`esptool.py --port COMx flash_id` auslesen.

Dieselben Optionen kann man auch interaktiv über
`idf.py menuconfig` → *Partition Table* setzen. Wir schreiben sie
direkt in `sdkconfig.defaults`, damit der Erstbau ohne menuconfig
durchläuft.

### 4.6 `sdkconfig.defaults` — das Konfigurationsmodell von ESP-IDF

ESP-IDF kennt drei Ebenen der Konfiguration:

1. **`Kconfig`-Dateien** (in jeder Komponente) — definieren *welche
   Optionen es überhaupt gibt*. Verändert man normalerweise nicht.
2. **`sdkconfig`** — die *aktuelle* Konfiguration. Wird automatisch
   verwaltet, sobald man `idf.py menuconfig` aufruft oder `idf.py
   build` startet. Sollte **nicht ins Git** committed werden, weil sie
   gerne zwischen Entwicklern divergiert.
3. **`sdkconfig.defaults`** — die *Vorgabe-Werte* für eine frische
   Konfiguration. Wird ins Git committed. Beim ersten Build (oder nach
   einem `idf.py fullclean`) liest ESP-IDF die Defaults und erzeugt
   daraus eine `sdkconfig`.

Unsere `sdkconfig.defaults` sieht so aus:

```bash
# Build target (ersetzt `idf.py set-target esp32c6`)
CONFIG_IDF_TARGET="esp32c6"

# Custom partition table (ersetzt `board_build.partitions` aus PIO)
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_PARTITION_TABLE_FILENAME="partitions.csv"

# FreeRTOS auf 1 kHz Tick — feinere Zeitschritte für unsere Tasks
CONFIG_FREERTOS_HZ=1000
CONFIG_FREERTOS_USE_TRACE_FACILITY=y
CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS=y

# Logging — sichtbar im Monitor
CONFIG_LOG_DEFAULT_LEVEL_INFO=y
CONFIG_LOG_DEFAULT_LEVEL=3
CONFIG_LOG_MAXIMUM_LEVEL=4
CONFIG_LOG_COLORS=n

# Hauptaufgaben-Stack vergrößern (Default 3.5 KB ist eng)
CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096

# Watchdog-Timeout 10 s — bei längeren WiFi-Reconnects nicht zu eng
CONFIG_ESP_TASK_WDT_TIMEOUT_S=10
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y

# Float-Printf einschalten — sonst druckt %f keine Zahlen!
CONFIG_NEWLIB_NANO_FORMAT=n

# CA-Bundle aktivieren — Voraussetzung für mqtts:// gegen HiveMQ Cloud
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
```

Zwei Einträge verdienen einen näheren Blick:

**`CONFIG_IDF_TARGET="esp32c6"`** — alternativ kann man auch
`idf.py set-target esp32c6` aufrufen, was dasselbe Ergebnis hat. Wir
schreiben den Wert direkt in die Defaults, damit ein neuer Klon des
Repos sofort für den richtigen Chip baut.

**`CONFIG_NEWLIB_NANO_FORMAT=n`** — die Default-`nano`-Variante von
`printf` ist kleiner, kann aber kein `%f`. Wer Floats loggen will,
**muss** dies auf `n` stellen. Das war historisch eine der häufigsten
Stolperfallen für Anfänger.

### 4.7 `.gitignore` und Geheimnisse

Zwei Header enthalten Klartext-Credentials und dürfen nie committed
werden:

- `main/wifi_config.h` — SSID + Passwort des WLANs
- `main/mqtt_config.h` — Cloud-MQTT-Username + Passwort

Diese Pfade gehören in `.gitignore`:

```
main/wifi_config.h
main/mqtt_config.h
```

Ein üblicher Workflow ist, neben den echten Headers `.example`-
Versionen zu committen (`wifi_config.example.h`), die jeder Entwickler
zu seiner lokalen Version umbenennt und ausfüllt.

Weitere Standard-Einträge für ESP-IDF-Projekte:

```
build/
sdkconfig
sdkconfig.old
managed_components/
.pio/                # falls jemand versehentlich PlatformIO startet
server/.venv/
server/*.db          # Python-SQLite-Datei nicht committen
```

`dependencies.lock` ist ein Spezialfall: in modernen ESP-IDF-Projekten
**committet man die Datei**, weil sie die exakten Versionen aller
gemanagten Komponenten festhält. Sie *nicht* in `.gitignore` aufnehmen.

---

## 5. Erster Build, Flash und Monitor

### 5.1 ESP-IDF-Umgebung aktivieren

Vor jedem `idf.py`-Aufruf muss die ESP-IDF-Umgebung in der aktuellen
Shell aktiv sein. Espressif liefert dazu Skripte mit:

- **PowerShell (Windows)**: `& "$env:IDF_PATH\export.ps1"` oder
  alternativ aus dem ESP-IDF-Installationsverzeichnis das beigelegte
  `Start ESP-IDF PowerShell` aus dem Startmenü starten.
- **Bash (Linux/macOS)**: `. $IDF_PATH/export.sh`

Erkennen, ob die Umgebung aktiv ist: `idf.py --version` muss eine
Versionsnummer ausgeben, statt "command not found".

### 5.2 Build

```bash
idf.py build
```

Der erste Build dauert deutlich länger als spätere — der Component
Manager lädt eventuelle Dependencies, alle Komponenten werden frisch
übersetzt. Am Ende sollte erscheinen:

```
Project build complete. To flash, run this command:
idf.py -p (PORT) flash
```

Sowie eine Zeile, wie groß das Image ist und wie viel Platz in der
Partition noch frei ist — die sollte deutlich unter 100 % liegen.

### 5.3 Flash und Monitor

```bash
idf.py -p COM5 flash monitor
```

`COM5` durch den eigenen seriellen Port ersetzen. Die Befehle
`flash` und `monitor` lassen sich kombinieren — der Monitor öffnet
sich direkt nach dem Flashen. Mit `Strg+]` wieder verlassen.

### 5.4 Konfiguration nachträglich ändern

```bash
idf.py menuconfig
```

Öffnet ein TUI-Menü, in dem alle `CONFIG_*`-Optionen interaktiv
einstellbar sind. Geänderte Werte landen in `sdkconfig`, **nicht** in
`sdkconfig.defaults`. Wer eine Änderung dauerhaft (für alle Klone des
Repos) festschreiben möchte, muss sie zusätzlich nach
`sdkconfig.defaults` übertragen.

---

## 6. Troubleshooting — typische Fehler bei dieser Migration

**`undefined reference to esp_wifi_init`** o. ä. → fehlt in
`PRIV_REQUIRES` der `main/CMakeLists.txt`. Komponente nachtragen,
`idf.py reconfigure` und neu bauen.

**`driver/gpio.h: No such file or directory`** (oder `driver/i2c_master.h`,
`driver/spi_master.h`, …) → die zugehörige `esp_driver_*`-Komponente
fehlt in `PRIV_REQUIRES`. Für GPIO `esp_driver_gpio`, für I2C
`esp_driver_i2c` etc. ergänzen. Wer keine Lust auf einzeln Auflisten
hat, kann übergangsweise die Sammel-Komponente `driver` nehmen, sollte
aber wissen, dass das in zukünftigen IDF-Versionen wegfallen kann.

**`Image is too large for the configured partition`** → Partitionstabelle
nicht aktiv. Prüfen, ob `CONFIG_PARTITION_TABLE_CUSTOM=y` in der
aktuellen `sdkconfig` (nicht nur in den Defaults!) gesetzt ist. Im
Zweifelsfall `idf.py fullclean && idf.py build`.

**`Partitions tables occupies XMB of flash which does not fit in
configured flash size YMB`** → die Flash-Größe in `sdkconfig` ist zu
klein für die Partitionstabelle. `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`
und `CONFIG_ESPTOOLPY_FLASHSIZE="8MB"` setzen (oder den passenden Wert
für die tatsächliche Flash-Größe des Boards).

**`%f` druckt nichts** → `CONFIG_NEWLIB_NANO_FORMAT` steht auf `y`
(Default). Auf `n` setzen.

**TLS-Handshake schlägt fehl mit `mbedtls_ssl_handshake returned
-0x2700`** → CA-Bundle nicht aktiviert. Prüfen, ob
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y` und
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` gesetzt sind.

**Build greift weiterhin den falschen Chip an** (z. B. baut für ESP32
statt ESP32-C6) → die alte `sdkconfig` aus dem Beispielprojekt steckt
noch im Verzeichnis. Löschen, dann `idf.py build` aus den Defaults
neu generieren lassen, oder explizit `idf.py set-target esp32c6`.

**Neue `.c`-Datei im `main/`-Ordner wird nicht gebaut** → CMake hat
das Glob nicht re-evaluiert. `idf.py reconfigure` oder einmalig
`idf.py fullclean && idf.py build`.

**WiFi/MQTT funktioniert kurz und der ESP startet plötzlich neu** →
Watchdog-Timeout zu kurz. `CONFIG_ESP_TASK_WDT_TIMEOUT_S` erhöhen oder
in den betroffenen Tasks `esp_task_wdt_reset()` regelmäßig aufrufen.

**`error: 'CONFIG_BLINK_GPIO' undeclared`** beim ersten Build → die
alte `Kconfig.projbuild` ist noch da oder das alte
`blink_example_main.c` wurde nicht gelöscht. Beide entfernen.

**`Failed to resolve component 'mqtt' required by component 'main'`**
unter ESP-IDF v6.0+ → die Komponente wurde aus dem Core-Tree in die
Registry verschoben. `main/idf_component.yml` mit
`espressif/mqtt: "*"` anlegen, siehe Abschnitt 4.3. Achtung:
nicht `espressif/esp-mqtt` — der Pfad in der Registry ist ohne
`esp-`-Präfix.

**Build läuft für ESP32 statt ESP32-C6**, obwohl `CONFIG_IDF_TARGET="esp32c6"`
in `sdkconfig.defaults` steht (erkennbar an
`Building ESP-IDF components for target esp32` und am Compiler-Pfad
`xtensa-esp32-elf-gcc.exe` statt `riscv32-esp-elf-gcc.exe`) → das
ESP-IDF-Plugin in VS Code überschreibt das Target via
Umgebungsvariable. Lösung: in der VS-Code-Statusleiste unten den
Eintrag *"esp32"* anklicken (oder Command Palette →
*"ESP-IDF: Set Espressif device target"*) und auf *"esp32c6"*
umstellen. Dasselbe gilt analog, wenn man `idf.py` außerhalb von VS
Code aufruft und vorher die Umgebungsvariable `IDF_TARGET=esp32`
gesetzt war — `Remove-Item Env:IDF_TARGET` (PowerShell) bzw.
`unset IDF_TARGET` (Bash) räumt das auf, danach greift wieder die
`sdkconfig.defaults`.

---

## 7. Was nicht migriert wurde — und warum

### Der Python-Server (`server/`)

Der Flask-/MQTT-Server ist nur eine **1:1-Kopie** der Dateien
(`app.py`, `templates/`, `static/`, `requirements.txt`). Er ist nicht
Teil des ESP-IDF-Build-Systems — er wird separat mit Python gestartet:

```powershell
cd server
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
python app.py
```

Die `.venv/` und die SQLite-Datenbank (`*.db`) wandern **nicht** mit
in den neuen Repo-Stand — sie sind Entwicklerumgebung bzw.
Laufzeit-Daten und werden von `.gitignore` ausgeschlossen.

### Die Geheimnis-Header

`main/wifi_config.h` und `main/mqtt_config.h` werden zwar inhaltlich
übertragen (sonst funktioniert die Firmware nicht), sind aber in
`.gitignore` aufgeführt und gehören **nicht** in irgendein öffentliches
Repository. Wer das Projekt mit dem Vorlesungsteam teilen will, sollte
zusätzlich `wifi_config.example.h` und `mqtt_config.example.h` als
Vorlagen mit Platzhaltern committen.

### Editor- und IDE-Konfiguration

`.vscode/`, `.devcontainer/`, `.clangd` aus dem Originalprojekt sind
nicht migriert. Sie sind entwicklerspezifisch — wer mag, generiert sie
neu mit `idf.py vscode --install` (oder einer ähnlichen Erweiterung).

### Die alte PlatformIO-Konfig (`platformio.ini`, `.pio/`)

Bewusst nicht mitgenommen, weil sie im neuen Projekt keinen Zweck
mehr erfüllt. Wer beide Welten parallel betreiben will (was selten
sinnvoll ist), kann `platformio.ini` neben `CMakeLists.txt`
existieren lassen — beide Build-Systeme respektieren in der Regel
einander, solange sie in unterschiedliche Build-Verzeichnisse
schreiben.

---

## Zusammenfassung in einem Satz

Die Migration besteht im Kern aus drei Bewegungen: **Quellcode** zieht
von `src/`+`inc/` nach `main/`, **Build-Konfiguration** verteilt sich
von der einen `platformio.ini` auf die zwei Welten `CMakeLists.txt`
(was wird kompiliert, mit welchen Komponenten) und `sdkconfig.defaults`
(welcher Chip, welche Partition, welche Compiler-Optionen), und
**Library-Management** entfällt in unserem Fall komplett, weil das
Projekt sowieso keine externen Libs nutzt. Was bleibt, ist sorgfältige
Dokumentation und ein sauberes `.gitignore` für Geheimnisse — und eben
dieses Dokument, damit der nächste Jahrgang den Weg nachvollziehen
kann.
