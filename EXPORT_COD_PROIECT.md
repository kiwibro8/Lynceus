# Lynceus MK-I - Radar Heartbeat (ESP32-S3 + HLK-LD2450 + ST7789)

Sistem de radar tactic mmWave 24GHz inspirat din Heartbeat Sensor (Call of Duty).
Proiect realizat pe ESP32-S3 cu ecran TFT IPS de 2.0 inch (ST7789) si senzor mmWave HLK-LD2450.

### Cum functioneaza pe scurt:
- **Core 0:** citeste continuu radarul pe serial la 256.000 baud, decodeaza pachetele de 30 octeti, aplica filtrul polar (R, theta) si protectia anti centroid hopping (ancora pe trunchi vs mana cu mouse-ul).
- **Core 1:** deseneaza tot ecranul intr-un canvas de 150KB in SRAM si il trimite dintr-o bucata la 40MHz prin SPI (~35 FPS fara flicker), controleaza ledul RGB de proximitate si asculta comenzile de pe serial.
- **Sincronizare:** transfer sigur de date intre nuclee cu spinlock hardware (portENTER_CRITICAL).


## platformio.ini
`ini
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html
; ESP32-S3 + ST7789 2.0" TFT + HLK-LD2450 Radar

[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_port = COM10
upload_speed = 460800
upload_flags =
    --no-stub

; Serial mapped to UART0 (CH343 / COM10) + compiler speed optimizations
build_flags = 
    -D ARDUINO_USB_CDC_ON_BOOT=0
    -O2
    -ffast-math

lib_deps = 
    adafruit/Adafruit GFX Library @ ^1.11.9
    adafruit/Adafruit ST7735 and ST7789 Library @ ^1.10.3
    adafruit/Adafruit NeoPixel @ ^1.12.0

`


## include/config.h
`cpp
#pragma once
#include <Arduino.h>

// configurari hardware si parametri pentru radar

// pini ecran st7789 (spi pe esp32-s3)
#define TFT_MOSI 11 // date spi (sda / mosi)
#define TFT_SCLK 12 // ceas spi (scl / sck la 40mhz)
#define TFT_CS   10 // chip select
#define TFT_DC   9  // data / command
#define TFT_RST  8  // reset hardware ecran
#define TFT_BLK  -1 // backlight legat direct la 3.3v

// rezolutie si orientare ecran
#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   240
#define SCREEN_ROTATION 1   // mod landscape

// moduri de distanta (in milimetri)
#define RADAR_RANGE_CQB     3000.0f // mod cqb: zoom la 3 metri
#define RADAR_RANGE_LONG    6000.0f // mod normal: 6 metri
#define RADAR_DEFAULT_RANGE RADAR_RANGE_LONG

// dimensiuni canvas in ram (double buffering ca sa nu palpaie ecranul)
#define CANVAS_WIDTH    320
#define CANVAS_HEIGHT   240
#define CANVAS_Y_OFFSET 0
// pozitia noastra pe ecran: centrat pe x, jos pe y
#define CANVAS_ORIGIN_X 160 
#define CANVAS_ORIGIN_Y 235 

#define MAX_TARGETS 3 // radarul stie sa urmareasca maxim 3 tinte o data

// persistenta tintelor pe ecran (milisecunde)
#define TARGET_STALE_BRIGHT_MS 300  // punct aprins in primele 300ms
#define TARGET_STALE_ORANGE_MS 800  // devine mai pal daca radarul pierde semnalul
#define TARGET_STALE_REMOVE_MS 1200 // stergem tinta daca nu mai e vazuta 1.2 secunde
#define TARGET_HOLD_BRIEF_MS   300  // retinere scurta pentru tinte noi
#define SENSOR_DEAD_MS         1500 // intram in modul offline daca nu mai primim date 1.5s
#define RADAR_MIN_DIST_MM      350  // ignoram sub 35cm (carcasa sau degetele noastre)

// unghi de vizualizare (fov)
// folosim tangenta unghiului (|x| <= y * tan) pentru ca e mult mai rapida decat atan2
#define RADAR_MAX_FOV_TAN_BAL  1.428f  // 110 grade (mod normal echilibrat)
#define RADAR_MAX_FOV_TAN_AGGR 1.000f  // 90 grade (mod agresiv pentru holuri/spatii inguste)
#define RADAR_MAX_FOV_TAN_RAW  1.732f  // 120 grade (tot conul senzorului, fara filtrare)
#define RADAR_MAX_FOV_TAN      RADAR_MAX_FOV_TAN_BAL

// praguri ca sa scapam de reflexii false si fantome
#define HUMAN_MIN_MOVE_SPEED_CMS 8    // un om care merge are minim 8 cm/s
#define STATIC_GHOST_HITS_MIN    20   // daca nu se misca, cerem 20 cadre la rand ca sa fim siguri
#define GHOST_CONFIRM_HITS_BALANCED   3    // omul in miscare trebuie vazut in 3 cadre consecutive
#define GHOST_CONFIRM_HITS_AGGRESSIVE 5    // 5 cadre in modul agresiv
#define GHOST_MAX_JUMP_MM             1200 // salt maxim admis intr-un cadru (1.2m la 50ms)
#define GHOST_MIN_TARGET_SEP_MM       350  // distanta minima intre doua persoane diferite

// stabilizare cand omul sta pe loc (zona moarta ca sa nu tremure pe ecran)
#define TRACK_DEADBAND_MM_PRECISION    45.0f // zona moarta 4.5 cm in mod precizie
#define TRACK_DEADBAND_MM_TACTICAL     30.0f // zona moarta 3.0 cm in mod normal
#define TRACK_DEADBAND_MM_RESPONSIVE   15.0f // zona moarta 1.5 cm in mod rapid
#define STATIONARY_SPEED_THRESHOLD_CMS 8     // sub 8 cm/s consideram ca sta pe loc
#define STATIONARY_LOCK_TIME_MS        350   // dupa 350ms blocam pozitia pe ancora
#define TRACK_ASSOCIATION_GATE_MM      900   // daca e la sub 90cm de pozitia veche, e aceeasi persoana

// anti centroid hopping (cand omul sta la birou si misca doar mouse-ul)
// bratul are intre 28 si 95 cm fata de corp, asa ca radarul sare intre piept si mana
#define LIMB_HOP_MIN_DIFF_MM           280.0f // distanta minima piept - mana
#define LIMB_HOP_MAX_DIFF_MM           950.0f // distanta maxima brat intins
#define LIMB_HOP_MAX_ANGLE_RAD         0.45f  // unghiul conului in care se misca mana (~25 grade)
#define LIMB_HOP_SUSTAIN_FRAMES        18     // cerem 18 cadre sustinute ca sa mutam ancora daca chiar s-a ridicat
#define AZIMUTH_DEADBAND_RAD           0.035f // zona moarta de 2 grade pe azimut (elimina reflexiile de pe laptop/birou)
#define AZIMUTH_SMOOTH_FACTOR          0.08f  // filtrare lina pe unghi
#define QUASI_STATIC_HOLD_MS           500    // dupa 500ms de stat nemiscat activam protectia

// coada de puncte pentru efectul de miscare (trail)
#define TRAIL_LEN       10 // tinem minte ultimele 10 pozitii
#define TRAIL_SAMPLE_MS 60 // salvam un punct la fiecare 60ms

// sageata de directie a vitezei
#define ARROW_MIN_SPEED_CMS 5   // nu desenam sageata daca merge cu sub 5 cm/s
#define ARROW_MAX_SPEED_CMS 150 // viteza maxima scalata (1.5 m/s)
#define ARROW_LEN_MIN       6   // lungime minima sageata
#define ARROW_LEN_MAX       22  // lungime maxima sageata

// conexiune serial uart cu radarul ld2450
#define RADAR_RX_PIN 4      // pin rx esp32 (legat la tx radar)
#define RADAR_TX_PIN 5      // pin tx esp32 (legat la rx radar)
#define RADAR_BAUD   256000 // baudrate radar

// led rgb ws2812 de pe placa
#if defined(RGB_BUILTIN)
#define NEOPIXEL_PIN RGB_BUILTIN
#else
#define NEOPIXEL_PIN 48
#endif
#define NEOPIXEL_BRIGHTNESS 64 // 25% luminozitate ca sa nu ne raneasca ochii

// timpi de executie
#define UI_REFRESH_INTERVAL_MS 28   // ~35 fps pe ecran
#define SERIAL_TELEMETRY_MS    1000 // raport pe serial o data pe secunda

// culorile ecranului in format rgb565 (16 biti)
#define COLOR_BG          0x0000 // negru
#define COLOR_HEADER_BG   0x0010 // albastru inchis pentru bara de sus
#define COLOR_HEADER_TXT  0xFFFF // alb
#define COLOR_BORDER      0x0277 // cyan pentru bordura
#define COLOR_RADAR_ARC   0x1945 // verde-albastrui militar pentru cercuri
#define COLOR_RADAR_CONE  0x2104 // linii discrete de unghi
#define COLOR_FOV_EDGE    0x03EF // marginile conului radar
#define COLOR_ORIGIN      0xF800 // rosu pentru pozitia noastra
#define COLOR_SWEEP       0x0277 // fasciculul care baleiaza ecranul
#define COLOR_TEXT_DIM    0x4A69 // gri pentru unitati si text secundar
#define COLOR_STATUS_OK   0x07E0 // verde cand radarul merge
#define COLOR_STATUS_WARN 0xF800 // rosu cand e offline

// culori pentru tinte (stil call of duty)
#define COLOR_TARGET1     0x07FF // tinta 1: cyan
#define COLOR_TARGET2     0xFFE0 // tinta 2: galben
#define COLOR_TARGET3     0xF81F // tinta 3: mov
#define COLOR_TGT_HOT     0xFFFF // miez alb intens

`


## include/radar_types.h
`cpp
#pragma once
#include <Arduino.h>
#include "config.h"

// datele pentru fiecare persoana detectata de radar
struct CODTarget {
  int  id;           // id-ul tintei (1, 2 sau 3, afisat ca T1, T2, T3)
  int  x;            // pozitia stanga/dreapta in mm (negativ la stanga, pozitiv la dreapta)
  int  y;            // distanta in fata in mm
  int  speed;        // viteza totala in cm/s (0 daca sta pe loc)
  int  radialSpeed;  // viteza doppler (+ se departeaza, - se apropie)
  int  vx;           // viteza pe orizontala (pentru directia sagetii)
  int  vy;           // viteza inainte / inapoi
  int  distance;     // distanta directa in mm: sqrt(x^2 + y^2)
  int  resolution;   // rezolutia raportata de senzor
  unsigned long lastSeenMs; // cand a fost vazut ultima oara (in ms)
  bool valid;        // true daca slotul are o persoana reala
  bool isHeld;       // true daca sta pe loc si il tinem pe ecran cu hold [HLD]
};

// un punct salvat pentru dâra de miscare
struct TrailPoint {
  int screenX;              // pixelul X pe ecran
  int screenY;              // pixelul Y pe ecran
  unsigned long timestamp;  // cand a trecut pe acolo
};

// buffer circular pentru ultimele 10 pozitii parcurse
struct TargetTrail {
  TrailPoint points[TRAIL_LEN];
  int head;
  int count;
  unsigned long lastSampleMs;
};

// moduri filtru anti reflexii false
enum GhostFilterMode {
  GHOST_FILTER_OFF = 0,         // fara filtru (transmite direct ce zice senzorul)
  GHOST_FILTER_BALANCED = 1,    // mod normal: con de 110 grade si 3 confirmari
  GHOST_FILTER_AGGRESSIVE = 2   // mod agresiv: con ingust de 90 grade si 5 confirmari
};

// cat de repede reactioneaza la miscare
enum AccuracyMode {
  ACCURACY_TACTICAL   = 0, // mod normal: echilibrat
  ACCURACY_PRECISION  = 1, // mod precizie: blocheaza orice tremurat cand stai pe scaun
  ACCURACY_RESPONSIVE = 2  // mod rapid: reactioneaza instant la orice miscare mica
};

// starea partajata intre cele doua nuclee (copiata cu spinlock intre Core 0 si Core 1)
struct CODSharedState {
  CODTarget       targets[MAX_TARGETS]; // cele 3 sloturi de tinte
  int             activeCount;          // cate persoane sunt active acum
  unsigned long   lastHeartbeatMs;      // cand am primit ultimul pachet valid
  unsigned long   totalPackets;         // total pachete procesate
  unsigned long   totalBytes;           // total octeti cititi pe serial
  uint8_t         rawFrame[30];         // ultimul pachet brut de 30 de octeti
  bool            multiTargetActive;    // mod multi-tinta (3 persoane)
  GhostFilterMode filterMode;           // filtrul curent
  AccuracyMode    accuracyMode;         // modul curent de acuratete
};

`


