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
