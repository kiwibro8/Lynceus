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