## include/ld2450.h
`cpp
#pragma once
#include <Arduino.h>
#include "config.h"
#include "radar_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// driver pentru radarul ld2450 (citeste datele pe Core 0 prin FreeRTOS)

// urmarirea interna a fiecarei tinte (ruleaza doar pe Core 0)
struct SlotTracker {
  int hits;                  // de cate ori la rand am vazut persoana
  unsigned long firstSeenMs; // cand a aparut prima oara (ms)
  unsigned long lastSeenMs;  // cand am primit ultimul cadru cu ea
  int lastRawX;              // coordonata bruta X anterioara (sa prindem salturi bruste)
  int lastRawY;              // coordonata bruta Y anterioara
  bool confirmed;            // true dupa ce trece de debounce (o desenam pe ecran)
  bool hasMoved;             // true daca a depasit macar o data viteza de mers (>= 8 cm/s)

  // valori filtrate pentru pozitie si viteza
  float estX;                // pozitia estimata pe orizontala (mm)
  float estY;                // distanta estimata in fata (mm)
  float estVx;               // viteza estimata pe X (mm/s)
  float estVy;               // viteza estimata pe Y (mm/s)
  unsigned long lastUpdateMs; // ultimul pas de calcul

  // ancora fixa cand omul sta pe loc (ca sa nu tremure pe ecran)
  float anchorX;
  float anchorY;
  unsigned long stationaryStartMs;
  bool isStationary;         // true daca sta pe loc (sub 8 cm/s)

  // coordonate polare (decuplam distanta de unghiul lateral)
  float estR;                // distanta directa in mm: sqrt(X^2 + Y^2)
  float estAngle;            // unghiul de azimut in radiani: atan2(X, Y)

  // ancora pentru trunchi cand sta pe scaun la birou
  float torsoAnchorR;        // distanta stabila a pieptului (~2.1 metri de exemplu)
  float torsoAnchorAngle;    // unghiul stabil al pieptului
  bool  isQuasiStatic;       // true daca sta pe scaun de peste 500ms
  int   limbHopHits;         // cadre consecutive cu miscarea mainii in fata
};

// date brute extrase din pachetul uart
struct RawCandidate {
  int x;          // coordonata X bruta in mm
  int y;          // distanta bruta Y in mm
  int speed;      // viteza doppler in cm/s
  int resolution; // rezolutia raportata de radar
  int distance;   // distanta directa in mm
  bool valid;     // true daca e in conul radarului si peste 35cm
};

class LD2450Radar {
public:
  LD2450Radar();
  ~LD2450Radar();

  // porneste portul serial la 256.000 baud si porneste task-ul de fundal pe Core 0
  bool begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud);

  // trimite o copie sigura a datelor catre ecran (cu spinlock)
  CODSharedState getSnapshot();

  // verifica daca radarul inca raspunde (sub 1.5s de la ultimul pachet)
  bool isConnected();

  // comuta filtrul anti reflexii (normal / agresiv / raw)
  void setGhostFilterMode(GhostFilterMode mode);
  GhostFilterMode getGhostFilterMode();

  // comuta sensibilitatea la miscare (tactical / precision / responsive)
  void setAccuracyMode(AccuracyMode mode);
  AccuracyMode getAccuracyMode();

  // comenzi de configurare pentru radar
  bool setMultiTargetMode(bool enable); // mod 3 tinte vs o singura tinta
  bool factoryReset();                  // reset la setarile din fabrica
  bool reboot();                        // repornire cip radar
  String queryFirmwareVersion();        // citeste versiunea de firmware

private:
  HardwareSerial* _serial;     // portul serial hardware (Serial1)
  TaskHandle_t    _taskHandle; // task-ul FreeRTOS de pe Core 0
  portMUX_TYPE    _mux;        // spinlock hardware pentru sincronizare cu Core 1

  CODSharedState  _sharedState;              // datele partajate cu ecranul
  SlotTracker     _slotTrackers[MAX_TARGETS];// cele 3 trackere de calcul
  GhostFilterMode _filterMode;               // modul curent de filtrare
  AccuracyMode    _accuracyMode;             // modul curent de raspuns

  // functiile interne care ruleaza continuu pe Core 0
  static void sensorTaskWrapper(void* param); // wrapper-ul cerut de FreeRTOS
  void sensorTaskLoop();                      // bucla de citire din serial

  bool feedFrameByte(uint8_t byte, uint8_t* frameBuffer, int &frameIndex); // asamblare pachet (30 octeti)
  void processFrame(const uint8_t* frame, uint32_t byteCount);             // procesarea completa a unui cadru

  void decodeCandidates(const uint8_t* frame, RawCandidate* candidates, float maxFovTan);
  void associateCandidates(RawCandidate* candidates, int8_t* slotMap);
  void filterTrackers(RawCandidate* candidates, const int8_t* slotMap, int reqHits, unsigned long now, AccuracyMode accMode);
  void suppressDuplicates(RawCandidate* candidates, const int8_t* slotMap);
  void publishTargetState(const uint8_t* frame, const RawCandidate* candidates, const int8_t* slotMap, uint32_t byteCount, unsigned long now, GhostFilterMode mode, AccuracyMode accMode);

  void updateKinematicFilter(SlotTracker &tr, const RawCandidate &cand, unsigned long now, AccuracyMode accMode);

  static int decodeCoordinate(uint8_t lo, uint8_t hi); // decodare format sign-magnitude pe 16 biti
  static int decodeSpeed(uint8_t lo, uint8_t hi);      // decodare viteza doppler

  // functii pentru comenzi de configurare
  bool sendCommand(uint16_t cmd, const uint8_t* val, uint16_t valLen,
                   uint8_t* respOut = NULL, uint8_t respMax = 0,
                   uint8_t* actualLen = NULL, uint32_t timeoutMs = 250);
  bool enterConfig(); // deschide modul configurare (0x00FF)
  bool exitConfig();  // inchide modul configurare (0x00FE)
};

`


