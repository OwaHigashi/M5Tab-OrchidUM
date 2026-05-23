// M5Tab-Orchid
// M5Stack Tab5 Chord Synthesizer inspired by Telepathic Instruments Orchid
// Board : M5Stack Tab5 (ESP32-P4 + ESP32-C6 via ESP-Hosted)
// Libs  : M5Unified, M5GFX
//
// Features inspired by Orchid:
//   - 16-voice polyphonic synthesizer
//   - Chord generation: root note + chord type + voicing
//   - Virtual analog, FM, and lead/piano synth engines
//   - Bass synth engine for low frequencies
//   - Effects: delay, reverb, chorus
//   - 1280x720 touch UI optimized for Tab5
//
// UI Layout:
//   - Top: Synth engine selector, preset display
//   - Center: One-octave velocity-sensitive keyboard (root note selection)
//   - Middle: Chord type selector (Major, Minor, 7th, Sus, Dim, Aug, etc.)
//   - Bottom: Chord voicing controls, bass toggle, effects panel

#include <M5Unified.h>
#include <math.h>

// ==== Screen ====
static constexpr int SCREEN_W = 1280;
static constexpr int SCREEN_H = 720;

// ==== Fonts ====
#define FONT_TITLE   (&fonts::FreeSansBold24pt7b)
#define FONT_LARGE   (&fonts::FreeSansBold18pt7b)
#define FONT_MED     (&fonts::FreeSans18pt7b)
#define FONT_SMALL   (&fonts::FreeSans12pt7b)

// ==== Colors ====
static constexpr uint16_t COL_BG         = TFT_BLACK;
static constexpr uint16_t COL_PANEL      = 0x10A2;  // dark slate
static constexpr uint16_t COL_BTN        = 0x2945;  // slate
static constexpr uint16_t COL_BTN_ACTIVE = TFT_GREEN;
static constexpr uint16_t COL_BTN_TXT    = TFT_WHITE;
static constexpr uint16_t COL_WHITEKEY   = TFT_WHITE;
static constexpr uint16_t COL_BLACKKEY   = 0x2104;
static constexpr uint16_t COL_ACCENT     = TFT_CYAN;
static constexpr uint16_t COL_VALUE      = TFT_YELLOW;

// ==== Synth Configuration ====
static constexpr int MAX_VOICES = 16;
static constexpr int SAMPLE_RATE = 44100;
static constexpr int BUFFER_SIZE = 512;

// ==== Synth Engine Types ====
enum SynthEngine {
  ENGINE_VIRTUAL_ANALOG,
  ENGINE_FM,
  ENGINE_LEAD_PIANO
};

// ==== Chord Types ====
enum ChordType {
  CHORD_MAJOR,
  CHORD_MINOR,
  CHORD_MAJOR7,
  CHORD_MINOR7,
  CHORD_DOM7,
  CHORD_SUS2,
  CHORD_SUS4,
  CHORD_DIM,
  CHORD_AUG,
  CHORD_ADD9,
  CHORD_COUNT
};

// Chord interval definitions (semitones from root)
static const int8_t chordIntervals[][6] = {
  {0, 4, 7, -1, -1, -1},        // Major (root, M3, P5)
  {0, 3, 7, -1, -1, -1},        // Minor (root, m3, P5)
  {0, 4, 7, 11, -1, -1},        // Major7 (root, M3, P5, M7)
  {0, 3, 7, 10, -1, -1},        // Minor7 (root, m3, P5, m7)
  {0, 4, 7, 10, -1, -1},        // Dom7 (root, M3, P5, m7)
  {0, 2, 7, -1, -1, -1},        // Sus2 (root, M2, P5)
  {0, 5, 7, -1, -1, -1},        // Sus4 (root, P4, P5)
  {0, 3, 6, -1, -1, -1},        // Dim (root, m3, d5)
  {0, 4, 8, -1, -1, -1},        // Aug (root, M3, A5)
  {0, 4, 7, 14, -1, -1},        // Add9 (root, M3, P5, M9)
};

static const char* chordNames[] = {
  "Major", "Minor", "Maj7", "Min7", "Dom7",
  "Sus2", "Sus4", "Dim", "Aug", "Add9"
};

