#include <Audio.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>

#include <OctoWS2811.h>
#include <math.h>

// =====================================================
//  CONFIG HARDWARE & PINS
// =====================================================

// ---------------------------
// Potentiomètres VOLUME
// ---------------------------
#define POT_VOL_SUB A0
#define POT_VOL_BASS A3
#define POT_VOL_MIDLOW A11
#define POT_VOL_MIDHIGH A14
#define POT_VOL_TOP A17

// ---------------------------
// Potentiomètres FREQUENCE (coupures LR4)
// ---------------------------
// Étape 1 : Sub/Bass
#define POT_FC_SUB_BASS A1
// Étape 2 : Bass/MidLow
#define POT_FC_BASS_MIDLOW A8
// Étape 3 : MidLow/MidHigh
#define POT_FC_MIDLOW_MIDHIGH A12
// Étape 4 : MidHigh/Top
#define POT_FC_MIDHIGH_TOP A15

// ---------------------------
// Potentiomètres Q (par coupure)
// ---------------------------
#define POT_Q_SUB A2       // Q de fc1
#define POT_Q_BASS A10     // Q de fc2
#define POT_Q_MIDLOW A13   // Q de fc3
#define POT_Q_MIDHIGH A16  // Q de fc4

// ---------------------------
// Switch MUTE (à la masse)  -> MUTE quand LOW
// ---------------------------
#define SWK_SUB 28
#define SWK_BASS 29
#define SWK_MID 30
#define SWK_UMID 31
#define SWK_TOPS 32

// ---------------------------
// BOUTONS TOPS (à la masse) -> actif quand LOW
// ---------------------------
#define BTN_TOP_LP 2       // choisit la fréquence du LP (13k / 18k)
#define BTN_LIMITER 3      // active/désactive le limiteur

// ---------------------------
// WS2812
// ---------------------------
#define LED_PIN 4
#define NUM_LEDS 56  // 7 modules x 8 LEDs
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

// =====================================================
//  LEDS: remplaçant minimal FastLED + OctoWS2811 (pinlist = {4})
// =====================================================

// --------- Mini remplaçant FastLED (CRGB/CHSV/utilitaires) ----------
struct CRGB {
  uint8_t r, g, b;
  CRGB()
    : r(0), g(0), b(0) {}
  CRGB(uint8_t rr, uint8_t gg, uint8_t bb)
    : r(rr), g(gg), b(bb) {}
  static const CRGB Black, Red, Green, Blue, White;
};
const CRGB CRGB::Black = CRGB(0, 0, 0);
const CRGB CRGB::Red = CRGB(255, 0, 0);
const CRGB CRGB::Green = CRGB(0, 255, 0);
const CRGB CRGB::Blue = CRGB(0, 0, 255);
const CRGB CRGB::White = CRGB(255, 255, 255);

struct CHSV {
  uint8_t h, s, v;
  CHSV(uint8_t hh, uint8_t ss, uint8_t vv)
    : h(hh), s(ss), v(vv) {}
};

static inline uint8_t lerp8by8(uint8_t a, uint8_t b, uint8_t frac) {
  int16_t d = (int16_t)b - (int16_t)a;
  return (uint8_t)(a + ((d * frac) >> 8));
}

static inline CRGB hsv2rgb(const CHSV &in) {
  uint8_t region = in.h / 43;
  uint8_t remainder = (in.h - region * 43) * 6;

  uint8_t p = (uint16_t)in.v * (255 - in.s) >> 8;
  uint8_t q = (uint16_t)in.v * (255 - ((uint16_t)in.s * remainder >> 8)) >> 8;
  uint8_t t = (uint16_t)in.v * (255 - ((uint16_t)in.s * (255 - remainder) >> 8)) >> 8;

  switch (region) {
    case 0: return CRGB(in.v, t, p);
    case 1: return CRGB(q, in.v, p);
    case 2: return CRGB(p, in.v, t);
    case 3: return CRGB(p, q, in.v);
    case 4: return CRGB(t, p, in.v);
    default: return CRGB(in.v, p, q);
  }
}

static inline void fill_solid(CRGB *arr, int n, const CRGB &c) {
  for (int i = 0; i < n; i++) arr[i] = c;
}

