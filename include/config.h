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