## src/ld2450.cpp
`cpp
#include "ld2450.h"
#include <math.h>
#include <string.h>

// initializam radarul si variabilele interne
LD2450Radar::LD2450Radar() {
  _serial = NULL;
  _taskHandle = NULL;
  _mux = portMUX_INITIALIZER_UNLOCKED; // mutex pentru transfer de date intre Core 0 si Core 1
  _filterMode = GHOST_FILTER_BALANCED;
  _accuracyMode = ACCURACY_TACTICAL;

  memset(&_sharedState, 0, sizeof(_sharedState));
  _sharedState.multiTargetActive = true;
  _sharedState.filterMode = GHOST_FILTER_BALANCED;
  _sharedState.accuracyMode = ACCURACY_TACTICAL;

  // pornim cu sloturile de tinte goale
  for (int i = 0; i < MAX_TARGETS; i++) {
    _sharedState.targets[i].id = i + 1;
    _slotTrackers[i] = {
      0, 0, 0, 0, 0, false, false,
      0.0f, 0.0f, 0.0f, 0.0f, 0,
      0.0f, 0.0f, 0, false,
      0.0f, 0.0f,
      0.0f, 0.0f, false, 0
    };
  }
}

LD2450Radar::~LD2450Radar() {
  if (_taskHandle != NULL) {
    vTaskDelete(_taskHandle);
    _taskHandle = NULL;
  }
}

// pornim serialul la 256k baud si lansam citirea pe Core 0
bool LD2450Radar::begin(HardwareSerial &serial, int rxPin, int txPin, uint32_t baud) {
  _serial = &serial;
  _serial->setRxBufferSize(1024); // buffer mai mare ca sa nu pierdem pachete la 256k
  _serial->begin(baud, SERIAL_8N1, rxPin, txPin);

  portENTER_CRITICAL(&_mux);
  memset(&_sharedState, 0, sizeof(_sharedState));
  _sharedState.multiTargetActive = true;
  _sharedState.filterMode = _filterMode;
  _sharedState.accuracyMode = _accuracyMode;
  for (int i = 0; i < MAX_TARGETS; i++) {
    _sharedState.targets[i].id = i + 1;
    _slotTrackers[i] = {
      0, 0, 0, 0, 0, false, false,
      0.0f, 0.0f, 0.0f, 0.0f, 0,
      0.0f, 0.0f, 0, false,
      0.0f, 0.0f,
      0.0f, 0.0f, false, 0
    };
  }
  portEXIT_CRITICAL(&_mux);

  delay(100);

  // activam modul multi-tinta pe radar
  bool multiOk = setMultiTargetMode(true);
  Serial.print("Initializare radar modul Multi-Target: ");
  Serial.println(multiOk ? "OK" : "REINCERCAM...");

  // cream task-ul pe Core 0 separat de ecran
  BaseType_t res = xTaskCreatePinnedToCore(
    sensorTaskWrapper,
    "LD2450_Core0",
    4096,
    this,
    2,
    &_taskHandle,
    0
  );

  return (res == pdPASS);
}

void LD2450Radar::sensorTaskWrapper(void* param) {
  LD2450Radar* radar = (LD2450Radar*)param;
  radar->sensorTaskLoop();
}

// senzorul HLK-LD2450 foloseste un format special pe 16 biti numit "Sign-Magnitude" (semn + marime),
// NU complement fata de doi (two's complement) cum e standardul in procesoare!
// - Bitul 15 este bitul de semn: 1 inseamna pozitiv (+), 0 inseamna negativ (-).
// - Bitii 0..14 reprezinta valoarea absoluta (marimea in milimetri).
// Daca am fi facut un simplu cast la int16_t, numerele negative ar fi fost complet gresite!
int LD2450Radar::decodeCoordinate(uint8_t lo, uint8_t hi) {
  uint16_t raw = (uint16_t)(lo | (hi << 8));
  int magnitudine = (int)(raw & 0x7FFF); // stergem bitul 15 cu masca 0x7FFF ca sa ramanem doar cu valoarea
  if (raw & 0x8000) {
    return magnitudine;  // bitul 15 este 1 -> valoare pozitiva (la dreapta senzorului)
  } else {
    return -magnitudine; // bitul 15 este 0 -> valoare negativa (la stanga senzorului)
  }
}

// la fel ca la coordonate, viteza Doppler (in cm/s) este codificata in format Sign-Magnitude:
// bitul 15 = 1 inseamna viteza pozitiva (tinta se indeparteaza de radar)
// bitul 15 = 0 inseamna viteza negativa (tinta se apropie de radar)
int LD2450Radar::decodeSpeed(uint8_t lo, uint8_t hi) {
  uint16_t raw = (uint16_t)(lo | (hi << 8));
  int magnitudine = (int)(raw & 0x7FFF);
  if (raw & 0x8000) {
    return magnitudine;
  } else {
    return -magnitudine;
  }
}

// bucla continua a task-ului de fundal (ruleaza dedicat pe Nucleul 0 al procesorului ESP32-S3).
// avand nucleu separat, putem citi serialul la 256.000 baud fara sa pierdem niciun octet,
// chiar daca pe Nucleul 1 ecranul TFT e ocupat cu desenarea la 40 MHz.
void LD2450Radar::sensorTaskLoop() {
  uint8_t rxBuffer[64];
  int bufferIndex = 0;
  uint32_t pendingBytes = 0;

  for (;;) {
    if (_serial == NULL) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    int avail = _serial->available();
    if (avail <= 0) {
      // daca nu sunt date in buffer, lasam procesorul sa respire 5ms
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    // citim pachetul in bucati sigure de maxim 64 de octeti
    uint8_t chunk[64];
    int deCitit = (avail > (int)sizeof(chunk)) ? (int)sizeof(chunk) : avail;
    int bytesCititi = _serial->read(chunk, deCitit);
    pendingBytes += bytesCititi;

    // trimitem fiecare octet primit catre masina de stari care reasambleaza cadrul
    for (int i = 0; i < bytesCititi; i++) {
      if (feedFrameByte(chunk[i], rxBuffer, bufferIndex)) {
        // am gasit un pachet complet si valid de 30 octeti -> il procesam imediat!
        processFrame(rxBuffer, pendingBytes);
        pendingBytes = 0;
        bufferIndex = 0;
      }
    }

    // cedam scurt controlul planificatorului FreeRTOS daca mai sunt alte task-uri de sistem
    taskYIELD();
  }
}

// masina de stari finita (FSM) care identifica si valideaza cadrele primite de la radar.
// structura unui cadru HLK-LD2450 este intotdeauna fixa (30 de octeti):
// - 4 octeti Antet: 0xAA 0xFF 0x03 0x00
// - 24 octeti Date: cate 8 octeti pentru fiecare din cele 3 tinte (X, Y, Viteza, Rezolutie)
// - 2 octeti Coada: 0x55 0xCC
bool LD2450Radar::feedFrameByte(uint8_t byte, uint8_t* frameBuffer, int &frameIndex) {
  if (frameIndex == 0) {
    // cautam primul octet de antet: 0xAA
    if (byte == 0xAA) frameBuffer[frameIndex++] = byte;
  } else if (frameIndex == 1) {
    // al doilea octet: 0xFF
    if (byte == 0xFF) frameBuffer[frameIndex++] = byte;
    else frameIndex = (byte == 0xAA) ? 1 : 0;
  } else if (frameIndex == 2) {
    // al treilea octet: 0x03
    if (byte == 0x03) frameBuffer[frameIndex++] = byte;
    else frameIndex = (byte == 0xAA) ? 1 : 0;
  } else if (frameIndex == 3) {
    // al patrulea octet: 0x00
    if (byte == 0x00) frameBuffer[frameIndex++] = byte;
    else frameIndex = (byte == 0xAA) ? 1 : 0;
  } else {
    // am gasit antetul complet, acum colectam corpul pachetului pana la 30 de octeti
    frameBuffer[frameIndex++] = byte;

    if (frameIndex >= 30) {
      // verificam coada pachetului: trebuie sa fie 0x55 urmat de 0xCC
      if (frameBuffer[28] == 0x55 && frameBuffer[29] == 0xCC) {
        return true; // pachetul este 100% integru si complet!
      }

      // daca coada nu se potriveste (zgomot pe cablu sau octeti pierduti), nu aruncam tot bufferul!
      // cautam daca nu cumva in interiorul pachetului exista un nou 0xAA si ne sincronizam de acolo.
      int nextHdr = -1;
      for (int k = 1; k < 30; k++) {
        if (frameBuffer[k] == 0xAA) {
          nextHdr = k;
          break;
        }
      }
      if (nextHdr != -1) {
        int ramas = 30 - nextHdr;
        memmove(frameBuffer, &frameBuffer[nextHdr], ramas);
        frameIndex = ramas;
      } else {
        frameIndex = 0;
      }
    }
  }
  return false;
}

// functia centrala apelata la fiecare pachet valid receptionat (de ~20 de ori pe secunda)
void LD2450Radar::processFrame(const uint8_t* frame, uint32_t byteCount) {
  unsigned long now = millis();

  // preluam modurile active de filtrare protejat prin spinlock
  GhostFilterMode gMode;
  AccuracyMode    aMode;
  portENTER_CRITICAL(&_mux);
  gMode = _filterMode;
  aMode = _accuracyMode;
  portEXIT_CRITICAL(&_mux);

  // calculam tangenta conului de vizualizare in functie de modul ales:
  // - normal: con util de 110 grade (taie peretii laterali din camere mici)
  // - agresiv: con ingust de 90 grade
  // - raw: con maxim de 120 grade fara nicio limitare
  float maxFovTan = (gMode == GHOST_FILTER_AGGRESSIVE) ? RADAR_MAX_FOV_TAN_AGGR :
                    (gMode == GHOST_FILTER_OFF)        ? RADAR_MAX_FOV_TAN_RAW  :
                                                          RADAR_MAX_FOV_TAN_BAL;

  // numarul de cadre consecutive cerute ca sa acceptam o tinta (debounce temporal)
  int reqHits = (gMode == GHOST_FILTER_AGGRESSIVE) ? GHOST_CONFIRM_HITS_AGGRESSIVE :
                                                     GHOST_CONFIRM_HITS_BALANCED;

  RawCandidate candidates[MAX_TARGETS];

  // Pasul 1: decodam coordonatele celor 3 tinte din octetii pachetului binar
  decodeCandidates(frame, candidates, maxFovTan);

  int8_t slotMap[MAX_TARGETS] = {-1, -1, -1};

  if (gMode != GHOST_FILTER_OFF) {
    // Pasul 2: asociem punctele noi cu tintele vechi (ca sa nu sara ID-urile si culorile T1/T2/T3)
    associateCandidates(candidates, slotMap);

    // Pasul 3: aplicam filtrul cinematic, ancora de trunchi si stabilizarea azimutala anti-tremurat
    filterTrackers(candidates, slotMap, reqHits, now, aMode);

    // Pasul 4: stergem punctele dublate (daca radarul vede si mana si trunchiul ca doua tinte)
    suppressDuplicates(candidates, slotMap);
  }

  // Pasul 5: copiem starea filtrata in structura partajata de unde o citeste ecranul
  publishTargetState(frame, candidates, slotMap, byteCount, now, gMode, aMode);
}

// extrage coordonatele brute ale celor 3 tinte posibile din sarcina utila de 24 octeti a pachetului.
// fiecare tinta ocupa cate 8 octeti consecutivi:
// - octetii 0..1: coordonata X (lo, hi)
// - octetii 2..3: coordonata Y (lo, hi)
// - octetii 4..5: viteza Doppler (lo, hi)
// - octetii 6..7: rezolutia distantei
void LD2450Radar::decodeCandidates(const uint8_t* frame, RawCandidate* candidates, float maxFovTan) {
  for (int i = 0; i < MAX_TARGETS; i++) {
    int offset = 4 + (i * 8); // sarim peste cei 4 octeti de antet

    int x = decodeCoordinate(frame[offset], frame[offset + 1]);
    int y = decodeCoordinate(frame[offset + 2], frame[offset + 3]);
    if (y < 0) y = -y; // distanta in fata senzorului este intotdeauna pozitiva

    int spd = decodeSpeed(frame[offset + 4], frame[offset + 5]);
    int res = (int)(frame[offset + 6] | (frame[offset + 7] << 8));
    // distanta euclidiana directa: d = sqrt(x^2 + y^2)
    int dist = (int)roundf(sqrtf((float)(x * x + y * y)));

    candidates[i] = {x, y, spd, res, dist, false};

    // validam ca punctul este real si se incadreaza in limitele fizice plauzibile:
    if (x != 0 || y != 0) {
      if (res > 0 &&
          y >= 100 &&                      // cel putin 10 cm in fata
          dist >= RADAR_MIN_DIST_MM &&     // ignoram reflexiile la sub 35 cm (carcasa, degete)
          dist <= (int)RADAR_RANGE_LONG && // maxim 6 metri distanta
          abs(x) <= (int)(y * maxFovTan))  // in interiorul conului util de 110 grade (taie reflexii din pereti)
      {
        candidates[i].valid = true;
      }
    }
  }
}

// asocierea punctelor noi cu tintele vechi (ca sa nu sara culorile la fiecare cadru)
void LD2450Radar::associateCandidates(RawCandidate* candidates, int8_t* slotMap) {
  bool folosit[MAX_TARGETS] = {false, false, false};

  // intai legam sloturile deja confirmate de punctele cele mai apropiate
  for (int trkIdx = 0; trkIdx < MAX_TARGETS; trkIdx++) {
    SlotTracker &tr = _slotTrackers[trkIdx];
    if (!tr.confirmed || tr.hits == 0) continue;

    int   bestCand = -1;
    float bestDist = (float)TRACK_ASSOCIATION_GATE_MM; // maxim 90cm salt admis

    for (int c = 0; c < MAX_TARGETS; c++) {
      if (!candidates[c].valid || folosit[c]) continue;

      float dx = (float)candidates[c].x - tr.estX;
      float dy = (float)candidates[c].y - tr.estY;
      float d = sqrtf(dx * dx + dy * dy);

      // daca sta la birou, masuram si fata de ancora de trunchi
      if (tr.isQuasiStatic || tr.isStationary) {
        float dxAnc = (float)candidates[c].x - tr.anchorX;
        float dyAnc = (float)candidates[c].y - tr.anchorY;
        float dAnc = sqrtf(dxAnc * dxAnc + dyAnc * dyAnc);
        if (dAnc < d) d = dAnc;
      }

      if (d < bestDist) {
        bestDist = d;
        bestCand = c;
      }
    }

    if (bestCand != -1) {
      slotMap[trkIdx] = bestCand;
      folosit[bestCand] = true;
    }
  }

  // punctele noi ramase le punem pe primul slot liber
  for (int c = 0; c < MAX_TARGETS; c++) {
    if (!candidates[c].valid || folosit[c]) continue;

    for (int trkIdx = 0; trkIdx < MAX_TARGETS; trkIdx++) {
      if (slotMap[trkIdx] == -1 && !_slotTrackers[trkIdx].confirmed) {
        slotMap[trkIdx] = c;
        folosit[c] = true;
        break;
      }
    }
  }
}

// filtru cinematic polar si protectie anti centroid hopping (stabilizare trunchi vs mana)
void LD2450Radar::updateKinematicFilter(SlotTracker &tr, const RawCandidate &cand, unsigned long now, AccuracyMode accMode) {
  // calculam dt intre cadre (in secunde)
  float dt = (tr.lastUpdateMs > 0) ? ((float)(now - tr.lastUpdateMs) * 0.001f) : 0.10f;
  if (dt < 0.02f) dt = 0.02f; // minim 20ms
  if (dt > 0.30f) dt = 0.10f; // maxim 100ms

  float rawCandX = (float)cand.x;
  float rawCandY = (float)cand.y;
  float rawR = (float)cand.distance;
  if (rawR < 100.0f) rawR = sqrtf(rawCandX * rawCandX + rawCandY * rawCandY);

  // transformam in coordonate polare: rawAngle este azimutul in radiani (-pi/2 .. +pi/2)
  float rawAngle = atan2f(rawCandX, rawCandY);

  // la prima aparitie pe ecran initializam direct variabilele
  if (!tr.confirmed || tr.hits <= 1) {
    tr.estX = rawCandX;
    tr.estY = rawCandY;
    tr.estVx = 0.0f;
    tr.estVy = 0.0f;
    tr.estR = rawR;
    tr.estAngle = rawAngle;
    tr.anchorX = rawCandX;
    tr.anchorY = rawCandY;
    tr.torsoAnchorR = rawR;
    tr.torsoAnchorAngle = rawAngle;
    tr.stationaryStartMs = now;
    tr.isStationary = (abs(cand.speed) <= STATIONARY_SPEED_THRESHOLD_CMS);
    tr.isQuasiStatic = false;
    tr.limbHopHits = 0;
    tr.lastUpdateMs = now;
    return;
  }

  // estimam unde ar trebui sa fie corpul: x_pred = x + vx * dt
  float predX = tr.estX + tr.estVx * dt;
  float predY = tr.estY + tr.estVy * dt;

  float rx = rawCandX - predX;
  float ry = rawCandY - predY;
  float distantaEroare = sqrtf(rx * rx + ry * ry);
  int vitezaDoppler = abs(cand.speed);

  // daca sta nemiscat peste 350ms -> isStationary, peste 500ms -> isQuasiStatic
  if (vitezaDoppler <= STATIONARY_SPEED_THRESHOLD_CMS) {
    if (tr.stationaryStartMs == 0) tr.stationaryStartMs = now;
    if (now - tr.stationaryStartMs >= STATIONARY_LOCK_TIME_MS) {
      tr.isStationary = true;
    }
    if (now - tr.stationaryStartMs >= QUASI_STATIC_HOLD_MS) {
      tr.isQuasiStatic = true;
    }
  } else if (vitezaDoppler >= 25 || distantaEroare > 1000.0f) {
    // s-a ridicat si merge, dezactivam ancorele ca sa nu avem lag
    tr.isStationary = false;
    tr.isQuasiStatic = false;
    tr.stationaryStartMs = 0;
    tr.limbHopHits = 0;
  }

  // anti centroid hopping: trunchi vs mana cu mouse-ul
  if (tr.isQuasiStatic || tr.isStationary) {
    float diffR = tr.torsoAnchorR - rawR; // pozitiv daca noul punct e mai aproape de radar
    float diffAngle = fabsf(rawAngle - tr.torsoAnchorAngle);

    // mana la birou e la 28..95cm in fata trunchiului, pe acelasi con (~25 grade)
    bool isLimbHop = (diffR >= LIMB_HOP_MIN_DIFF_MM && diffR <= LIMB_HOP_MAX_DIFF_MM && diffAngle <= LIMB_HOP_MAX_ANGLE_RAD);

    if (isLimbHop) {
      tr.limbHopHits++;
      // sub 18 cadre ignoram mana si tinem punctul blocat pe trunchi
      if (tr.limbHopHits < LIMB_HOP_SUSTAIN_FRAMES) {
        rawR = tr.torsoAnchorR;
        rawAngle = tr.torsoAnchorAngle;
        rawCandX = tr.anchorX;
        rawCandY = tr.anchorY;
      } else {
        // daca a ramas nemiscat acolo peste 1 secunda, actualizam ancora
        tr.torsoAnchorR = rawR;
        tr.torsoAnchorAngle = rawAngle;
        tr.anchorX = rawCandX;
        tr.anchorY = rawCandY;
        tr.limbHopHits = 0;
      }
    } else {
      if (fabsf(rawR - tr.torsoAnchorR) < LIMB_HOP_MIN_DIFF_MM) {
        tr.limbHopHits = 0;
      }
    }
  }

  // filtrare azimut: zona moarta de 2 grade ca sa nu oscileze stanga-dreapta de la birou sau ecranul laptopului
  float deltaAngle = rawAngle - tr.estAngle;
  while (deltaAngle > 3.14159f) deltaAngle -= 6.28318f;
  while (deltaAngle < -3.14159f) deltaAngle += 6.28318f;

  if (tr.isStationary || tr.isQuasiStatic) {
    if (fabsf(deltaAngle) < AZIMUTH_DEADBAND_RAD) {
      deltaAngle = 0.0f;
    } else {
      float alphaAngle = (accMode == ACCURACY_PRECISION) ? 0.04f : AZIMUTH_SMOOTH_FACTOR;
      deltaAngle *= alphaAngle;
    }
  } else {
    float alphaAngle = (accMode == ACCURACY_RESPONSIVE) ? 0.80f : 0.60f;
    deltaAngle *= alphaAngle;
  }
  tr.estAngle += deltaAngle;

  // filtrare distanta radiala
  float deltaR = rawR - tr.estR;
  if (tr.isStationary || tr.isQuasiStatic) {
    float deadbandR = (accMode == ACCURACY_PRECISION)  ? TRACK_DEADBAND_MM_PRECISION :
                      (accMode == ACCURACY_RESPONSIVE) ? TRACK_DEADBAND_MM_RESPONSIVE :
                                                         TRACK_DEADBAND_MM_TACTICAL;
    if (fabsf(deltaR) < deadbandR) {
      deltaR = 0.0f;
    } else {
      deltaR *= 0.12f;
      tr.torsoAnchorR = tr.estR;
      tr.torsoAnchorAngle = tr.estAngle;
      tr.anchorX = tr.estX;
      tr.anchorY = tr.estY;
    }
  } else {
    float alphaR = (accMode == ACCURACY_RESPONSIVE) ? 0.80f : 0.60f;
    deltaR *= alphaR;
  }
  tr.estR += deltaR;

  // reconstruim coordonatele carteziene (X, Y) din valorile polare (R, unghi) filtrate
  // formula: X = R * sin(unghi), Y = R * cos(unghi)
  float newX = tr.estR * sinf(tr.estAngle);
  float newY = tr.estR * cosf(tr.estAngle);
  if (newY < 100.0f) newY = 100.0f;

  // calculam viteza vectoriala interna din deplasarea pe milisecunda
  if (tr.isStationary) {
    tr.estVx = 0.0f;
    tr.estVy = 0.0f;
  } else {
    float vxInst = (newX - tr.estX) / dt;
    float vyInst = (newY - tr.estY) / dt;
    float betaV = 0.30f;
    tr.estVx = (1.0f - betaV) * tr.estVx + betaV * vxInst;
    tr.estVy = (1.0f - betaV) * tr.estVy + betaV * vyInst;
    // limitam viteza maxima la 4 m/s (nicio persoana nu alearga mai repede in casa)
    tr.estVx = constrain(tr.estVx, -4000.0f, 4000.0f);
    tr.estVy = constrain(tr.estVy, -4000.0f, 4000.0f);
  }

  tr.estX = newX;
  tr.estY = newY;
  tr.lastUpdateMs = now;
}

// confirmare pe mai multe cadre consecutive (Debounce temporal) si protectie la sarituri aberante
void LD2450Radar::filterTrackers(RawCandidate* candidates, const int8_t* slotMap, int reqHits, unsigned long now, AccuracyMode accMode) {
  for (int trkIdx = 0; trkIdx < MAX_TARGETS; trkIdx++) {
    SlotTracker &tr = _slotTrackers[trkIdx];
    int8_t candIdx = slotMap[trkIdx];

    if (candIdx >= 0 && candidates[candIdx].valid) {
      const auto &cand = candidates[candIdx];

      // daca o tinta sare dintr-un cadru in altul cu peste 1.2 metri (viteza de 24 m/s, imposibila fizic),
      // e clar o reflexie parazita sau un glitch al senzorului
      if (tr.confirmed && tr.hits > 0) {
        int dx = cand.x - tr.lastRawX;
        int dy = cand.y - tr.lastRawY;
        if ((dx * dx + dy * dy) > (GHOST_MAX_JUMP_MM * GHOST_MAX_JUMP_MM)) {
          // daca omul este deja confirmat si sta la birou, NU resetam trackerul! Doar ignoram cadrul aberant
          if (tr.isQuasiStatic || tr.isStationary) {
            continue;
          } else {
            tr.hits = 0;
            tr.confirmed = false;
            tr.hasMoved = false;
            tr.firstSeenMs = now;
          }
        }
      }

      if (tr.hits == 0) {
        tr.firstSeenMs = now;
        tr.hasMoved = false;
      }
      if (tr.hits < 255) tr.hits++;
      tr.lastSeenMs = now;
      tr.lastRawX = cand.x;
      tr.lastRawY = cand.y;

      // daca a atins viteza reala de mers (>= 8 cm/s), o marcam ca fiind o fiinta umana
      if (abs(cand.speed) >= HUMAN_MIN_MOVE_SPEED_CMS) {
        tr.hasMoved = true;
      }

      updateKinematicFilter(tr, cand, now, accMode);

      // logica de confirmare a tintei:
      // - daca a fost in miscare: o confirmam foarte rapid dupa 3..5 cadre (~150-250ms)
      // - daca a aparut complet nemiscata (reflexie de mobilier, perdea, monitor): cerem 20 de cadre (~1s)
      if (tr.hasMoved && tr.hits >= reqHits) {
        tr.confirmed = true;
      } else if (!tr.hasMoved && tr.hits >= STATIC_GHOST_HITS_MIN) {
        tr.confirmed = true;
      }
    } else {
      if (!tr.confirmed) {
        tr.hits = 0;
        tr.hasMoved = false;
      }
    }
  }
}

// deduplicare spatiala si suprimarea fantomelor de membre
void LD2450Radar::suppressDuplicates(RawCandidate* candidates, const int8_t* slotMap) {
  for (int i = 0; i < MAX_TARGETS; i++) {
    if (!_slotTrackers[i].confirmed || slotMap[i] < 0) continue;

    for (int j = i + 1; j < MAX_TARGETS; j++) {
      if (!_slotTrackers[j].confirmed || slotMap[j] < 0) continue;

      float dx = _slotTrackers[i].estX - _slotTrackers[j].estX;
      float dy = _slotTrackers[i].estY - _slotTrackers[j].estY;
      float distSq = dx * dx + dy * dy;

      // Cazul 1: doua tinte sunt la mai putin de 35 cm una de alta.
      // corpul uman are o grosime fizica; radarul vede pieptul si spatele ca doua puncte.
      // le comasam si pastram doar punctul mai vechi/stabil
      if (distSq < ((float)GHOST_MIN_TARGET_SEP_MM * GHOST_MIN_TARGET_SEP_MM)) {
        if (_slotTrackers[i].hits >= _slotTrackers[j].hits) {
          candidates[slotMap[j]].valid = false;
          _slotTrackers[j].confirmed = false;
          _slotTrackers[j].hits = 0;
        } else {
          candidates[slotMap[i]].valid = false;
          _slotTrackers[i].confirmed = false;
          _slotTrackers[i].hits = 0;
        }
        continue;
      }

      // Cazul 2: una dintre tinte este trunchiul stabil la birou, iar cealalta tinta a aparut
      // pe birou ca mana aceleiasi persoane (la 28cm..95cm in fata trunchiului pe aceeasi linie)
      if (_slotTrackers[i].isQuasiStatic || _slotTrackers[j].isQuasiStatic) {
        int torsoIdx = _slotTrackers[i].isQuasiStatic ? i : j;
        int otherIdx = (torsoIdx == i) ? j : i;

        float diffR = _slotTrackers[torsoIdx].estR - _slotTrackers[otherIdx].estR;
        float latDiff = fabsf(_slotTrackers[torsoIdx].estX - _slotTrackers[otherIdx].estX);

        if (diffR >= LIMB_HOP_MIN_DIFF_MM && diffR <= LIMB_HOP_MAX_DIFF_MM && latDiff < 350.0f) {
          // este mana persoanei de la birou -> suprimam duplicatul ca sa nu apara al doilea punct pe ecran
          candidates[slotMap[otherIdx]].valid = false;
          _slotTrackers[otherIdx].confirmed = false;
          _slotTrackers[otherIdx].hits = 0;
        }
      }
    }
  }
}

// publicarea starii finale catre bucla grafica de pe Core 1
void LD2450Radar::publishTargetState(const uint8_t* frame, const RawCandidate* candidates, const int8_t* slotMap,
                                     uint32_t byteCount, unsigned long now, GhostFilterMode gMode, AccuracyMode aMode) {
  // blocam spinlock-ul hardware pentru transfer atomic intre nuclee
  portENTER_CRITICAL(&_mux);
  _sharedState.totalBytes += byteCount;
  int activeCount = 0;

  for (int i = 0; i < MAX_TARGETS; i++) {
    SlotTracker &tr = _slotTrackers[i];
    _sharedState.targets[i].id = i + 1;

    // daca utilizatorul a oprit filtrul (modul RAW), transmitem datele brute nemodificate
    if (gMode == GHOST_FILTER_OFF) {
      if (candidates[i].x != 0 || candidates[i].y != 0) {
        _sharedState.targets[i].x = candidates[i].x;
        _sharedState.targets[i].y = candidates[i].y;
        _sharedState.targets[i].speed = abs(candidates[i].speed);
        _sharedState.targets[i].radialSpeed = candidates[i].speed;
        _sharedState.targets[i].vx = 0;
        _sharedState.targets[i].vy = candidates[i].speed;
        _sharedState.targets[i].resolution = candidates[i].resolution;
        _sharedState.targets[i].distance = candidates[i].distance;
        _sharedState.targets[i].lastSeenMs = now;
        _sharedState.targets[i].valid = true;
        _sharedState.targets[i].isHeld = false;
        activeCount++;
      } else {
        _sharedState.targets[i].valid = false;
        _sharedState.targets[i].isHeld = false;
      }
      continue;
    }

    int8_t candIdx = slotMap[i];
    if (tr.confirmed && candIdx >= 0 && candidates[candIdx].valid) {
      const auto &cand = candidates[candIdx];

      // transmitem coordonatele carteziene stabilizate polare
      _sharedState.targets[i].x = (int)roundf(tr.estX);
      _sharedState.targets[i].y = (int)roundf(tr.estY);

      // vitezele pe axe impartite la 10 (convertim din mm/s in cm/s)
      _sharedState.targets[i].vx = (int)roundf(tr.estVx / 10.0f);
      _sharedState.targets[i].vy = (int)roundf(tr.estVy / 10.0f);
      _sharedState.targets[i].radialSpeed = cand.speed;

      float vTotal = sqrtf(tr.estVx * tr.estVx + tr.estVy * tr.estVy) / 10.0f;
      _sharedState.targets[i].speed = tr.isStationary ? 0 : (int)roundf(vTotal);

      _sharedState.targets[i].distance = (int)roundf(sqrtf(tr.estX * tr.estX + tr.estY * tr.estY));
      _sharedState.targets[i].resolution = cand.resolution;
      _sharedState.targets[i].lastSeenMs = now;
      _sharedState.targets[i].valid = true;
      _sharedState.targets[i].isHeld = false;
      activeCount++;
    } else if (tr.confirmed && _sharedState.targets[i].valid) {
      // HOLD TIMER: daca semnalul a disparut temporar (omul a stat complet nemiscat cateva fractiuni de secunda),
      // nu stergem tinta instant! O mai tinem pe ecran 1.2 secunde ca [HLD] ca sa nu clipeasca suparator
      unsigned long timpUrmarire = tr.lastSeenMs - tr.firstSeenMs;
      unsigned long limitaHold = (timpUrmarire < 600) ? TARGET_HOLD_BRIEF_MS :
                                 ((gMode == GHOST_FILTER_AGGRESSIVE) ? 800 : TARGET_STALE_REMOVE_MS);

      if (now - tr.lastSeenMs < limitaHold) {
        _sharedState.targets[i].speed = 0;
        _sharedState.targets[i].vx = 0;
        _sharedState.targets[i].vy = 0;
        _sharedState.targets[i].isHeld = true;
        activeCount++;
      } else {
        // a trecut timpul de hold -> eliberam slotul
        tr.confirmed = false;
        tr.hits = 0;
        tr.hasMoved = false;
        _sharedState.targets[i].valid = false;
        _sharedState.targets[i].isHeld = false;
      }
    } else {
      _sharedState.targets[i].valid = false;
      _sharedState.targets[i].isHeld = false;
    }
  }

  _sharedState.activeCount = activeCount;
  _sharedState.lastHeartbeatMs = now;
  _sharedState.totalPackets++;
  _sharedState.filterMode = gMode;
  _sharedState.accuracyMode = aMode;
  if (frame != NULL) {
    memcpy(_sharedState.rawFrame, frame, 30);
  }
  // deblocam spinlock-ul: Core 1 poate citi acum copia proaspata
  portEXIT_CRITICAL(&_mux);
}

// luam datele curente sub protectie de mutex
CODSharedState LD2450Radar::getSnapshot() {
  CODSharedState snapshot;
  portENTER_CRITICAL(&_mux);
  memcpy(&snapshot, &_sharedState, sizeof(CODSharedState));
  portEXIT_CRITICAL(&_mux);
  return snapshot;
}

// verificam daca radarul a trimis ceva in ultimele 1.5 secunde
bool LD2450Radar::isConnected() {
  portENTER_CRITICAL(&_mux);
  unsigned long last = _sharedState.lastHeartbeatMs;
  portEXIT_CRITICAL(&_mux);
  return (last > 0 && (millis() - last < SENSOR_DEAD_MS));
}

void LD2450Radar::setGhostFilterMode(GhostFilterMode mode) {
  portENTER_CRITICAL(&_mux);
  _filterMode = mode;
  _sharedState.filterMode = mode;
  for (int i = 0; i < MAX_TARGETS; i++) {
    _slotTrackers[i].hits = 0;
    _slotTrackers[i].confirmed = false;
    _slotTrackers[i].hasMoved = false;
    _slotTrackers[i].isStationary = false;
    _slotTrackers[i].isQuasiStatic = false;
    _slotTrackers[i].limbHopHits = 0;
  }
  portEXIT_CRITICAL(&_mux);
}

GhostFilterMode LD2450Radar::getGhostFilterMode() {
  portENTER_CRITICAL(&_mux);
  GhostFilterMode m = _filterMode;
  portEXIT_CRITICAL(&_mux);
  return m;
}

void LD2450Radar::setAccuracyMode(AccuracyMode mode) {
  portENTER_CRITICAL(&_mux);
  _accuracyMode = mode;
  _sharedState.accuracyMode = mode;
  portEXIT_CRITICAL(&_mux);
}

AccuracyMode LD2450Radar::getAccuracyMode() {
  portENTER_CRITICAL(&_mux);
  AccuracyMode m = _accuracyMode;
  portEXIT_CRITICAL(&_mux);
  return m;
}

// trimitere comanda seriala la senzor
bool LD2450Radar::sendCommand(uint16_t cmd, const uint8_t* val, uint16_t valLen,
                              uint8_t* respOut, uint8_t respMax,
                              uint8_t* actualLen, uint32_t timeoutMs) {
  if (_serial == NULL) return false;

  // oprim temporar task-ul de citire ca sa nu ne fure raspunsul la comanda
  bool taskSuspended = false;
  if (_taskHandle != NULL) {
    vTaskSuspend(_taskHandle);
    taskSuspended = true;
  }

  while (_serial->available()) _serial->read();

  uint8_t header[4] = {0xFD, 0xFC, 0xFB, 0xFA};
  uint8_t footer[4] = {0x04, 0x03, 0x02, 0x01};
  uint16_t length = 2 + valLen;

  _serial->write(header, 4);
  _serial->write((uint8_t)(length & 0xFF));
  _serial->write((uint8_t)((length >> 8) & 0xFF));
  _serial->write((uint8_t)(cmd & 0xFF));
  _serial->write((uint8_t)((cmd >> 8) & 0xFF));
  if (val != NULL && valLen > 0) {
    _serial->write(val, valLen);
  }
  _serial->write(footer, 4);
  _serial->flush();

  uint32_t start = millis();
  uint8_t buf[64];
  uint8_t idx = 0;
  bool success = false;

  while (millis() - start < timeoutMs) {
    if (_serial->available()) {
      buf[idx++] = (uint8_t)_serial->read();
      if (idx >= 10) {
        if (buf[0] == 0xFD && buf[1] == 0xFC && buf[2] == 0xFB && buf[3] == 0xFA) {
          uint16_t pLen = buf[4] | (buf[5] << 8);
          uint8_t totalExpected = 4 + 2 + pLen + 4;
          if (idx >= totalExpected && totalExpected <= sizeof(buf)) {
            if (respOut != NULL && respMax > 0) {
              uint8_t toCopy = (idx < respMax) ? idx : respMax;
              memcpy(respOut, buf, toCopy);
              if (actualLen != NULL) *actualLen = toCopy;
            }
            success = (pLen >= 4 && buf[8] == 0x00 && buf[9] == 0x00);
            break;
          }
        }
      }
      if (idx >= sizeof(buf) - 1) idx = 0;
    } else {
      delay(2);
    }
  }

  if (taskSuspended && _taskHandle != NULL) {
    vTaskResume(_taskHandle);
  }

  return success;
}

bool LD2450Radar::enterConfig() {
  uint8_t val[2] = {0x01, 0x00};
  return sendCommand(0x00FF, val, 2, NULL, 0, NULL, 300);
}

bool LD2450Radar::exitConfig() {
  return sendCommand(0x00FE, NULL, 0, NULL, 0, NULL, 300);
}

bool LD2450Radar::setMultiTargetMode(bool enable) {
  if (!enterConfig()) {
    delay(50);
    enterConfig();
  }
  delay(50);
  uint16_t cmd = enable ? 0x0090 : 0x0080;
  bool ok = sendCommand(cmd, NULL, 0, NULL, 0, NULL, 300);
  delay(50);
  exitConfig();
  delay(50);

  portENTER_CRITICAL(&_mux);
  _sharedState.multiTargetActive = enable;
  portEXIT_CRITICAL(&_mux);

  return ok;
}

bool LD2450Radar::factoryReset() {
  enterConfig();
  delay(60);
  bool ok = sendCommand(0x00A2, NULL, 0, NULL, 0, NULL, 500);
  delay(100);
  exitConfig();
  delay(50);

  setMultiTargetMode(true);
  delay(50);
  reboot();
  return ok;
}

bool LD2450Radar::reboot() {
  enterConfig();
  delay(50);
  bool ok = sendCommand(0x00A3, NULL, 0, NULL, 0, NULL, 300);
  delay(100);
  return ok;
}

String LD2450Radar::queryFirmwareVersion() {
  enterConfig();
  delay(50);
  uint8_t resp[32];
  uint8_t len = 0;
  bool ok = sendCommand(0x00A0, NULL, 0, resp, sizeof(resp), &len, 300);
  delay(50);
  exitConfig();
  delay(50);

  if (ok && len >= 18) {
    char buf[32];
    sprintf(buf, "V%d.%02X.%02X%02X%02X%02X",
            resp[13], resp[12],
            resp[17], resp[16], resp[15], resp[14]);
    return String(buf);
  }
  return "";
}

`


