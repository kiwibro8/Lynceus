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