// Buffer logique (comme FastLED)
CRGB leds[NUM_LEDS];

// --------- OctoWS2811 Teensy 4.1 pin list personnalisée ----------
static const int LEDS_PER_STRIP = NUM_LEDS;  // une seule sortie, NUM_LEDS pixels
static const uint8_t NUM_PINS = 1;
static const uint8_t PIN_LIST[NUM_PINS] = { 4 };  // Data sur pin 4

DMAMEM int displayMemory[LEDS_PER_STRIP * 6];
int drawingMemory[LEDS_PER_STRIP * 6];

static const int OCTO_CONFIG = WS2811_GRB | WS2811_800kHz;

// IMPORTANT: ce constructeur existe sur Teensy 4.x (pinlist)
OctoWS2811 octo(LEDS_PER_STRIP, displayMemory, drawingMemory, OCTO_CONFIG, NUM_PINS, PIN_LIST);

static const uint8_t LED_BRIGHTNESS = 5;  // 0..255 (équivalent FastLED.setBrightness(5))

static inline uint8_t scale8(uint8_t x, uint8_t s) {
  return (uint16_t)x * (uint16_t)s / 255;
}

void ledsShow() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t r = scale8(leds[i].r, LED_BRIGHTNESS);
    uint8_t g = scale8(leds[i].g, LED_BRIGHTNESS);
    uint8_t b = scale8(leds[i].b, LED_BRIGHTNESS);
    octo.setPixel(i, r, g, b);
  }
  octo.show();
}

void ledsClear(bool showNow = true) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  if (showNow) ledsShow();
}

// Segments (8 LEDs chacun)
#define SEG_SIZE 8
#define SEG_IN 0
#define SEG_SUB 1
#define SEG_BASS 2
#define SEG_MID 3
#define SEG_UMID 4
#define SEG_TOPS 5
#define SEG_OUT 6

const bool SEG_REVERSED[7] = {
  false,  // IN
  true,   // SUB
  false,  // BASS
  true,   // MID
  false,  // U/MID
  true,   // TOPS
  false   // OUT
};

// =====================================================
//  OBJETS AUDIO (LR4 5 bandes - ROUTAGE "A")
//  + LP doux avant ampTop (bande TOP)
//  + Limiteur bypassable en sortie (waveshaper + sélecteur)
// =====================================================

// GUItool: begin automatically generated code
AudioInputI2S i2s1;       //xy=120,140
AudioAnalyzePeak peakIn;  //xy=120,200

// SUB : LP(fc1)^2
AudioFilterStateVariable sub_L1;  //xy=300,60
AudioFilterStateVariable sub_L2;  //xy=460,60
AudioAmplifier ampSub;            //xy=620,60
AudioAnalyzePeak peakSub;         //xy=760,60

// BASS : HP(fc1)^2 -> LP(fc2)^2
AudioFilterStateVariable bass_H1;  //xy=300,140
AudioFilterStateVariable bass_H2;  //xy=460,140
AudioFilterStateVariable bass_L1;  //xy=620,140
AudioFilterStateVariable bass_L2;  //xy=780,140
AudioAmplifier ampBass;            //xy=940,140
AudioAnalyzePeak peakBass;         //xy=1080,140

// MIDLOW : HP(fc2)^2 -> LP(fc3)^2
AudioFilterStateVariable midlow_H1;  //xy=300,220
AudioFilterStateVariable midlow_H2;  //xy=460,220
AudioFilterStateVariable midlow_L1;  //xy=620,220
AudioFilterStateVariable midlow_L2;  //xy=780,220
AudioAmplifier ampMidLow;            //xy=940,220
AudioAnalyzePeak peakMidLow;         //xy=1080,220

// MIDHIGH : HP(fc3)^2 -> LP(fc4)^2
AudioFilterStateVariable midhigh_H1;  //xy=300,300
AudioFilterStateVariable midhigh_H2;  //xy=460,300
AudioFilterStateVariable midhigh_L1;  //xy=620,300
AudioFilterStateVariable midhigh_L2;  //xy=780,300
AudioAmplifier ampMidHigh;            //xy=940,300
AudioAnalyzePeak peakMidHigh;         //xy=1080,300