## include/display.h
`cpp
#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "config.h"
#include "radar_types.h"

// afisare grafica pe ecranul st7789 (ruleaza pe Core 1 in loop)
// desenam totul intr-un canvas de 150KB in RAM si il trimitem dintr-o bucata prin SPI la 40MHz
// ca sa nu palpaie deloc ecranul (double buffering)

class DisplayManager {
public:
  DisplayManager();
  ~DisplayManager();

  // porneste ecranul, aloca bufferul in ram si calculeaza cercurile
  bool begin();

  // deseneaza cadrul complet pe ecran
  void render(const CODSharedState &state, bool isConnected);

  // rotire ecran (daca e montat invers)
  void setOrientation(uint8_t rot);
  uint8_t getOrientation() const;

  // mod zoom apropiat (3m) vs normal (6m)
  void setRangeMode(bool cqb);
  void toggleRangeMode();
  bool isCqbMode() const;
  float getMaxRange() const;

private:
  Adafruit_ST7789  _tft;
  GFXcanvas16*     _canvas; // buffer de 150KB in SRAM

  float _maxRange;
  float _canvasScale;
  bool  _cqbMode;

  // dârele lasate de tinte in miscare
  TargetTrail    _trails[MAX_TARGETS];

  // tabele precalculate (LUT) pentru cercuri si linii ca sa mearga rapid fara sin/cos in loop
  struct RingPoint {
    int x;
    int y;
  };
  RingPoint _ringPoints[400];
  int       _numRingPoints;
  int       _spokeEndX[5];
  int       _spokeEndY[5];
  int       _sweepEndX[61];
  int       _sweepEndY[61];
  int       _sweepIndex;
  bool      _sweepDir;

  void recomputeGridGeometry();

  // elemente de desenat pe ecran
  void drawCanvasGrid(bool isConnected, GhostFilterMode filterMode, AccuracyMode accMode);
  void drawDistanceRings();
  void drawAzimuthSpokes();
  void drawOriginMarker();
  void drawSweepLine();
  void drawCornerHud(bool isConnected, GhostFilterMode filterMode, AccuracyMode accMode);
  void drawBottomHud(const CODSharedState &state);
  void drawTargetTelemetryLine(int x, int y, int slotIdx, const CODTarget &t);

  void drawStaticLayout();

  // conversie din milimetri in pixeli pe ecran
  bool worldToCanvas(float worldX_mm, float worldY_mm, int &canvasX, int &canvasY);

  // grafica pentru tinte
  void drawVelocityArrow(int startX, int startY, int endX, int endY, uint16_t color);
  void drawTargetPing(int x, int y, int id, int dist_mm, unsigned long ageMs, uint16_t targetColor, bool isHeld);
  void updateTrails(int slot, int scX, int scY, unsigned long now);
  void drawTrails(int slot, uint16_t targetColor);

  static uint16_t getTargetColor(int slotIdx);
  static uint16_t trailColorForLife(uint16_t baseColor, float lifeRatio);
};

`


