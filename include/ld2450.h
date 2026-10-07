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