// TOP : HP(fc4)^2 -> (LP doux 13k/18k) -> AMP
AudioFilterStateVariable top_H1;  //xy=300,380
AudioFilterStateVariable top_H2;  //xy=460,380
AudioFilterBiquad top_LP;         // <-- AJOUT (LP doux AVANT ampTop)
AudioAmplifier ampTop;            //xy=620,380
AudioAnalyzePeak peakTop;         //xy=760,380

// MIX (5 bandes)
AudioMixer4 mix1;          //xy=1220,180  // Sub, Bass, MidLow, MidHigh
AudioMixer4 mix2;          //xy=1380,240  // mix1 + Top

// Sortie : Limiteur bypassable
AudioEffectWaveshaper limiterWS;  // <-- AJOUT (soft limiter)
AudioMixer4 postMix;              // <-- AJOUT (sélecteur dry/limited)

AudioAnalyzePeak peakOut;  //xy=1540,280
AudioOutputI2S i2s2;       //xy=1560,220

AudioConnection patchCord1(i2s1, 0, peakIn, 0);

// SUB
AudioConnection patchCord2(i2s1, 0, sub_L1, 0);
AudioConnection patchCord3(sub_L1, 0, sub_L2, 0);  // LP out(0)
AudioConnection patchCord4(sub_L2, 0, ampSub, 0);
AudioConnection patchCord5(ampSub, 0, peakSub, 0);

// BASS
AudioConnection patchCord6(i2s1, 0, bass_H1, 0);
AudioConnection patchCord7(bass_H1, 2, bass_H2, 0);  // HP out(2)
AudioConnection patchCord8(bass_H2, 2, bass_L1, 0);
AudioConnection patchCord9(bass_L1, 0, bass_L2, 0);  // LP out(0)
AudioConnection patchCord10(bass_L2, 0, ampBass, 0);
AudioConnection patchCord11(ampBass, 0, peakBass, 0);

// MIDLOW
AudioConnection patchCord12(i2s1, 0, midlow_H1, 0);
AudioConnection patchCord13(midlow_H1, 2, midlow_H2, 0);
AudioConnection patchCord14(midlow_H2, 2, midlow_L1, 0);
AudioConnection patchCord15(midlow_L1, 0, midlow_L2, 0);
AudioConnection patchCord16(midlow_L2, 0, ampMidLow, 0);
AudioConnection patchCord17(ampMidLow, 0, peakMidLow, 0);

// MIDHIGH
AudioConnection patchCord18(i2s1, 0, midhigh_H1, 0);
AudioConnection patchCord19(midhigh_H1, 2, midhigh_H2, 0);
AudioConnection patchCord20(midhigh_H2, 2, midhigh_L1, 0);
AudioConnection patchCord21(midhigh_L1, 0, midhigh_L2, 0);
AudioConnection patchCord22(midhigh_L2, 0, ampMidHigh, 0);
AudioConnection patchCord23(ampMidHigh, 0, peakMidHigh, 0);

// TOP (MODIFIÉ: insertion top_LP avant ampTop)
AudioConnection patchCord24(i2s1, 0, top_H1, 0);
AudioConnection patchCord25(top_H1, 2, top_H2, 0);
AudioConnection patchCord26(top_H2, 2, top_LP, 0);     // <-- AU LIEU de top_H2 -> ampTop
AudioConnection patchCord26b(top_LP, 0, ampTop, 0);    // <-- AJOUT
AudioConnection patchCord27(ampTop, 0, peakTop, 0);

// MIX
AudioConnection patchCord28(ampSub, 0, mix1, 0);
AudioConnection patchCord29(ampBass, 0, mix1, 1);
AudioConnection patchCord30(ampMidLow, 0, mix1, 2);
AudioConnection patchCord31(ampMidHigh, 0, mix1, 3);

AudioConnection patchCord32(mix1, 0, mix2, 0);
AudioConnection patchCord33(ampTop, 0, mix2, 1);

// SORTIE (MODIFIÉE: limiterWS + postMix, au lieu de mix2 -> i2s2 direct)
AudioConnection patchCord34(mix2, 0, postMix, 0);       // dry
AudioConnection patchCord35(mix2, 0, limiterWS, 0);     // limited path
AudioConnection patchCord36(limiterWS, 0, postMix, 1);  // limited into selector