## src/display.cpp
`cpp
#include "display.h"
#include <math.h>

// culorile pentru fiecare tinta in parte (t1 cyan, t2 galben, t3 mov)
static const uint16_t TARGET_COLORS[MAX_TARGETS] = {
  COLOR_TARGET1, // T1
  COLOR_TARGET2, // T2
  COLOR_TARGET3  // T3
};

// pozitii pe ecran pentru text si marginile de jos
#define HUD_MARGIN_X    8   // margine stanga/dreapta
#define HUD_TOP_Y       6   // linia de sus cu numele si modurile
#define HUD_MODE_TAG_X  56  // pozitia unde scriem modurile
#define HUD_BOT_LINE1_Y 218 // linia 1 din colturile de jos
#define HUD_BOT_LINE2_Y 228 // linia 2 din colturile de jos
#define HUD_RIGHT_COL_X 244 // coloana din dreapta jos
#define CHAR_WIDTH_PX   6   // latimea unei litere pe ecran

// conversie din grade in radiani (avem nevoie de ea pentru functiile sinf si cosf)
#define DEG_TO_RAD_F    (3.14159265f / 180.0f)

// constructorul clasei: initializam obiectul ST7789 si variabilele de stare
DisplayManager::DisplayManager()
  : _tft(Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST)),
    _canvas(NULL),
    _maxRange(RADAR_DEFAULT_RANGE),
    // calculam scala astfel incat raza maxima (6m sau 3m) sa se incadreze exact pe inaltimea ecranului
    _canvasScale((float)(CANVAS_ORIGIN_Y - 4) / RADAR_DEFAULT_RANGE),
    _cqbMode(false),
    _numRingPoints(0),
    _sweepIndex(0),
    _sweepDir(true) {
  // golim cozile istorice de puncte pentru fiecare din cele 3 sloturi de tinte
  for (int i = 0; i < MAX_TARGETS; i++) {
    _trails[i] = {{}, 0, 0, 0};
  }
  // precalculam de la bun inceput pozitia pixelilor pentru arce si linii
  recomputeGridGeometry();
}

// destructor: daca am alocat memorie dinamica pentru canvas, o eliberam corect la inchidere
DisplayManager::~DisplayManager() {
  if (_canvas != NULL) {
    delete _canvas;
    _canvas = NULL;
  }
}

// initializam ecranul fizic ST7789 prin magistrala SPI
bool DisplayManager::begin() {
  // daca pinul de backlight este controlat software, il setam HIGH ca sa aprindem ledurile ecranului
  if (TFT_BLK >= 0) {
    pinMode(TFT_BLK, OUTPUT);
    digitalWrite(TFT_BLK, HIGH);
  }

  // pornim magistrala hardware SPI: Clock pe GPIO 12, Data pe GPIO 11, Chip Select pe GPIO 10
  // MISO nu este folosit (-1) fiindca doar trimitem date spre ecran, nu citim nimic inapoi
  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  // pornim controlerul ecranului ST7789 la rezolutia nativa 240x320
  _tft.init(240, 320);
  
  // setam frecventa SPI la 40 MHz. e viteza maxima stabila pentru acest cip pe cablaje scurte.
  // la 40 MHz putem varsa tot ecranul de 150 KB in doar ~30ms, atingand lejer 35 de cadre pe secunda
  _tft.setSPISpeed(40000000);
  _tft.setRotation(SCREEN_ROTATION); // rotim imaginea conform pozitiei din carcasa (180 grade)
  _tft.fillScreen(COLOR_BG);

  // alocam bufferul de double-buffering in memoria SRAM a procesorului ESP32-S3.
  // 320 latime x 240 inaltime x 2 octeti/pixel (format RGB565) = 153.600 octeti (~150 KB).
  // desenam totul mai intai aici in memorie, ca sa nu existe nicio palpaire pe ecran.
  _canvas = new GFXcanvas16(CANVAS_WIDTH, CANVAS_HEIGHT);
  if (_canvas == NULL || _canvas->getBuffer() == NULL) {
    Serial.println("Eroare critica: Nu avem destul RAM liber pentru canvas!");
    return false;
  }

  Serial.println("Ecran ST7789 initializat cu succes (Double-Buffering activ pe 150KB RAM)!");
  drawStaticLayout();
  return true;
}

// comutam distanta maxima afisata pe ecran (3 metri pentru zoom apropiat sau 6 metri normal)
void DisplayManager::setRangeMode(bool cqb) {
  _cqbMode = cqb;
  _maxRange = cqb ? RADAR_RANGE_CQB : RADAR_RANGE_LONG;
  // recalculam raportul pixeli / milimetru ca sa se scaleze corect pe ecran
  _canvasScale = (float)(CANVAS_ORIGIN_Y - 4) / _maxRange;

  // cand schimbam scala stergem cozile vechi ca sa nu sara punctele aiurea pe ecran
  for (int i = 0; i < MAX_TARGETS; i++) {
    _trails[i].count = 0;
  }
  // recalculam pozitiile cercurilor pentru noua scala aleasa
  recomputeGridGeometry();
}

void DisplayManager::toggleRangeMode() {
  setRangeMode(!_cqbMode);
}

bool DisplayManager::isCqbMode() const {
  return _cqbMode;
}

float DisplayManager::getMaxRange() const {
  return _maxRange;
}

// permite rotirea ecranului cu 180 de grade din comanda seriala daca e tinut invers
void DisplayManager::setOrientation(uint8_t rot) {
  _tft.setRotation(rot);
  for (int i = 0; i < MAX_TARGETS; i++) {
    _trails[i].count = 0;
  }
  drawStaticLayout();
}

uint8_t DisplayManager::getOrientation() const {
  return _tft.getRotation();
}

// optimizare de performanta: precalculam punctele geometrice ale cercurilor si liniilor radiale.
// daca am apela functiile sin() si cos() pentru cateva sute de pixeli la fiecare cadru (de 35 de ori pe secunda),
// am consuma inutil cicluri de procesor pe nucleul 1. Asa ca le calculam o singura data si le salvam in vector.
void DisplayManager::recomputeGridGeometry() {
  _numRingPoints = 0;
  int numRanges = _cqbMode ? 3 : 6;
  int rangeMeters[6] = {1, 2, 3, 4, 5, 6};

  // generam arcele de cerc din 2 in 2 grade, in intervalul util de vizualizare (intre 30 si 150 grade)
  for (int i = 0; i < numRanges; i++) {
    int radius = (int)roundf(rangeMeters[i] * 1000.0f * _canvasScale);

    for (int deg = 30; deg <= 150; deg += 2) {
      float rad = (float)deg * DEG_TO_RAD_F;
      // formula cercului: x = r * cos(theta), y = r * sin(theta)
      // scadem Y pentru ca originea ecranului (0,0) este in stanga-sus, iar axa Y merge in jos
      int px = CANVAS_ORIGIN_X + (int)roundf((float)radius * cosf(rad));
      int py = CANVAS_ORIGIN_Y - (int)roundf((float)radius * sinf(rad));

      // lasam zona libera in partea de sus ca sa nu desenam arcul peste bara de titlu (HUD)
      if ((px < 135 || px > 260) && py < 18) continue;

      // adaugam pixelul in tabloul precalculat daca incape pe ecran
      if (px >= 0 && px < CANVAS_WIDTH && py >= 0 && py <= CANVAS_ORIGIN_Y) {
        if (_numRingPoints < (int)(sizeof(_ringPoints) / sizeof(_ringPoints[0]))) {
          _ringPoints[_numRingPoints++] = {px, py};
        }
      }
    }
  }

  // precalculam capetele liniilor de unghi (la 30, 60, 90, 120 si 150 de grade)
  float spokeAngles[] = {30.0f, 60.0f, 90.0f, 120.0f, 150.0f};
  int maxR = (int)roundf(_maxRange * _canvasScale);
  for (int k = 0; k < 5; k++) {
    float rad = spokeAngles[k] * DEG_TO_RAD_F;
    _spokeEndX[k] = CANVAS_ORIGIN_X + (int)roundf((float)maxR * cosf(rad));
    _spokeEndY[k] = CANVAS_ORIGIN_Y - (int)roundf((float)maxR * sinf(rad));
  }

  // precalculam cele 60 de pozitii succesive ale liniei verzi de baleiaj (sweep line)
  for (int idx = 0; idx <= 60; idx++) {
    float deg = 30.0f + (float)idx * 2.0f;
    float rad = deg * DEG_TO_RAD_F;
    _sweepEndX[idx] = CANVAS_ORIGIN_X + (int)roundf((float)maxR * cosf(rad));
    _sweepEndY[idx] = CANVAS_ORIGIN_Y - (int)roundf((float)maxR * sinf(rad));
  }
}

// transforma coordonatele reale in milimetri (din lumea fizica) in coordonate pe ecran (pixeli)
bool DisplayManager::worldToCanvas(float worldX_mm, float worldY_mm, int &canvasX, int &canvasY) {
  // distanta in fata senzorului este intotdeauna pozitiva
  float forwardY = (worldY_mm < 0.0f) ? -worldY_mm : worldY_mm;

  // originea senzorului este plasata jos, in centrul ecranului la coordonatele (160, 235).
  // pe orizontala (X): adunam deplasarea in mm inmultita cu factorul de scala.
  // pe verticala (Y): scadem deplasarea, deoarece in grafica pe calculator Y=0 e sus si creste in jos.
  canvasX = CANVAS_ORIGIN_X + (int)roundf(worldX_mm * _canvasScale);
  canvasY = CANVAS_ORIGIN_Y - (int)roundf(forwardY * _canvasScale);

  // verificam daca punctul se afla in interiorul limitelor fizice ale ecranului (320x240)
  return (canvasX >= 0 && canvasX < CANVAS_WIDTH &&
          canvasY >= 0 && canvasY <= CANVAS_ORIGIN_Y + 2);
}

// returneaza culoarea distincta alocata fiecarui slot (T1 = Cyan, T2 = Galben, T3 = Mov)
uint16_t DisplayManager::getTargetColor(int slotIdx) {
  if (slotIdx >= 0 && slotIdx < MAX_TARGETS) {
    return TARGET_COLORS[slotIdx];
  }
  return COLOR_TARGET1;
}

// calculeaza estomparea culorii pentru coada tintei (trail effect).
// ecranul foloseste formatul RGB565 pe 16 biti:
// - primii 5 biti: Rosu (masca 0xF800, adica >> 11 si & 0x1F)
// - urmatorii 6 biti: Verde (masca 0x07E0, adica >> 5 si & 0x3F)
// - ultimii 5 biti: Albastru (masca 0x001F, adica & 0x1F)
// extragem fiecare canal separat, il inmultim cu raportul de viata (intre 0.0 si 1.0) si le impachetam la loc.
uint16_t DisplayManager::trailColorForLife(uint16_t baseColor, float lifeRatio) {
  uint32_t scale = (uint32_t)(lifeRatio * 256.0f);
  uint32_t r = (((baseColor >> 11) & 0x1F) * scale) >> 8;
  uint32_t g = (((baseColor >> 5) & 0x3F) * scale) >> 8;
  uint32_t b = ((baseColor & 0x1F) * scale) >> 8;

  return (uint16_t)((r << 11) | (g << 5) | b);
}

// deseneaza cercurile concentrice de distanta (la 1m, 2m, etc.)
void DisplayManager::drawDistanceRings() {
  if (_canvas == NULL) return;

  uint16_t* buf = _canvas->getBuffer();
  // aprindem pixelii precalculati direct in memoria RAM, la viteza maxima (fara apeluri lente)
  for (int i = 0; i < _numRingPoints; i++) {
    buf[_ringPoints[i].y * CANVAS_WIDTH + _ringPoints[i].x] = COLOR_RADAR_ARC;
  }

  int numRanges = _cqbMode ? 3 : 6;
  int rangeMeters[6] = {1, 2, 3, 4, 5, 6};

  // afisam etichetele text ("1m", "2m", etc.) langa fiecare cerc
  _canvas->setTextColor(COLOR_TEXT_DIM);
  _canvas->setTextSize(1);
  for (int i = 0; i < numRanges; i++) {
    int radius = (int)roundf(rangeMeters[i] * 1000.0f * _canvasScale);
    int labelY = CANVAS_ORIGIN_Y - radius;
    if (labelY >= 2 && labelY < CANVAS_ORIGIN_Y) {
      _canvas->setCursor(CANVAS_ORIGIN_X + 4, labelY - 3);
      _canvas->printf("%dm", rangeMeters[i]);
    }
  }
}

// deseneaza liniile radiale care definesc unghiurile si deschiderea conului util (FOV)
void DisplayManager::drawAzimuthSpokes() {
  if (_canvas == NULL) return;
  for (int k = 0; k < 5; k++) {
    // marginile conului (la 30 si 150 de grade) sunt accentuate cu o culoare mai vizibila
    uint16_t spokeColor = (k == 0 || k == 4) ? COLOR_FOV_EDGE : COLOR_RADAR_CONE;
    _canvas->drawLine(CANVAS_ORIGIN_X, CANVAS_ORIGIN_Y, _spokeEndX[k], _spokeEndY[k], spokeColor);
  }
}

// deseneaza punctul de origine al senzorului: un mic triunghi rosu indreptat in sus
void DisplayManager::drawOriginMarker() {
  if (_canvas == NULL) return;

  _canvas->fillTriangle(
    CANVAS_ORIGIN_X,     CANVAS_ORIGIN_Y - 7,
    CANVAS_ORIGIN_X - 6, CANVAS_ORIGIN_Y + 2,
    CANVAS_ORIGIN_X + 6, CANVAS_ORIGIN_Y + 2,
    COLOR_ORIGIN
  );
  _canvas->drawFastHLine(CANVAS_ORIGIN_X - 10, CANVAS_ORIGIN_Y + 3, 21, COLOR_BORDER);
}

// linia verde animata care se misca stanga-dreapta continuu, imitand baleiajul mecanic al unui radar clasic
void DisplayManager::drawSweepLine() {
  if (_canvas == NULL) return;
  _canvas->drawLine(CANVAS_ORIGIN_X, CANVAS_ORIGIN_Y, _sweepEndX[_sweepIndex], _sweepEndY[_sweepIndex], COLOR_SWEEP);

  // alternam directia de miscare cand ajungem la marginile conului
  if (_sweepDir) {
    if (++_sweepIndex >= 60) _sweepDir = false;
  } else {
    if (--_sweepIndex <= 0) _sweepDir = true;
  }
}

void DisplayManager::drawStaticLayout() {
  _tft.fillScreen(COLOR_BG);
}

// functia care curata ecranul si redeseneaza toate elementele fixe (grila, originea, baleiajul si bara de sus)
void DisplayManager::drawCanvasGrid(bool isConnected, GhostFilterMode filterMode, AccuracyMode accMode) {
  if (_canvas == NULL) return;
  _canvas->fillScreen(COLOR_BG); // umplem fundalul cu negru
  drawDistanceRings();
  drawAzimuthSpokes();
  drawOriginMarker();
  drawSweepLine();
  drawCornerHud(isConnected, filterMode, accMode);
}

// bara de stare din partea de sus a ecranului (HUD): titlul Lynceus, modurile active si statusul conexiunii
void DisplayManager::drawCornerHud(bool isConnected, GhostFilterMode filterMode, AccuracyMode accMode) {
  if (_canvas == NULL) return;

  _canvas->setTextSize(1);

  // titlul gadgetului
  _canvas->setTextColor(COLOR_HEADER_TXT);
  _canvas->setCursor(HUD_MARGIN_X, HUD_TOP_Y);
  _canvas->print("Lynceus");

  // etichetele modurilor curente de operare: [6M sau 3M | Mod Filtru | Mod Acuratete]
  _canvas->setTextColor(COLOR_TEXT_DIM);
  _canvas->setCursor(HUD_MODE_TAG_X, HUD_TOP_Y);
  const char* fltTag = (filterMode == GHOST_FILTER_AGGRESSIVE) ? "AGR" :
                       (filterMode == GHOST_FILTER_OFF) ? "RAW" : "FLT";
  const char* accTag = (accMode == ACCURACY_PRECISION)  ? "PRE" :
                       (accMode == ACCURACY_RESPONSIVE) ? "RSP" : "TAC";
  _canvas->printf("[%s|%s|%s]", _cqbMode ? "3M" : "6M", fltTag, accTag);

  // indicatorul de stare al senzorului: verde daca primim date, rosu daca senzorul e deconectat
  if (isConnected) {
    _canvas->setTextColor(COLOR_STATUS_OK);
    _canvas->setCursor(CANVAS_WIDTH - HUD_MARGIN_X - (6 * CHAR_WIDTH_PX), HUD_TOP_Y);
    _canvas->print("ONLINE");
  } else {
    _canvas->setTextColor(COLOR_STATUS_WARN);
    _canvas->setCursor(CANVAS_WIDTH - HUD_MARGIN_X - (7 * CHAR_WIDTH_PX), HUD_TOP_Y);
    _canvas->print("OFFLINE");
  }
}

// afiseaza informatiile specifice pentru o singura tinta in panoul de telemetrie din colturile de jos
void DisplayManager::drawTargetTelemetryLine(int x, int y, int slotIdx, const CODTarget &t) {
  uint16_t col = getTargetColor(slotIdx);
  _canvas->setCursor(x, y);

  if (t.valid) {
    // afisam identificatorul (T1, T2, T3) in culoarea specifica
    _canvas->setTextColor(col);
    _canvas->printf("T%d:", t.id);

    // afisam distanta masurata formatata frumos in metri cu 2 zecimale (de exemplu: 2.14m)
    _canvas->setTextColor(COLOR_HEADER_TXT);
    _canvas->printf(" %d.%02dm ", t.distance / 1000, (t.distance % 1000) / 10);

    // daca omul s-a oprit pe loc, afisam eticheta [HLD] (Hold). Daca se misca, afisam viteza in cm/s cu semn (+ sau -)
    if (t.isHeld) {
      _canvas->setTextColor(COLOR_TEXT_DIM);
      _canvas->print("[HLD]");
    } else {
      _canvas->setTextColor(col);
      _canvas->printf("%+d", t.speed);
    }
  } else {
    // daca slotul nu a detectat nicio persoana, afisam linie punctata
    _canvas->setTextColor(COLOR_TEXT_DIM);
    _canvas->printf("T%d: --.-m", slotIdx + 1);
  }
}

// panoul de telemetrie tactica plasat in partea de jos a ecranului
void DisplayManager::drawBottomHud(const CODSharedState &state) {
  if (_canvas == NULL) return;

  _canvas->setTextSize(1);

  // in stanga jos: datele pentru tintele T1 si T2
  drawTargetTelemetryLine(HUD_MARGIN_X, HUD_BOT_LINE1_Y, 0, state.targets[0]);
  drawTargetTelemetryLine(HUD_MARGIN_X, HUD_BOT_LINE2_Y, 1, state.targets[1]);

  // in dreapta jos: datele pentru tinta T3 si numarul total de persoane detectate in incapere
  drawTargetTelemetryLine(HUD_RIGHT_COL_X, HUD_BOT_LINE1_Y, 2, state.targets[2]);

  _canvas->setCursor(HUD_RIGHT_COL_X, HUD_BOT_LINE2_Y);
  if (state.activeCount > 0) {
    _canvas->setTextColor(COLOR_HEADER_TXT);
    _canvas->printf("Tinte: %d", state.activeCount);
  } else {
    _canvas->setTextColor(COLOR_TEXT_DIM);
    _canvas->print("Tinte: 0");
  }
}

// deseneaza o sageata orientata 360 de grade care arata directia exacta si viteza de mers a tintei
void DisplayManager::drawVelocityArrow(int startX, int startY, int endX, int endY, uint16_t color) {
  if (_canvas == NULL) return;
  // linia principala a sagetii (tija)
  _canvas->drawLine(startX, startY, endX, endY, color);

  // calculam unghiul directiei folosind functia arc-tangenta (atan2f) pe diferenta de coordonate
  float unghi = atan2f((float)(endY - startY), (float)(endX - startX));
  float wingAngle = 0.5f; // deschiderea laterala a aripioarelor sagetii (~30 de grade)
  float wingLen = 5.0f;   // lungimea aripioarelor in pixeli

  // calculam cele doua varfuri laterale prin rotatie trigonometrica
  int wing1X = endX - (int)roundf(wingLen * cosf(unghi - wingAngle));
  int wing1Y = endY - (int)roundf(wingLen * sinf(unghi - wingAngle));
  int wing2X = endX - (int)roundf(wingLen * cosf(unghi + wingAngle));
  int wing2Y = endY - (int)roundf(wingLen * sinf(unghi + wingAngle));

  // desenam cele doua laturi ale varfului
  _canvas->drawLine(endX, endY, wing1X, wing1Y, color);
  _canvas->drawLine(endX, endY, wing2X, wing2Y, color);
}

// deseneaza simbolul grafic al tintei (blip-ul radar): cerc exterior, cerc interior si eticheta text
void DisplayManager::drawTargetPing(int x, int y, int id, int dist_mm, unsigned long ageMs, uint16_t targetColor, bool isHeld) {
  if (_canvas == NULL) return;

  // cercul exterior cu raza de 6 pixeli
  _canvas->drawCircle(x, y, 6, targetColor);

  // cercul din mijloc: daca omul se misca il desenam plin, daca sta pe loc il lasam contur gol
  if (isHeld) {
    _canvas->drawCircle(x, y, 3, targetColor);
  } else {
    _canvas->fillCircle(x, y, 3, targetColor);
  }

  // un punct alb stralucitor in centrul exact al tintei (nucleul "fierbinte")
  _canvas->drawPixel(x, y, COLOR_TGT_HOT);

  // calculam pozitia textului langa punct, avand grija sa nu iasa din marginile ecranului
  int textX = (x > (CANVAS_WIDTH - 50)) ? (x - 44) : (x + 8);
  int textY1 = (y < 20) ? (y + 4) : (y - 8);
  int textY2 = (y < 20) ? (y + 13) : (y + 1);

  // afisam ID-ul tintei (T1, T2 sau T3)
  _canvas->setTextSize(1);
  _canvas->setTextColor(targetColor);
  _canvas->setCursor(textX, textY1);
  _canvas->printf("T%d", id);

  // afisam distanta directa calculata
  _canvas->setTextColor(COLOR_HEADER_TXT);
  _canvas->setCursor(textX, textY2);
  _canvas->printf("%d.%dm", dist_mm / 1000, (dist_mm % 1000) / 100);
}

// adauga pozitia curenta a tintei in bufferul circular pentru a genera efectul de coada (trail)
void DisplayManager::updateTrails(int slot, int scX, int scY, unsigned long now) {
  TargetTrail &tr = _trails[slot];
  // preluam o noua mostra la fiecare 60ms ca sa avem puncte distantate vizibil in mers
  if (now - tr.lastSampleMs >= TRAIL_SAMPLE_MS) {
    tr.lastSampleMs = now;
    tr.points[tr.head] = {scX, scY, now};
    tr.head = (tr.head + 1) % TRAIL_LEN;
    if (tr.count < TRAIL_LEN) tr.count++;
  }
}

// deseneaza urma lasata in spate de tinta, estompand punctele mai vechi pana devin invizibile
void DisplayManager::drawTrails(int slot, uint16_t targetColor) {
  if (_canvas == NULL) return;
  TargetTrail &tr = _trails[slot];
  unsigned long now = millis();

  for (int i = 0; i < tr.count; i++) {
    // parcurgem punctele in ordine inversa, de la cel mai recent la cel mai vechi
    int idx = (tr.head - 1 - i + TRAIL_LEN) % TRAIL_LEN;
    unsigned long age = now - tr.points[idx].timestamp;
    // pastram punctele timp de maxim 1.5 secunde
    if (age < 1500) {
      // calculam raportul de estompare: la 0ms viata e 1.0 (plin), la 1500ms viata e 0.0 (transparent)
      float viata = 1.0f - ((float)age / 1500.0f);
      uint16_t c = trailColorForLife(targetColor, viata);
      _canvas->drawPixel(tr.points[idx].screenX, tr.points[idx].screenY, c);
    }
  }
}

// desenam un cadru complet pe ecran
void DisplayManager::render(const CODSharedState &state, bool isConnected) {
  if (_canvas == NULL) return;

  // fundal, cercuri, con si bara de sus
  drawCanvasGrid(isConnected, state.filterMode, state.accuracyMode);

  // telemetria din colturile de jos
  drawBottomHud(state);

  unsigned long now = millis();

  // desenam tintele active
  for (int i = 0; i < MAX_TARGETS; i++) {
    uint16_t col = getTargetColor(i);

    if (state.targets[i].valid) {
      const auto &t = state.targets[i];

      // transformam din mm in pixeli
      int canvasX = 0, canvasY = 0;
      if (worldToCanvas((float)t.x, (float)t.y, canvasX, canvasY)) {
        // dâra lasata in spate
        updateTrails(i, canvasX, canvasY, now);
        drawTrails(i, col);

        // sageata de directie daca omul se deplaseaza cu peste 5 cm/s
        if (!t.isHeld && t.speed >= ARROW_MIN_SPEED_CMS) {
          float vNorm = sqrtf((float)(t.vx * t.vx + t.vy * t.vy));
          if (vNorm > 0.5f) {
            int vitezaClamped = constrain(t.speed, ARROW_MIN_SPEED_CMS, ARROW_MAX_SPEED_CMS);
            float lungime = ARROW_LEN_MIN + ((float)(vitezaClamped - ARROW_MIN_SPEED_CMS) /
                                            (ARROW_MAX_SPEED_CMS - ARROW_MIN_SPEED_CMS)) *
                                            (ARROW_LEN_MAX - ARROW_LEN_MIN);

            float dirX = (float)t.vx / vNorm;
            float dirY = (float)t.vy / vNorm;

            int arrowEndX = canvasX + (int)roundf(dirX * lungime);
            int arrowEndY = canvasY - (int)roundf(dirY * lungime);
            drawVelocityArrow(canvasX, canvasY, arrowEndX, arrowEndY, col);
          }
        }

        // punctul luminos si distanta scrisa langa el
        unsigned long age = now - t.lastSeenMs;
        drawTargetPing(canvasX, canvasY, t.id, t.distance, age, col, t.isHeld);
      }
    } else {
      _trails[i].count = 0;
    }
  }

  // trimitem tot canvasul din ram pe ecran prin spi dintr-o bucata (fara flicker)
  _tft.drawRGBBitmap(0, CANVAS_Y_OFFSET, _canvas->getBuffer(), CANVAS_WIDTH, CANVAS_HEIGHT);
}

`


