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