AudioConnection patchCord37(postMix, 0, i2s2, 0);
AudioConnection patchCord38(postMix, 0, peakOut, 0);

AudioSynthWaveformDc dcZero;
AudioConnection patchCordRight(dcZero, 0, i2s2, 1);

AudioControlSGTL5000 sgtl5000_1;  //xy=120,430
// GUItool: end automatically generated code

// Table waveshaper (soft limiter)
float limiterTable[257];

// =====================================================
//  PARAMÈTRES CROSSOVER & UTILS
// =====================================================

// Plages de fréquences (log) autour de 100 / 300 / 1000 / 4000 Hz
const float FC1_MIN = 60.0f;  // Sub/Bass
const float FC1_MAX = 180.0f;

const float FC2_MIN = 180.0f;  // Bass/MidLow
const float FC2_MAX = 600.0f;

const float FC3_MIN = 600.0f;  // MidLow/MidHigh
const float FC3_MAX = 2500.0f;

const float FC4_MIN = 2500.0f;  // MidHigh/Top
const float FC4_MAX = 8000.0f;

// Q : 0.7 -> 5.0 (doc Teensy StateVariable)
const float Q_MIN = 0.7f;
const float Q_MAX = 5.0f;

// VU lissé
float vuSub = 0, vuBass = 0, vuMidLow = 0, vuMidHigh = 0, vuTop = 0, vuIn = 0, vuOut = 0;

// Peak-hold
uint8_t peakHold[7] = { 0 };    // position 0..7
uint32_t peakHoldT[7] = { 0 };  // timer pour la retombée

float potTo01(int raw) {
  return (float)raw / 1023.0f;
}

// mapping expo pour les fréquences
float potToFreq(float v, float fmin, float fmax) {
  float logMin = log10f(fmin);
  float logMax = log10f(fmax);
  float logF = logMin + v * (logMax - logMin);
  return powf(10.0f, logF);
}

// mapping pot -> Q
float potToQ(int raw) {
  float v = potTo01(raw);  // 0..1
  return Q_MIN + v * (Q_MAX - Q_MIN);
}

// Anti-pop : rampe de gain
float smoothGain(float current, float target) {
  const float COEFF = 0.20f;
  return current + (target - current) * COEFF;
}

// Gains "courants" (rampés)
float gSub = 1.0f, gBass = 1.0f, gMidLow = 1.0f, gMidHigh = 1.0f, gTop = 1.0f;

static inline float clampf(float x, float a, float b) {
  return (x < a) ? a : (x > b) ? b
                               : x;
}

// mapping "centre 0 dB" avec loi dB (log en amplitude)
// v = 0..1 (pot)
// dBmin < 0 (atténuation max), dBmax > 0 (boost max)
// deadZone = largeur autour du centre qui colle à 0 dB (ex 0.03 = ±3%)
float potToGainCenteredDB(float v, float dBmin, float dBmax, float deadZone = 0.03f) {
  v = clampf(v, 0.0f, 1.0f);

  const float mid = 0.5f;
  float x = v - mid;  // [-0.5 .. +0.5]
  float ax = fabsf(x);

  // "vrai cran" : zone morte autour du milieu => gain exactement 1.0
  if (ax <= deadZone) return 1.0f;

  // normalisation hors zone morte => t in [0..1]
  float t = (ax - deadZone) / (0.5f - deadZone);
  t = clampf(t, 0.0f, 1.0f);

  // courbe "audio" (un peu progressive près du centre)
  // 1.0 = linéaire, 1.5..2.0 = plus doux au centre
  const float SHAPE = 1.6f;
  t = powf(t, SHAPE);

  float dB = (x < 0.0f) ? (-t * fabsf(dBmin)) : (t * dBmax);

  // conversion dB -> gain linéaire
  return powf(10.0f, dB / 20.0f);
}

const float VOL_DB_MIN = -24.0f;
const float VOL_DB_MAX = +18.0f;
const float VOL_DEADZONE = 0.035f;  // ~±3.5% autour du centre