## include/proximity_led.h
`cpp
#pragma once
#include <Arduino.h>
#include "config.h"
#include "radar_types.h"

// led rgb ws2812 de pe placa (gpio 48 pe esp32-s3)
// schimba culoarea in functie de cel mai apropiat om:
// rosu sub 1m, portocaliu 1-2m, verde 2-3.5m, albastru peste 5m
// daca senzorul e oprit, pulseaza usor pe rosu

class ProximityLED {
public:
  ProximityLED();

  // test scurt la pornire (rosu -> verde -> albastru)
  void begin();

  // actualizeaza culoarea in functie de tinte (o data la 50ms)
  void update(const CODSharedState &state, bool isConnected);

  // seteaza culoarea rgb (cu luminozitate redusa la 25%)
  void setRgb(int r, int g, int b);

  int getR() const { return _currentR; }
  int getG() const { return _currentG; }
  int getB() const { return _currentB; }
  int getLastMinDist() const { return _lastMinDist; }

private:
  int _currentR;
  int _currentG;
  int _currentB;
  int _lastMinDist;
  unsigned long _lastUpdateMs;

  void computeProximityColor(int distMm, int &r, int &g, int &b);
};

`


## src/proximity_led.cpp
`cpp
#include "proximity_led.h"
#include <math.h>

// control pentru ledul rgb ws2812 de pe esp32-s3
// ne arata distanta pana la cel mai apropiat om prin culori (rosu -> verde -> albastru)
// iar daca radarul e oprit pulseaza usor pe rosu

ProximityLED::ProximityLED() {
  _currentR = 0;
  _currentG = 0;
  _currentB = 0;
  _lastMinDist = 9999;
  _lastUpdateMs = 0;
}

// test la pornire: aprindem scurt rosu, verde, albastru sa stim ca merge
void ProximityLED::begin() {
  Serial.println("Testare indicator LED RGB WS2812...");

  setRgb(255, 0, 0); // rosu
  delay(250);
  setRgb(0, 255, 0); // verde
  delay(250);
  setRgb(0, 0, 255); // albastru
  delay(250);
  setRgb(0, 0, 0);   // oprit
  delay(100);

  Serial.println("LED RGB testat si functional!");
}

// trimite comanda catre ledul ws2812 prin functia nativa din esp-idf
void ProximityLED::setRgb(int r, int g, int b) {
  _currentR = r;
  _currentG = g;
  _currentB = b;

  // reducem luminozitatea la 25% (NEOPIXEL_BRIGHTNESS = 64) ca sa nu ne orbeasca
  // si sa nu consume bateria degeaba
  uint8_t scaledR = (uint8_t)((r * NEOPIXEL_BRIGHTNESS) / 255);
  uint8_t scaledG = (uint8_t)((g * NEOPIXEL_BRIGHTNESS) / 255);
  uint8_t scaledB = (uint8_t)((b * NEOPIXEL_BRIGHTNESS) / 255);

  neopixelWrite(RGB_BUILTIN, scaledR, scaledG, scaledB);
}

// transforma distanta in mm intr-o tranzitie lina de culori
void ProximityLED::computeProximityColor(int distMm, int &r, int &g, int &b) {
  if (distMm < 1000) {
    // sub 1m: rosu (foarte aproape)
    r = 255;
    g = 0;
    b = 0;
  } else if (distMm < 2000) {
    // 1m - 2m: rosu spre portocaliu
    float t = (float)(distMm - 1000) / 1000.0f;
    r = 255;
    g = (int)(120.0f * t);
    b = 0;
  } else if (distMm < 3500) {
    // 2m - 3.5m: portocaliu spre verde
    float t = (float)(distMm - 2000) / 1500.0f;
    r = (int)(255.0f * (1.0f - t));
    g = (int)(120.0f + 135.0f * t);
    b = 0;
  } else if (distMm < 5000) {
    // 3.5m - 5m: verde spre cyan
    float t = (float)(distMm - 3500) / 1500.0f;
    r = 0;
    g = 255;
    b = (int)(255.0f * t);
  } else {
    // peste 5m: cyan spre albastru rece
    float t = constrain((float)(distMm - 5000) / 1000.0f, 0.0f, 1.0f);
    r = 0;
    g = (int)(255.0f * (1.0f - t));
    b = 255;
  }
}

// actualizeaza culoarea ledului (limitat la 20hz)
void ProximityLED::update(const CODSharedState &state, bool isConnected) {
  unsigned long now = millis();
  if (now - _lastUpdateMs < 50) return;
  _lastUpdateMs = now;

  // daca radarul nu raspunde, facem un efect lin de respiratie pe rosu
  if (!isConnected) {
    _lastMinDist = 9999;
    float puls = (sinf((float)now * 0.005f) + 1.0f) * 0.5f;
    int r = (int)(180.0f * puls + 30.0f);
    setRgb(r, 0, 0);
    return;
  }

  // daca e conectat, cautam cel mai apropiat om dintre cele 3 tinte
  int minDist = 9999;
  bool gasit = false;

  for (int i = 0; i < MAX_TARGETS; i++) {
    if (state.targets[i].valid && state.targets[i].distance > 0) {
      if (state.targets[i].distance < minDist) {
        minDist = state.targets[i].distance;
        gasit = true;
      }
    }
  }

  if (gasit) {
    _lastMinDist = minDist;
    int r = 0, g = 0, b = 0;
    computeProximityColor(minDist, r, g, b);
    setRgb(r, g, b);
  } else {
    // daca nu e nimeni in camera, stingem ledul ca sa economisim curent
    _lastMinDist = 9999;
    setRgb(0, 0, 0);
  }
}

`