// ==== Voice Structure ====
struct Voice {
  bool active;
  uint8_t note;
  uint8_t velocity;
  float phase;
  float frequency;
  float amplitude;
  uint32_t startTime;

  // ADSR envelope
  enum EnvStage { ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE, ENV_OFF };
  EnvStage envStage;
  float envLevel;
};

// ==== Synth State ====
static Voice voices[MAX_VOICES];
static SynthEngine currentEngine = ENGINE_VIRTUAL_ANALOG;
static ChordType currentChordType = CHORD_MAJOR;
static int8_t rootNote = 60; // Middle C
static bool bassEnabled = false;
static uint8_t bassNote = 0;

// Effect parameters
static float delayMix = 0.0f;
static float reverbMix = 0.0f;
static float chorusMix = 0.0f;

// ==== UI Geometry ====
struct Rect { int x, y, w, h; };
struct PianoKey { Rect r; uint8_t note; bool isBlack; };

static PianoKey pianoKeys[13]; // One octave (C to C)
static Rect chordTypeButtons[CHORD_COUNT];
static Rect engineButtons[3];
static Rect effectSliders[3];

// ==== Audio Generation ====

float noteToFreq(uint8_t note) {
  return 440.0f * powf(2.0f, (note - 69) / 12.0f);
}

void initVoice(Voice& v, uint8_t note, uint8_t velocity) {
  v.active = true;
  v.note = note;
  v.velocity = velocity;
  v.phase = 0.0f;
  v.frequency = noteToFreq(note);
  v.amplitude = velocity / 127.0f;
  v.startTime = millis();
  v.envStage = Voice::ENV_ATTACK;
  v.envLevel = 0.0f;
}

Voice* findFreeVoice() {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].active) return &voices[i];
  }
  // Voice stealing: find oldest voice
  Voice* oldest = &voices[0];
  for (int i = 1; i < MAX_VOICES; i++) {
    if (voices[i].startTime < oldest->startTime) {
      oldest = &voices[i];
    }
  }
  return oldest;
}

void noteOn(uint8_t note, uint8_t velocity) {
  Voice* v = findFreeVoice();
  initVoice(*v, note, velocity);
}

void noteOff(uint8_t note) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].active && voices[i].note == note) {
      voices[i].envStage = Voice::ENV_RELEASE;
    }
  }
}

void allNotesOff() {
  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i].active = false;
  }
}

// Simple virtual analog oscillator (sawtooth)
float generateVirtualAnalog(Voice& v) {
  float sample = 2.0f * (v.phase - 0.5f); // Sawtooth
  v.phase += v.frequency / SAMPLE_RATE;
  if (v.phase >= 1.0f) v.phase -= 1.0f;
  return sample;
}

// Simple FM synthesis (2-operator)
float generateFM(Voice& v) {
  static float modPhase = 0.0f;
  float modFreq = v.frequency * 2.0f;
  float modIndex = 2.0f;

  float modulator = sinf(2.0f * PI * modPhase);
  float sample = sinf(2.0f * PI * (v.phase + modIndex * modulator));

  v.phase += v.frequency / SAMPLE_RATE;
  modPhase += modFreq / SAMPLE_RATE;

  if (v.phase >= 1.0f) v.phase -= 1.0f;
  if (modPhase >= 1.0f) modPhase -= 1.0f;

  return sample;
}

// Simple lead/piano (sine with harmonics)
float generateLeadPiano(Voice& v) {
  float fundamental = sinf(2.0f * PI * v.phase);
  float harmonic2 = 0.3f * sinf(4.0f * PI * v.phase);
  float harmonic3 = 0.15f * sinf(6.0f * PI * v.phase);

  v.phase += v.frequency / SAMPLE_RATE;
  if (v.phase >= 1.0f) v.phase -= 1.0f;

  return fundamental + harmonic2 + harmonic3;
}

