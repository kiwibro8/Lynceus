# Radar Heartbeat - ESP32-S3 + HLK-LD2450 + ST7789

Sistem complet de monitorizare și urmărire radar în timp real, bazat pe microcontrollerul **ESP32-S3**, senzorul radar undă milimetrică de 24GHz **HLK-LD2450** și ecranul color **ST7789 320x240 SPI**.

---

## 1. Caracteristici principale

- **Arhitectură Dual-Core pe ESP32-S3 (FreeRTOS):**
  - **Core 0:** Task dedicat de fundal pentru citirea continuă a radarului pe UART la 256.000 baud și procesarea filtrelor cinematice.
  - **Core 1:** Rulează bucla grafică (UI la ~35 FPS), controlul LED-ului RGB și procesarea comenzilor de la utilizator.
  - Datele sunt sincronizate în siguranță între nuclee prin stare partajată protejată cu spinlock (`portENTER_CRITICAL`).
- **Afișaj grafic rapid și fără pâlpâire (Double-Buffering):**
  - Comunicare SPI hardware la frecvența de **40 MHz**.
  - Randare completă în memorie (`GFXcanvas16`) înainte de afișare, eliminând complet efectul de pâlpâire (flicker-free).
  - Geometrie precalculată (Look-Up Table) pentru cercuri și linii radiale, evitând calcule trigonometrice costisitoare în fiecare cadru.
- **Filtrare avansată și urmărire cinematică 2D:**
  - **Filtru Anti-Ghost:** Confirmare multi-cadru și filtrare după unghiul conului util pentru eliminarea reflexiilor de pe pereți.
  - **Deadband Anti-Tremurat:** Când o persoană se oprește pe loc, poziția este blocată pe o ancoră fixă pentru a preveni fluctuațiile pe ecran.
  - **Vectori de viteză și urme (Trails):** Fiecare țintă are săgeată de direcție și o urmă vizuală estompată în timp.
- **Indicator vizual de proximitate (NeoPixel WS2812):**
  - Schimbă culoarea fluid în funcție de cel mai apropiat obiect:
    - Sub 1m: Roșu (pericol / apropiere mare)
    - 1m - 2m: Portocaliu
    - 2m - 3.5m: Galben / Verde
    - 3.5m - 5m: Cyan
    - Peste 5m: Albastru
    - Senzor deconectat: Roșu pulsant (efect respirație)

---

## 2. Conexiuni Hardware (Pinout)

### Conexiune Ecran ST7789 (320x240 SPI)
| Pin Ecran ST7789 | Pin ESP32-S3 | Funcție |
| :--- | :--- | :--- |
| **GND** | GND | Masă comună |
| **VCC** | 3.3V / 5V | Alimentare |
| **SCL** | GPIO 12 | SPI Clock (SCLK) |
| **SDA** | GPIO 11 | SPI Data (MOSI) |
| **RES** | GPIO 8 | Reset ecran |
| **DC**  | GPIO 9 | Data / Command |
| **CS**  | GPIO 10 | Chip Select |
| **BLK** | 3.3V (sau lăsat liber) | Backlight |

### Conexiune Radar HLK-LD2450
| Pin LD2450 | Pin ESP32-S3 | Funcție |
| :--- | :--- | :--- |
| **VCC** | 5V | Alimentare 5V |
| **GND** | GND | Masă comună |
| **TX**  | GPIO 4 | Radar TX -> ESP32 RX (256.000 baud) |
| **RX**  | GPIO 5 | Radar RX <- ESP32 TX (256.000 baud) |

---

## 3. Structura Proiectului

```
Lynceus/
├── cad/                  # Fisiere de fabricatie: STL (printare 3D) si STEP (ansamblu mecanic)
├── include/
│   ├── config.h          # Toate setările hardware, pini, culori și praguri
│   ├── radar_types.h     # Structurile de date pentru ținte și starea partajată
│   ├── ld2450.h          # Clasa radarului și definițiile funcțiilor
│   ├── display.h         # Managerul ecranului TFT ST7789
│   └── proximity_led.h   # Modulul de control pentru ledul RGB NeoPixel
├── src/
│   ├── ld2450.cpp        # Decodare pachete 30 bytes, task Core 0 și filtre
│   ├── display.cpp       # Desenare canvas, cercuri, săgeți și telemetrie
│   ├── proximity_led.cpp # Calculul gradientului de culoare după distanță
│   └── main.cpp          # Setup, loop cooperativ și comenzi Serial
├── platformio.ini        # Configurația PlatformIO (ESP32-S3, librării)
├── .gitignore            # Excludere foldere de build (.pio)
└── README.md             # Documentația proiectului
```

---

## 4. Comenzi Serial Monitor (Interactive)

Poți deschide consola Serial la viteza **115200 baud** și tasta următoarele comenzi în timp real:

| Tastă | Acțiune |
| :---: | :--- |
| **A** | Schimbă modul de acuratețe (`NORMAL` -> `PRECIZIE` -> `RAPID`) |
| **G** | Schimbă filtrul anti-reflexii (`ECHILIBRAT` -> `AGRESIV` -> `OPRIT / RAW`) |
| **M** | Activează modul Multi-Target (urmărește până la 3 persoane) |
| **S** | Activează modul Single-Target (concentrare pe 1 persoană) |
| **V** | Interoghează versiunea de firmware a radarului |
| **F** | Resetare la setările din fabrică ale senzorului |
| **R** | Repornire software (reboot) a senzorului |
| **O** | Rotește afișajul ecranului cu 180° |
| **Z** | Comută scara de vizualizare (Zoom 3m <-> Normal 6m) |
| **?** | Afișează lista de ajutor cu toate comenzile |

---

## 5. Compilare și Încărcare (PlatformIO)

Din terminal sau din bara de jos din VS Code / PlatformIO:

```bash
# Compilare proiect
pio run

# Încărcare pe placa ESP32-S3
pio run -t upload
```

---

## 6. Modelare 3D & Credite CAD (Acknowledgments)

Carcasa tactică a radarului **Lynceus MK-I** a fost proiectată special pentru integrarea componentelor și imprimare 3D (fișierele de fabricație se află în directorul `cad/`).

Pentru realizarea ansamblului CAD mecanic (toleranțe, decupaj USB, fixări pe distanțiere și unghi de vizualizare radar), au fost utilizate ca modele dimensionale de referință următoarele piese electronice COTS din comunitatea GrabCAD:
- **Modul ESP32-S3:** [ESP32-S3-WROOM-1](https://grabcad.com/library/esp32-s3-wroom-1-1) (GrabCAD)
- **Senzor radar mmWave:** [HLK-LD2450 24G Human Presence Sensor](https://grabcad.com/library/hlk-ld2450-24g-human-presence-sensor-millimeter-wave-radar-module-1) (GrabCAD)
- **Shield alimentare 18650:** [18650 Battery Shield V8](https://grabcad.com/library/18650-battery-shield-v8_enclosure_r2-1) (GrabCAD) – modulul electronic de alimentare a fost integrat într-un corp de carcasă complet reproiectat și adaptat cerințelor de ergonomie și răcire ale dispozitivului Lynceus.