// -----------------------------------------------------
// LED / VU helpers
// -----------------------------------------------------
CRGB vuColorByStep(uint8_t i) {
  const uint8_t H_BLUE = 160;
  const uint8_t H_GREEN = 96;
  const uint8_t H_YELLOW = 48;
  const uint8_t H_ORANGE = 24;
  const uint8_t H_RED = 0;

  uint8_t hue;
  switch (i) {
    case 0: hue = H_BLUE; break;
    case 1: hue = lerp8by8(H_BLUE, H_GREEN, 180); break;
    case 2:
    case 3: hue = H_GREEN; break;
    case 4: hue = lerp8by8(H_GREEN, H_YELLOW, 160); break;
    case 5: hue = H_YELLOW; break;
    case 6: hue = H_ORANGE; break;
    default: hue = H_RED; break;
  }
  return hsv2rgb(CHSV(hue, 255, 200));
}

void setSegmentVU(uint8_t seg, uint8_t level) {
  if (seg >= 7) return;
  if (level > SEG_SIZE) level = SEG_SIZE;

  uint32_t t = millis();
  uint8_t peakPos = (level == 0) ? 0 : (level - 1);

  if (level > 0 && peakPos >= peakHold[seg]) {
    peakHold[seg] = peakPos;
    peakHoldT[seg] = t;
  } else {
    if (t - peakHoldT[seg] > 250) {
      static uint32_t lastFall[7] = { 0 };
      if (t - lastFall[seg] > 80) {
        lastFall[seg] = t;
        if (peakHold[seg] > 0) peakHold[seg]--;
      }
    }
  }

  uint8_t start = seg * SEG_SIZE;
  for (uint8_t i = 0; i < SEG_SIZE; i++) {
    uint8_t phys = SEG_REVERSED[seg] ? (SEG_SIZE - 1 - i) : i;
    uint8_t idx = start + phys;

    if (i < level) leds[idx] = vuColorByStep(i);
    else leds[idx] = CRGB::Black;
  }

  if (level > 0) {
    uint8_t i = peakHold[seg];
    uint8_t phys = SEG_REVERSED[seg] ? (SEG_SIZE - 1 - i) : i;
    uint8_t idx = start + phys;
    leds[idx] = vuColorByStep(i);
  }
}

float smoothVU(float current, float target) {
  const float ATTACK = 0.95f;
  const float RELEASE = 0.10f;
  return current + (target - current) * (target > current ? ATTACK : RELEASE);
}

uint8_t levelFromPeak_dB(float p) {
  if (p < 1e-6f) return 0;
  float db = 20.0f * log10f(p);
  static const float th[SEG_SIZE] = { -36.0f, -24.0f, -18.0f, -12.0f, -9.0f, -6.0f, -3.0f, -1.0f };
  uint8_t lvl = 0;
  for (uint8_t i = 0; i < SEG_SIZE; i++) {
    if (db >= th[i]) lvl = i + 1;
  }
  return lvl;
}

void testWS2812() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  ledsShow();
  delay(50);

  fill_solid(leds, NUM_LEDS, CRGB::Red);
  ledsShow();
  delay(500);

  fill_solid(leds, NUM_LEDS, CRGB::Green);
  ledsShow();
  delay(500);

  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  ledsShow();
  delay(500);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  ledsShow();

  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::White;
    ledsShow();
    delay(25);
    leds[i] = CRGB::Black;
  }
  ledsClear(true);
}

// =====================================================
//  APPLY fc & Q to the new routing
// =====================================================

void setCrossoverFrequencies(float fc1, float fc2, float fc3, float fc4) {
  sub_L1.frequency(fc1);
  sub_L2.frequency(fc1);
  bass_H1.frequency(fc1);
  bass_H2.frequency(fc1);
  bass_L1.frequency(fc2);
  bass_L2.frequency(fc2);
  midlow_H1.frequency(fc2);
  midlow_H2.frequency(fc2);
  midlow_L1.frequency(fc3);
  midlow_L2.frequency(fc3);
  midhigh_H1.frequency(fc3);
  midhigh_H2.frequency(fc3);
  midhigh_L1.frequency(fc4);
  midhigh_L2.frequency(fc4);
  top_H1.frequency(fc4);
  top_H2.frequency(fc4);
}