// Simple ADSR envelope
void updateEnvelope(Voice& v, float dt) {
  const float attackTime = 0.01f;  // 10ms
  const float decayTime = 0.1f;    // 100ms
  const float sustainLevel = 0.7f;
  const float releaseTime = 0.2f;  // 200ms

  switch (v.envStage) {
    case Voice::ENV_ATTACK:
      v.envLevel += dt / attackTime;
      if (v.envLevel >= 1.0f) {
        v.envLevel = 1.0f;
        v.envStage = Voice::ENV_DECAY;
      }
      break;

    case Voice::ENV_DECAY:
      v.envLevel -= dt * (1.0f - sustainLevel) / decayTime;
      if (v.envLevel <= sustainLevel) {
        v.envLevel = sustainLevel;
        v.envStage = Voice::ENV_SUSTAIN;
      }
      break;

    case Voice::ENV_SUSTAIN:
      v.envLevel = sustainLevel;
      break;

    case Voice::ENV_RELEASE:
      v.envLevel -= dt / releaseTime;
      if (v.envLevel <= 0.0f) {
        v.envLevel = 0.0f;
        v.active = false;
        v.envStage = Voice::ENV_OFF;
      }
      break;

    case Voice::ENV_OFF:
      v.active = false;
      break;
  }
}

float generateSample(Voice& v) {
  float sample = 0.0f;

  switch (currentEngine) {
    case ENGINE_VIRTUAL_ANALOG:
      sample = generateVirtualAnalog(v);
      break;
    case ENGINE_FM:
      sample = generateFM(v);
      break;
    case ENGINE_LEAD_PIANO:
      sample = generateLeadPiano(v);
      break;
  }

  return sample * v.amplitude * v.envLevel;
}

// ==== Chord Generation ====

void playChord(uint8_t root, ChordType type, uint8_t velocity) {
  allNotesOff();

  // Play chord notes
  for (int i = 0; i < 6; i++) {
    int8_t interval = chordIntervals[type][i];
    if (interval < 0) break;

    uint8_t note = root + interval;
    if (note > 0 && note < 128) {
      noteOn(note, velocity);
    }
  }

  // Play bass note if enabled
  if (bassEnabled && root >= 24) {
    bassNote = root - 12; // One octave down
    noteOn(bassNote, velocity);
  }
}

void stopChord() {
  allNotesOff();
}

// ==== UI Drawing ====

void drawPianoKeyboard() {
  const int keyY = 250;
  const int whiteKeyW = 90;
  const int whiteKeyH = 200;
  const int blackKeyW = 60;
  const int blackKeyH = 120;

  int startX = 50;
  int whiteKeyIndex = 0;

  // Note pattern: C, C#, D, D#, E, F, F#, G, G#, A, A#, B, C
  const bool blackKeyPattern[] = {false, true, false, true, false, false, true, false, true, false, true, false, false};
  const uint8_t noteOffsets[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};

  // Draw white keys first
  for (int i = 0; i < 13; i++) {
    if (!blackKeyPattern[i]) {
      int x = startX + whiteKeyIndex * whiteKeyW;
      M5.Lcd.fillRect(x, keyY, whiteKeyW - 2, whiteKeyH, COL_WHITEKEY);
      M5.Lcd.drawRect(x, keyY, whiteKeyW - 2, whiteKeyH, TFT_DARKGREY);

      pianoKeys[i].r = {x, keyY, whiteKeyW - 2, whiteKeyH};
      pianoKeys[i].note = 60 + noteOffsets[i]; // Middle C octave
      pianoKeys[i].isBlack = false;

      whiteKeyIndex++;
    }
  }

  // Draw black keys on top
  whiteKeyIndex = 0;
  for (int i = 0; i < 13; i++) {
    if (blackKeyPattern[i]) {
      int x = startX + whiteKeyIndex * whiteKeyW - blackKeyW / 2;
      M5.Lcd.fillRect(x, keyY, blackKeyW, blackKeyH, COL_BLACKKEY);
      M5.Lcd.drawRect(x, keyY, blackKeyW, blackKeyH, TFT_BLACK);

      pianoKeys[i].r = {x, keyY, blackKeyW, blackKeyH};
      pianoKeys[i].note = 60 + noteOffsets[i];
      pianoKeys[i].isBlack = true;
    } else {
      whiteKeyIndex++;
    }
  }
}

