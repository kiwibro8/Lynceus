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
