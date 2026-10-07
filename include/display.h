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