void drawChordTypeButtons() {
  const int btnW = 120;
  const int btnH = 60;
  const int spacing = 10;
  const int startX = 50;
  const int startY = 480;

  for (int i = 0; i < CHORD_COUNT; i++) {
    int col = i % 5;
    int row = i / 5;
    int x = startX + col * (btnW + spacing);
    int y = startY + row * (btnH + spacing);

    chordTypeButtons[i] = {x, y, btnW, btnH};

    uint16_t color = (currentChordType == i) ? COL_BTN_ACTIVE : COL_BTN;
    M5.Lcd.fillRoundRect(x, y, btnW, btnH, 8, color);
    M5.Lcd.drawRoundRect(x, y, btnW, btnH, 8, TFT_WHITE);

    M5.Lcd.setFont(FONT_SMALL);
    M5.Lcd.setTextColor(COL_BTN_TXT);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.drawString(chordNames[i], x + btnW / 2, y + btnH / 2);
  }
}

void drawEngineSelector() {
  const char* engineNames[] = {"Analog", "FM", "Piano"};
  const int btnW = 150;
  const int btnH = 50;
  const int spacing = 10;
  const int startX = 50;
  const int startY = 30;

  M5.Lcd.setFont(FONT_TITLE);
  M5.Lcd.setTextColor(COL_ACCENT);
  M5.Lcd.setTextDatum(top_left);
  M5.Lcd.drawString("Orchid Synth", startX, startY);

  for (int i = 0; i < 3; i++) {
    int x = startX + i * (btnW + spacing);
    int y = startY + 60;

    engineButtons[i] = {x, y, btnW, btnH};

    uint16_t color = (currentEngine == i) ? COL_BTN_ACTIVE : COL_BTN;
    M5.Lcd.fillRoundRect(x, y, btnW, btnH, 8, color);
    M5.Lcd.drawRoundRect(x, y, btnW, btnH, 8, TFT_WHITE);

    M5.Lcd.setFont(FONT_MED);
    M5.Lcd.setTextColor(COL_BTN_TXT);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.drawString(engineNames[i], x + btnW / 2, y + btnH / 2);
  }
}

void drawEffectsPanel() {
  const char* effectNames[] = {"Delay", "Reverb", "Chorus"};
  const int sliderX = 800;
  const int sliderY = 30;
  const int sliderW = 400;
  const int sliderH = 40;
  const int spacing = 60;

  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(COL_ACCENT);
  M5.Lcd.setTextDatum(top_left);
  M5.Lcd.drawString("Effects", sliderX, sliderY);

  float* effectValues[] = {&delayMix, &reverbMix, &chorusMix};

  for (int i = 0; i < 3; i++) {
    int y = sliderY + 50 + i * spacing;

    effectSliders[i] = {sliderX, y, sliderW, sliderH};

    // Draw slider background
    M5.Lcd.fillRoundRect(sliderX, y, sliderW, sliderH, 8, COL_PANEL);
    M5.Lcd.drawRoundRect(sliderX, y, sliderW, sliderH, 8, TFT_WHITE);

    // Draw fill
    int fillW = (int)(sliderW * (*effectValues[i]));
    M5.Lcd.fillRoundRect(sliderX, y, fillW, sliderH, 8, COL_ACCENT);

    // Draw label
    M5.Lcd.setFont(FONT_SMALL);
    M5.Lcd.setTextColor(COL_BTN_TXT);
    M5.Lcd.setTextDatum(middle_left);
    M5.Lcd.drawString(effectNames[i], sliderX + 10, y + sliderH / 2);

    // Draw value
    char valStr[16];
    sprintf(valStr, "%.0f%%", (*effectValues[i]) * 100);
    M5.Lcd.setTextDatum(middle_right);
    M5.Lcd.drawString(valStr, sliderX + sliderW - 10, y + sliderH / 2);
  }
}

void drawBassToggle() {
  const int btnX = 800;
  const int btnY = 250;
  const int btnW = 180;
  const int btnH = 80;

  uint16_t color = bassEnabled ? COL_BTN_ACTIVE : COL_BTN;
  M5.Lcd.fillRoundRect(btnX, btnY, btnW, btnH, 8, color);
  M5.Lcd.drawRoundRect(btnX, btnY, btnW, btnH, 8, TFT_WHITE);

  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(COL_BTN_TXT);
  M5.Lcd.setTextDatum(middle_center);
  M5.Lcd.drawString("BASS", btnX + btnW / 2, btnY + btnH / 2);
}