// Q par coupure (fc1..fc4)
void setCrossoverQ_Sub(float Q) {
  sub_L1.resonance(Q);
  sub_L2.resonance(Q);
  bass_H1.resonance(Q);
  bass_H2.resonance(Q);
}
void setCrossoverQ_Bass(float Q) {
  bass_L1.resonance(Q);
  bass_L2.resonance(Q);
  midlow_H1.resonance(Q);
  midlow_H2.resonance(Q);
}
void setCrossoverQ_MidLow(float Q) {
  midlow_L1.resonance(Q);
  midlow_L2.resonance(Q);
  midhigh_H1.resonance(Q);
  midhigh_H2.resonance(Q);
}
void setCrossoverQ_MidHigh(float Q) {
  midhigh_L1.resonance(Q);
  midhigh_L2.resonance(Q);
  top_H1.resonance(Q);
  top_H2.resonance(Q);
}

// =====================================================
//  SETUP
// =====================================================

void setup() {
  AudioMemory(200);
  analogReadResolution(10);  // 0..1023

  //Codec
  sgtl5000_1.enable();
  sgtl5000_1.inputSelect(AUDIO_INPUT_LINEIN);
  sgtl5000_1.adcHighPassFilterFreeze();
  sgtl5000_1.adcHighPassFilterDisable();
  sgtl5000_1.dacVolumeRampDisable();
  sgtl5000_1.audioProcessorDisable();
  sgtl5000_1.autoVolumeDisable();
  sgtl5000_1.surroundSoundDisable();
  sgtl5000_1.enhanceBassDisable();
  sgtl5000_1.eqSelect(0);
  sgtl5000_1.lineInLevel(0);     // pas de boost d'entrée
  sgtl5000_1.lineOutLevel(13);   // niveau line-out typique
  sgtl5000_1.volume(0.0f);       // casque coupé
  sgtl5000_1.micGain(0);   // micro coupé
  dcZero.amplitude(0.0f); // RIGHT OUT muet 
  


  // Mixers : somme des 5 bandes
  mix1.gain(0, 1.0f);
  mix1.gain(1, 1.0f);
  mix1.gain(2, 1.0f);
  mix1.gain(3, 1.0f);

  mix2.gain(0, 1.0f);
  mix2.gain(1, 1.0f);
  mix2.gain(2, 0.0f);
  mix2.gain(3, 0.0f);

  // Switchs mute (tirage interne vers 3.3V)
  pinMode(SWK_SUB, INPUT_PULLUP);
  pinMode(SWK_BASS, INPUT_PULLUP);
  pinMode(SWK_MID, INPUT_PULLUP);
  pinMode(SWK_UMID, INPUT_PULLUP);
  pinMode(SWK_TOPS, INPUT_PULLUP);

  // Boutons TOPS
  pinMode(BTN_TOP_LP, INPUT);
  pinMode(BTN_LIMITER, INPUT);

  // LEDs (OctoWS2811)
  octo.begin();
  ledsClear(true);
  testWS2812();

  // Init volumes à 0 dB
  ampSub.gain(1.0f);
  ampBass.gain(1.0f);
  ampMidLow.gain(1.0f);
  ampMidHigh.gain(1.0f);
  ampTop.gain(1.0f);

  // Fréquences de départ
  setCrossoverFrequencies(100.0f, 300.0f, 1000.0f, 4000.0f);

  // Q de départ : Butterworth (LR4)
  setCrossoverQ_Sub(0.707f);
  setCrossoverQ_Bass(0.707f);
  setCrossoverQ_MidLow(0.707f);
  setCrossoverQ_MidHigh(0.707f);

  // ---------------------------
  // LP doux TOP (bouton pin 2 : 13k / 18k)
  // ---------------------------
  // Choix "13 kHz" comme valeur "anti-harsh" (entre 12-14 kHz)
  // OFF (bouton relâché) -> 18 kHz (plus ouvert)
  // ON  (bouton pressé)  -> 13 kHz (plus doux)
  top_LP.setLowpass(0, 18000.0f, 0.707f);

  // ---------------------------
  // Limiteur soft (waveshaper) + sélecteur
  // ---------------------------
  // Courbe soft clip : y = x / (1 + k*|x|) ; k plus grand => limite plus
// Limiteur "transparent jusqu'au rouge", puis soft clip
// T : seuil (0..1). Plus haut = limite plus tard
// k : force au-dessus du seuil. Plus haut = plus ferme
const float T = 0.85f;  // ~ -0.9 dBFS (proche de ton rouge -1 dB)
const float k = 3.0f;

for (int i = 0; i <= 256; i++) {
  float x = (i - 128) / 128.0f;   // -1..+1
  float ax = fabsf(x);
  float s  = (x < 0.0f) ? -1.0f : 1.0f;

  float y;
  if (ax <= T) {
    y = x; // transparent
  } else {
    float u = (ax - T) / (1.0f - T);   // 0..1
    float shaped = u / (1.0f + k * u); // soft clip au-dessus du seuil
    y = s * (T + shaped * (1.0f - T));
  }

  limiterTable[i] = clampf(y, -1.0f, 1.0f);
}
limiterWS.shape(limiterTable, 257);




  // Limiteur OFF par défaut : dry = 1, limited = 0
  postMix.gain(0, 1.0f);
  postMix.gain(1, 0.0f);
  postMix.gain(2, 0.0f);
  postMix.gain(3, 0.0f);
}

