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