## src/main.cpp
`cpp
#include <Arduino.h>
#include "config.h"
#include "radar_types.h"
#include "ld2450.h"
#include "display.h"
#include "proximity_led.h"

// radar heartbeat pe esp32-s3
// Core 0 citeste radarul pe serial la 256k si filtreaza tintele
// Core 1 deseneaza ecranul la ~35 fps si asculta comenzile din serial

// instantiem obiectele principale ale aplicatiei
LD2450Radar    radar;   // driverul radarului mmWave HLK-LD2450 (creeaza task-ul pe Core 0)
DisplayManager display; // motorul grafic pentru ecranul ST7789 (gestioneaza canvas-ul din RAM)
ProximityLED   proxLed; // indicatorul optic RGB NeoPixel de pe placa (GPIO 48)

// variabile pentru temporizare neblocanta (inlocuiesc functia clasica delay() ca sa nu blocam procesorul)
unsigned long lastUiUpdate = 0;
unsigned long lastSerialTelemetry = 0;

// afiseaza un raport text complet in consola seriala o data pe secunda (telemetrie 1 Hz)
void printSerialTelemetry(const CODSharedState &state, bool connected) {
  Serial.println("-------------------------------------------------");
  const char* fltStr = (state.filterMode == GHOST_FILTER_AGGRESSIVE) ? "AGRESIV" :
                       (state.filterMode == GHOST_FILTER_OFF) ? "FARA FILTRU (RAW)" : "ECHILIBRAT";
  const char* accStr = (state.accuracyMode == ACCURACY_PRECISION)  ? "PRECIZIE" :
                       (state.accuracyMode == ACCURACY_RESPONSIVE) ? "RAPID" : "NORMAL";

  Serial.printf("Radar: %s | Mod: %s | Filtru: %s | Acuratete: %s | Scala: %s\n",
                connected ? "CONECTAT" : "DECONECTAT",
                state.multiTargetActive ? "MULTI-TINTA" : "O SINGURA TINTA",
                fltStr, accStr,
                display.isCqbMode() ? "3 Metri (Zoom)" : "6 Metri (Normal)");

  Serial.printf("Tinte detectate: %d | Cel mai apropiat obiect: %d mm\n",
                state.activeCount, proxLed.getLastMinDist());

  unsigned long now = millis();
  for (int i = 0; i < MAX_TARGETS; i++) {
    if (state.targets[i].valid) {
      const auto &t = state.targets[i];
      // afisam coordonatele spatiale in milimetri si viteza in cm/s
      Serial.printf("  [Tinta %d] Distanta: %5d mm | X: %+5d mm | Y: %5d mm | Viteza: %3d cm/s (Vx=%+3d, Vy=%+3d) %s\n",
                    t.id, t.distance, t.x, t.y, t.speed, t.vx, t.vy,
                    t.isHeld ? "[RETINUT PE LOC]" : "[IN MISCARE]");
    } else {
      Serial.printf("  [Tinta %d] -\n", i + 1);
    }
  }
}

// citeste comenzile primite de la utilizator prin consola seriala (tastate in Serial Monitor)
void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = (char)toupper((unsigned char)Serial.read());

    switch (cmd) {
      // tasta 'A': comuta modul de acuratete (Tactical -> Precision -> Responsive)
      case 'A': {
        AccuracyMode curent = radar.getAccuracyMode();
        AccuracyMode urmator = (curent == ACCURACY_TACTICAL)   ? ACCURACY_PRECISION :
                               (curent == ACCURACY_PRECISION)  ? ACCURACY_RESPONSIVE :
                                                                 ACCURACY_TACTICAL;
        radar.setAccuracyMode(urmator);

        if (urmator == ACCURACY_PRECISION) {
          Serial.println("Mod acuratete: PRECIZIE (ancora la 4.5cm, blocheaza tremuratul pe loc)");
        } else if (urmator == ACCURACY_RESPONSIVE) {
          Serial.println("Mod acuratete: RAPID (deadband 1.5cm, reactioneaza instant la miscare)");
        } else {
          Serial.println("Mod acuratete: NORMAL (echilibrat, deadband 3.0cm)");
        }
        break;
      }

      // tasta 'G': comuta filtrul anti-ghost (Balanced -> Aggressive -> Off)
      case 'G': {
        GhostFilterMode curent = radar.getGhostFilterMode();
        GhostFilterMode urmator = (curent == GHOST_FILTER_BALANCED)   ? GHOST_FILTER_AGGRESSIVE :
                                  (curent == GHOST_FILTER_AGGRESSIVE) ? GHOST_FILTER_OFF        :
                                                                        GHOST_FILTER_BALANCED;
        radar.setGhostFilterMode(urmator);

        if (urmator == GHOST_FILTER_AGGRESSIVE) {
          Serial.println("Filtru: AGRESIV (con 90 grade, taie peretii laterali si cere 5 confirmari)");
        } else if (urmator == GHOST_FILTER_OFF) {
          Serial.println("Filtru: OPRIT (afiseaza date brute direct din radar)");
        } else {
          Serial.println("Filtru: ECHILIBRAT (con normal 110 grade, 3 confirmari)");
        }
        break;
      }

      // tasta 'M': activeaza modul Multi-Target (senzorul poate urmari pana la 3 persoane simultan)
      case 'M': {
        Serial.print("Trimitere comanda Multi-Target catre senzor... ");
        bool ok = radar.setMultiTargetMode(true);
        Serial.println(ok ? "Gata!" : "Eroare la configurare!");
        break;
      }

      // tasta 'S': activeaza modul Single-Target (senzorul urmareste doar cea mai puternica tinta)
      case 'S': {
        Serial.print("Trimitere comanda Single-Target catre senzor... ");
        bool ok = radar.setMultiTargetMode(false);
        Serial.println(ok ? "Gata!" : "Eroare la configurare!");
        break;
      }

      // tasta 'V': cere versiunea interna de firmware a modulului radar LD2450
      case 'V': {
        Serial.print("Interogare versiune firmware radar... ");
        String ver = radar.queryFirmwareVersion();
        if (ver.length() > 0) {
          Serial.print("Versiune firmware radar: ");
          Serial.println(ver);
        } else {
          Serial.println("Senzorul nu a raspuns.");
        }
        break;
      }

      // tasta 'F': resetare software completa a senzorului la setarile din fabrica
      case 'F': {
        Serial.println("Resetare radar la setarile din fabrica...");
        bool ok = radar.factoryReset();
        Serial.println(ok ? "Resetat cu succes!" : "Gata.");
        break;
      }

      // tasta 'R': comanda senzorului sa faca un reboot software
      case 'R': {
        Serial.print("Repornire senzor (reboot)... ");
        radar.reboot();
        Serial.println("Gata!");
        break;
      }

      // tasta 'O': roteste orientarea ecranului cu 180 de grade
      case 'O': {
        uint8_t rotCurenta = display.getOrientation();
        uint8_t rotNoua = (rotCurenta == 3) ? 1 : 3;
        display.setOrientation(rotNoua);
        Serial.printf("Rotatie ecran schimbata la: %d\n", rotNoua);
        break;
      }

      // tasta 'Z': schimba scala radarului intre 3 metri (CQB Zoom) si 6 metri (Normal)
      case 'Z': {
        display.toggleRangeMode();
        Serial.printf("Scala radar: %s\n",
                      display.isCqbMode() ? "3 Metri (Zoom Apropiat)" : "6 Metri (Normal)");
        break;
      }

      // tasta '?' sau 'H': afiseaza meniul cu toate comenzile disponibile
      case '?':
      case 'H': {
        Serial.println("\n--- Comenzi Disponibile in Consola ---");
        Serial.println("  [a] Schimba modul de acuratete (Normal -> Precizie -> Rapid)");
        Serial.println("  [g] Schimba filtrul anti-ghost (Echilibrat -> Agresiv -> Oprit)");
        Serial.println("  [m] Activeaza modul multi-tinta (3 persoane)");
        Serial.println("  [s] Activeaza modul o singura tinta");
        Serial.println("  [v] Afiseaza versiunea de firmware");
        Serial.println("  [f] Resetare la setarile din fabrica");
        Serial.println("  [r] Reporneste senzorul (reboot)");
        Serial.println("  [o] Roteste ecranul cu 180 de grade");
        Serial.println("  [z] Schimba distanta (3m Zoom / 6m Normal)\n");
        break;
      }

      default:
        break;
    }
  }
}

// functia de initializare la alimentarea placii ESP32-S3
void setup() {
  // pornim portul serial USB la viteza standard de 115200 baud
  Serial.begin(115200);
  delay(1000); // pauza scurta de o secunda ca sa aiba timp portul COM din PC sa se deschida

  Serial.println("\n--- RADAR HLK-LD2450 ESP32-S3 (LYNCEUS MK-I) ---");

  // Pasul 1: initializam ecranul TFT ST7789 si alocam memoria RAM pentru Double-Buffering
  Serial.print("Pornire ecran ST7789 (SPI 40MHz)... ");
  if (display.begin()) {
    Serial.println("OK");
  } else {
    Serial.println("EROARE CRITICA LA ECRAN");
  }

  // Pasul 2: initializam portul serial al radarului si lansam task-ul FreeRTOS pe Core 0
  Serial.print("Pornire radar LD2450 (UART 256.000 baud pe Core 0)... ");
  if (radar.begin(Serial1, RADAR_RX_PIN, RADAR_TX_PIN, RADAR_BAUD)) {
    Serial.println("OK");
  } else {
    Serial.println("EROARE LA RADAR");
  }

  // Pasul 3: pornim ledul RGB NeoPixel de pe placa (GPIO 48) si facem un test scurt de culori
  proxLed.begin();

  Serial.println("Sistemul a pornit cu succes! Tasteaza '?' in consola pentru comenzi.\n");
}

// bucla principala care ruleaza continuu pe Nucleul 1 (Core 1)
void loop() {
  // Pasul 1: verificam daca utilizatorul a tastat comenzi in consola seriala
  handleSerialCommands();

  // Pasul 2: preluam un instantaneu complet al datelor radarului de pe Nucleul 0.
  // apelul getSnapshot() foloseste spinlock hardware (portENTER_CRITICAL),
  // garantand ca nu citim coordonate pe jumatate scrise de Core 0.
  CODSharedState state = radar.getSnapshot();
  unsigned long now = millis();

  // verificam daca radarul inca emite date (daca nu a mai trimis nimic de 1.5s, e offline)
  bool connected = (state.lastHeartbeatMs > 0 && (now - state.lastHeartbeatMs < SENSOR_DEAD_MS));

  // Pasul 3: actualizam culoarea LED-ului RGB in functie de cel mai apropiat obiect
  proxLed.update(state, connected);

  // Pasul 4: randam ecranul la intervale regulate de 28 milisecunde (~35 FPS)
  unsigned long elapsed = now - lastUiUpdate;
  if (elapsed >= UI_REFRESH_INTERVAL_MS) {
    lastUiUpdate = now;
    display.render(state, connected);
  } else {
    // daca am terminat randarea mai repede de 28ms, lasam procesorul sa se odihneasca scurt,
    // evitand ca bucla while/loop sa consume 100% CPU inutil
    unsigned long ramase = UI_REFRESH_INTERVAL_MS - elapsed;
    if (ramase > 2) delay(ramase - 1);
    else delay(1);
  }

  // Pasul 5: emitem raportul de telemetrie in consola seriala o data pe secunda (1 Hz)
  if (now - lastSerialTelemetry >= SERIAL_TELEMETRY_MS) {
    lastSerialTelemetry = now;
    printSerialTelemetry(state, connected);
  }
}

`