// =====================================================
//  LOOP
// =====================================================

void loop() {
  static uint32_t lastUpdate = 0;
  uint32_t now = millis();

  // Pour crossfade limiter (anti-pop)
  static float gDry = 1.0f;
  static float gLim = 0.0f;

  // Mise à jour ~50 Hz (20 ms)
  if (now - lastUpdate >= 20) {
    lastUpdate = now;

    // --------- BOUTON LP TOP (pin 2) ---------
    bool lpTopOn = (digitalRead(BTN_TOP_LP) == LOW);
    // ON  -> 13 kHz, OFF -> 18 kHz
    float lpFreq = lpTopOn ? 13000.0f : 18000.0f;
    top_LP.setLowpass(0, lpFreq, 0.707f);

    // --------- BOUTON LIMITEUR (pin 3) ---------
    bool limiterOn = (digitalRead(BTN_LIMITER) == LOW);

    // crossfade dry/limited (anti-pop)
    float tgtDry = limiterOn ? 0.0f : 1.0f;
    float tgtLim = limiterOn ? 1.0f : 0.0f;
    gDry += (tgtDry - gDry) * 0.20f;
    gLim += (tgtLim - gLim) * 0.20f;
    postMix.gain(0, gDry);
    postMix.gain(1, gLim);

    // --------- LECTURE SWITCHS MUTE ---------
    bool muteSub = (digitalRead(SWK_SUB) == LOW);
    bool muteBass = (digitalRead(SWK_BASS) == LOW);
    bool muteMid = (digitalRead(SWK_MID) == LOW);
    bool muteUMid = (digitalRead(SWK_UMID) == LOW);
    bool muteTops = (digitalRead(SWK_TOPS) == LOW);

    // --------- LECTURE POTS FREQUENCE ---------
    float v_fc1 = potTo01(analogRead(POT_FC_SUB_BASS));
    float v_fc2 = potTo01(analogRead(POT_FC_BASS_MIDLOW));
    float v_fc3 = potTo01(analogRead(POT_FC_MIDLOW_MIDHIGH));
    float v_fc4 = potTo01(analogRead(POT_FC_MIDHIGH_TOP));

    float fc1 = potToFreq(v_fc1, FC1_MIN, FC1_MAX);
    float fc2 = potToFreq(v_fc2, FC2_MIN, FC2_MAX);
    float fc3 = potToFreq(v_fc3, FC3_MIN, FC3_MAX);
    float fc4 = potToFreq(v_fc4, FC4_MIN, FC4_MAX);

    setCrossoverFrequencies(fc1, fc2, fc3, fc4);

    // --------- LECTURE POTS Q ---------
    float Qsub = potToQ(analogRead(POT_Q_SUB));
    float Qbass = potToQ(analogRead(POT_Q_BASS));
    float QmidLow = potToQ(analogRead(POT_Q_MIDLOW));
    float QmidHigh = potToQ(analogRead(POT_Q_MIDHIGH));

    setCrossoverQ_Sub(Qsub);
    setCrossoverQ_Bass(Qbass);
    setCrossoverQ_MidLow(QmidLow);
    setCrossoverQ_MidHigh(QmidHigh);

    // --------- LECTURE POTS VOLUME ---------
    float vSub = potTo01(analogRead(POT_VOL_SUB));
    float vBass = potTo01(analogRead(POT_VOL_BASS));
    float vMidLow = potTo01(analogRead(POT_VOL_MIDLOW));
    float vMidHigh = potTo01(analogRead(POT_VOL_MIDHIGH));
    float vTop = potTo01(analogRead(POT_VOL_TOP));

    // --------- APPLICATION GAINS + MUTE (avec fade) ---------
    float tgtSub = muteSub ? 0.0f : potToGainCenteredDB(vSub, VOL_DB_MIN, VOL_DB_MAX, VOL_DEADZONE);
    float tgtBass = muteBass ? 0.0f : potToGainCenteredDB(vBass, VOL_DB_MIN, VOL_DB_MAX, VOL_DEADZONE);
    float tgtMidLow = muteMid ? 0.0f : potToGainCenteredDB(vMidLow, VOL_DB_MIN, VOL_DB_MAX, VOL_DEADZONE);
    float tgtMidHigh = muteUMid ? 0.0f : potToGainCenteredDB(vMidHigh, VOL_DB_MIN, VOL_DB_MAX, VOL_DEADZONE);
    float tgtTop = muteTops ? 0.0f : potToGainCenteredDB(vTop, VOL_DB_MIN, VOL_DB_MAX, VOL_DEADZONE);

    gSub = smoothGain(gSub, tgtSub);
    gBass = smoothGain(gBass, tgtBass);
    gMidLow = smoothGain(gMidLow, tgtMidLow);
    gMidHigh = smoothGain(gMidHigh, tgtMidHigh);
    gTop = smoothGain(gTop, tgtTop);

    ampSub.gain(gSub);
    ampBass.gain(gBass);
    ampMidLow.gain(gMidLow);
    ampMidHigh.gain(gMidHigh);
    ampTop.gain(gTop);

    // --------- PEAKS & LEDs ---------
    float pSub = 0, pBass = 0, pMidLow = 0, pMidHigh = 0, pTop = 0, pIn = 0, pOut = 0;

    if (peakSub.available()) pSub = peakSub.read();
    if (peakBass.available()) pBass = peakBass.read();
    if (peakMidLow.available()) pMidLow = peakMidLow.read();
    if (peakMidHigh.available()) pMidHigh = peakMidHigh.read();
    if (peakTop.available()) pTop = peakTop.read();
    if (peakIn.available()) pIn = peakIn.read();
    if (peakOut.available()) pOut = peakOut.read();

    vuSub = smoothVU(vuSub, pSub);
    vuBass = smoothVU(vuBass, pBass);
    vuMidLow = smoothVU(vuMidLow, pMidLow);
    vuMidHigh = smoothVU(vuMidHigh, pMidHigh);
    vuTop = smoothVU(vuTop, pTop);
    vuIn = smoothVU(vuIn, pIn);
    vuOut = smoothVU(vuOut, pOut);

    uint8_t levSub = levelFromPeak_dB(vuSub);
    uint8_t levBass = levelFromPeak_dB(vuBass);
    uint8_t levMidLow = levelFromPeak_dB(vuMidLow);
    uint8_t levMidHigh = levelFromPeak_dB(vuMidHigh);
    uint8_t levTop = levelFromPeak_dB(vuTop);
    uint8_t levIn = levelFromPeak_dB(vuIn);
    uint8_t levOut = levelFromPeak_dB(vuOut);

    setSegmentVU(SEG_IN, levIn);
    setSegmentVU(SEG_SUB, levSub);
    setSegmentVU(SEG_BASS, levBass);
    setSegmentVU(SEG_MID, levMidLow);
    setSegmentVU(SEG_UMID, levMidHigh);
    setSegmentVU(SEG_TOPS, levTop);
    setSegmentVU(SEG_OUT, levOut);

    ledsShow();
  }
}