void drawUI() {
  M5.Lcd.fillScreen(COL_BG);

  drawEngineSelector();
  drawEffectsPanel();
  drawPianoKeyboard();
  drawChordTypeButtons();
  drawBassToggle();
}

// ==== Touch Handling ====

bool touchInRect(int tx, int ty, const Rect& r) {
  return (tx >= r.x && tx < r.x + r.w && ty >= r.y && ty < r.y + r.h);
}

void handleTouch(int tx, int ty) {
  // Check piano keys
  for (int i = 12; i >= 0; i--) { // Check black keys first (drawn on top)
    if (pianoKeys[i].isBlack && touchInRect(tx, ty, pianoKeys[i].r)) {
      rootNote = pianoKeys[i].note;
      playChord(rootNote, currentChordType, 100);
      return;
    }
  }
  for (int i = 0; i < 13; i++) {
    if (!pianoKeys[i].isBlack && touchInRect(tx, ty, pianoKeys[i].r)) {
      rootNote = pianoKeys[i].note;
      playChord(rootNote, currentChordType, 100);
      return;
    }
  }

  // Check chord type buttons
  for (int i = 0; i < CHORD_COUNT; i++) {
    if (touchInRect(tx, ty, chordTypeButtons[i])) {
      currentChordType = (ChordType)i;
      drawChordTypeButtons();
      return;
    }
  }

  // Check engine buttons
  for (int i = 0; i < 3; i++) {
    if (touchInRect(tx, ty, engineButtons[i])) {
      currentEngine = (SynthEngine)i;
      drawEngineSelector();
      return;
    }
  }

  // Check bass toggle
  if (touchInRect(tx, ty, {800, 250, 180, 80})) {
    bassEnabled = !bassEnabled;
    drawBassToggle();
    return;
  }

  // Check effect sliders (simple on/off for now)
  for (int i = 0; i < 3; i++) {
    if (touchInRect(tx, ty, effectSliders[i])) {
      float* effectValue = (i == 0) ? &delayMix : (i == 1) ? &reverbMix : &chorusMix;
      float relativeX = (tx - effectSliders[i].x) / (float)effectSliders[i].w;
      *effectValue = constrain(relativeX, 0.0f, 1.0f);
      drawEffectsPanel();
      return;
    }
  }
}

void handleTouchRelease() {
  stopChord();
}

// ==== Setup ====

void setup() {
  M5.begin();
  M5.Lcd.setRotation(1); // Landscape
  M5.Lcd.fillScreen(COL_BG);

  // Initialize voices
  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i].active = false;
  }

  // Initialize speaker
  auto cfg = M5.Speaker.config();
  cfg.sample_rate = SAMPLE_RATE;
  cfg.task_pinned_core = 0;
  M5.Speaker.config(cfg);
  M5.Speaker.begin();

  drawUI();
}

// ==== Loop ====

void loop() {
  M5.update();

  // Handle touch
  static bool wasTouched = false;
  auto t = M5.Touch.getDetail();

  if (t.wasPressed()) {
    handleTouch(t.x, t.y);
    wasTouched = true;
  } else if (t.wasReleased() && wasTouched) {
    handleTouchRelease();
    wasTouched = false;
  }

  // Generate audio samples
  static int16_t audioBuffer[BUFFER_SIZE * 2]; // Stereo

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float mixL = 0.0f;
    float mixR = 0.0f;

    // Mix all active voices
    for (int v = 0; v < MAX_VOICES; v++) {
      if (voices[v].active) {
        updateEnvelope(voices[v], 1.0f / SAMPLE_RATE);
        float sample = generateSample(voices[v]);
        mixL += sample;
        mixR += sample;
      }
    }

    // Normalize and convert to int16
    mixL = constrain(mixL * 0.3f, -1.0f, 1.0f);
    mixR = constrain(mixR * 0.3f, -1.0f, 1.0f);

    audioBuffer[i * 2] = (int16_t)(mixL * 32767);
    audioBuffer[i * 2 + 1] = (int16_t)(mixR * 32767);
  }

  // Play audio
  M5.Speaker.playRaw(audioBuffer, BUFFER_SIZE * 2, SAMPLE_RATE, true, 1, 0);

  delay(1);
}
