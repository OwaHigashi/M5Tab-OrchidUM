// M5Tab-Orchid
// M5Stack Tab5 Chord Controller inspired by Telepathic Instruments Orchid
// Board : M5Stack Tab5 (ESP32-P4)
// Libs  : M5Unified, M5GFX
//
// Audio output: external M5 Unit MIDI (SAM2695) on PortA UART.
// The Tab5 is a controller — it does NOT synthesize audio internally.
//
// Chord system follows the real Orchid manual:
//   Top row    — chord BASE (radio): Major / Minor / Sus / Dim
//   Bottom row — chord MODIFIERS (toggle, combinable): 6 / M7 / m7 / 9
//   Sus uses explicit "m7"; Major/Minor/Dim use bare "7".
//   Special override labels:
//     Minor + M7+m7+9        → "CmJAZZ"
//     Sus   + 6+m7+9         → "CsusJAZZ"
//     Dim   + 6+m7+9         → "CdimJAZZ"
//     Dim   + M7+m7+6+9      → "CdimWTF"
//
// Performance Mode (Orchid manual): one of 7, cycled by the PERF button.
//   Strum / Strum 2oct / Slop / Arp / Arp 2oct / Pattern / Harp
//   The Perf slider gives the per-mode parameter (strum/arp rate, etc.) and
//   stays where it is set — it does NOT spring back.
//
// MIDI routing:
//   Chord notes → Ch 1 with current GM program
//   Bass note   → Ch 2, GM 38 (Synth Bass 1)
//   Reverb / Chorus → CC 91 / CC 93 on Ch 1 & Ch 2
//   Bend slider     → 14-bit MIDI Pitch Bend; springs back to centre.

#include <M5Unified.h>
#include <math.h>

// ==== Tab5 PortA UART for M5 Unit MIDI ====
#define RXD2 54
#define TXD2 53
#define MIDI_BAUD 31250

// ==== MIDI Channels (zero-based) ====
static constexpr uint8_t MIDI_CH_CHORD = 0;
static constexpr uint8_t MIDI_CH_BASS  = 1;

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
static constexpr uint16_t COL_PANEL      = 0x10A2;
static constexpr uint16_t COL_BTN        = 0x2945;
static constexpr uint16_t COL_BTN_ACTIVE = TFT_GREEN;
static constexpr uint16_t COL_BTN_TXT    = TFT_WHITE;
static constexpr uint16_t COL_WHITEKEY   = TFT_WHITE;
static constexpr uint16_t COL_BLACKKEY   = 0x2104;
static constexpr uint16_t COL_ACCENT     = TFT_CYAN;
static constexpr uint16_t COL_VALUE      = TFT_YELLOW;
static constexpr uint16_t COL_MUTED      = 0x7BEF;

// ==== GM Program names ====
static const char* const kGmInstrumentNames[128] = {
  "Grand Piano 1", "Bright Piano 2", "El Grd Piano 3", "Honky-Tonk Piano",
  "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavi",
  "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
  "Marimba", "Xylophone", "Tubular Bells", "Santur",
  "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
  "Reed Organ", "Accordion French", "Harmonica", "Tango Accordion",
  "Ac Guitar Nylon", "Ac Guitar Steel", "Ac Guitar Jazz", "Ac Guitar Clean",
  "Ac Guitar Muted", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
  "Acoustic Bass", "Finger Bass", "Picked Bass", "Fretless Bass",
  "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
  "Violin", "Viola", "Cello", "Contrabass",
  "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani",
  "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2",
  "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
  "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
  "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
  "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax",
  "Oboe", "English Horn", "Bassoon", "Clarinet",
  "Piccolo", "Flute", "Recorder", "Pan Flute",
  "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
  "Lead 1 Square", "Lead 2 Sawtooth", "Lead 3 Calliope", "Lead 4 Chiff",
  "Lead 5 Charang", "Lead 6 Voice", "Lead 7 Fifths", "Lead 8 Bass+Lead",
  "Pad 1 Fantasia", "Pad 2 Warm", "Pad 3 PolySynth", "Pad 4 Choir",
  "Pad 5 Bowed", "Pad 6 Metallic", "Pad 7 Halo", "Pad 8 Sweep",
  "FX 1 Rain", "FX 2 Soundtrack", "FX 3 Crystal", "FX 4 Atmosphere",
  "FX 5 Brightness", "FX 6 Goblins", "FX 7 Echoes", "FX 8 Sci-Fi",
  "Sitar", "Banjo", "Shamisen", "Koto",
  "Kalimba", "Bag Pipe", "Fiddle", "Shanai",
  "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock",
  "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
  "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
  "Telephone Ring", "Helicopter", "Applause", "Gunshot"
};

static const uint8_t BASS_PROGRAM = 38;

// ==== Chord base (Top Keys) ====
enum ChordBase {
  BASE_MAJOR, BASE_MINOR, BASE_SUS, BASE_DIM, BASE_COUNT
};
static const int8_t baseIntervals[BASE_COUNT][3] = {
  {0, 4, 7}, {0, 3, 7}, {0, 5, 7}, {0, 3, 6}
};
static const char* const baseLabels[BASE_COUNT] = {"Major", "Minor", "Sus", "Dim"};
static const char* const baseSuffix[BASE_COUNT] = {"", "m", "sus", "dim"};

// ==== Chord modifiers (Bottom Keys) ====
static constexpr uint8_t MOD_6  = 0x01;
static constexpr uint8_t MOD_M7 = 0x02;
static constexpr uint8_t MOD_m7 = 0x04;
static constexpr uint8_t MOD_9  = 0x08;
static const uint8_t modFlags[4]      = {MOD_6, MOD_M7, MOD_m7, MOD_9};
static const int8_t  modIntervals[4]  = {9, 11, 10, 14};
static const char* const modLabels[4] = {"6", "M7", "m7", "9"};

// ==== Performance Mode ====
enum PerfMode {
  PERF_STRUM,
  PERF_STRUM_2OCT,
  PERF_SLOP,
  PERF_ARP,
  PERF_ARP_2OCT,
  PERF_PATTERN,
  PERF_HARP,
  PERF_COUNT
};
static const char* const perfModeNames[PERF_COUNT] = {
  "Strum", "Strum 2oct", "Slop", "Arp", "Arp 2oct", "Pattern", "Harp"
};

// ==== State ====
static uint8_t   currentProgram = 0;
static ChordBase currentBase    = BASE_MAJOR;
static uint8_t   currentMods    = 0;
static int8_t    rootNote       = 60;
static bool      bassEnabled    = false;
static PerfMode  currentPerfMode = PERF_STRUM;
static uint16_t  bpm            = 120;

// Maximum number of notes we may have on the wire at once (4-octave Harp
// expansion can produce up to 16 notes from a 4-note chord).
static constexpr int MAX_NOTES = 20;

// Active MIDI notes (non-arp; strum appends to this as it emits)
static uint8_t activeChordNotes[MAX_NOTES];
static uint8_t activeChordCount = 0;

// Bass voice
static uint8_t activeBassNote = 0;
static bool    bassNoteActive = false;

// Strum queue — drives Strum, Strum 2oct, Slop, and Harp one-shots.
static uint8_t  strumNotes[MAX_NOTES];
static uint8_t  strumCount     = 0;
static uint8_t  strumIndex     = 0;
static uint32_t strumLastEmitMs= 0;
static bool     strumActive    = false;
static PerfMode strumMode      = PERF_STRUM;  // mode that started the current strum

// Arpeggiator loop — drives Arp, Arp 2oct, and Pattern.
static uint8_t  arpNotes[MAX_NOTES];
static uint8_t  arpNoteCount   = 0;
static uint8_t  arpIndex       = 0;
static uint8_t  arpCurrentNote = 0;
static bool     arpNoteActive  = false;
static uint32_t arpLastTickMs  = 0;
static PerfMode arpMode        = PERF_ARP;

// Pattern mode — a fixed step sequence over chord-note indices (Orchid's
// "Pattern" mode plays the chord notes in a non-monotonic rhythm rather than
// the straight up sweep of the arpeggiator).
static const uint8_t patternSeq[] = {0, 2, 1, 2, 0, 3, 2, 1};
static constexpr int PATTERN_LEN = sizeof(patternSeq);
static int patternStep = 0;

// Effect / performance sliders
static float perfParam = 0.5f;  // mode-dependent speed/depth; doesn't spring back
static float bendValue = 0.5f;  // 14-bit pitch bend; springs back to centre
static float reverbMix = 0.2f;
static float chorusMix = 0.0f;

// Preset
static uint8_t currentPreset = 0;
struct Preset {
  uint8_t program;
  float   reverb;
  float   chorus;
  bool    bass;
  const char* name;
};
static const Preset presets[] = {
  {  0, 0.2f, 0.0f, false, "Grand Piano" },
  {  0, 0.6f, 0.2f, true,  "Piano Hall"  },
  { 81, 0.3f, 0.1f, false, "Saw Lead"    },
  { 88, 0.5f, 0.3f, true,  "Fantasia Pad"},
  { 48, 0.4f, 0.2f, false, "Strings"     },
};
static constexpr int PRESET_COUNT = sizeof(presets) / sizeof(Preset);

// ==== Geometry ====
struct Rect { int x, y, w, h; };
struct PianoKey { Rect r; uint8_t note; bool isBlack; };

static constexpr int CHORD_PAD_X   = 14;
static constexpr int CHORD_PAD_W   = 610;
static constexpr int BASE_BTN_Y    = 20;
static constexpr int BASE_BTN_H    = 150;
static constexpr int MOD_BTN_Y     = 190;
static constexpr int MOD_BTN_H     = 150;
static constexpr int LEFT_EFX_Y    = 360;
static constexpr int EFX_SLIDER_H  = 70;
static constexpr int EFX_GAP       = 8;
static constexpr int EFX_COUNT     = 4;

static constexpr int RIGHT_X       = 640;
static constexpr int RIGHT_W       = 640;

// Chord-name area is split between the chord label and the perf-mode label.
static constexpr int CHORD_NAME_Y  = 230;
static constexpr int CHORD_NAME_H  = 96;
static constexpr int CHORD_NAME_X  = 650;
static constexpr int CHORD_NAME_W  = 380;
static constexpr int PERF_LBL_X    = 1040;
static constexpr int PERF_LBL_W    = 240;

static constexpr int PIANO_Y       = 334;
static constexpr int PIANO_WHITE_W = 79;
static constexpr int PIANO_WHITE_H = 380;
static constexpr int PIANO_BLACK_W = 50;
static constexpr int PIANO_BLACK_H = 240;

static PianoKey pianoKeys[13];
static Rect chordBaseButtons[BASE_COUNT];
static Rect chordModButtons[4];
static Rect effectSliders[EFX_COUNT];

// Right-column header
static Rect bpmDownBtn        = { 650,  14,  70, 64};
static Rect bpmDisplayRect    = { 730,  14, 200, 64};
static Rect bpmUpBtn          = { 940,  14,  70, 64};
static Rect presetDisplayRect = {1020,  14, 260, 64};
static Rect prgDownBtn        = { 650,  86,  80, 64};
static Rect prgDisplayRect    = { 740,  86, 460, 64};
static Rect prgUpBtn          = {1210,  86,  70, 64};
static Rect bassToggleRect    = { 650, 158, 210, 64};
static Rect perfToggleRect    = { 870, 158, 410, 64};  // wide so the mode name fits

// ==== Forward declarations ====
void drawUI();
void drawHeader();
void drawHeaderToggles();
void drawChordPad();
void drawChordNameDisplay();
void drawEffectsPanel();
void drawPianoKeyboard();
static void drawSlider(int idx, const char* label, float value, bool bipolar);

// ==== MIDI OUT helpers ====
static inline void midiWrite(uint8_t b) { Serial2.write(b); }

void sendNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
  midiWrite(0x90 | (ch & 0x0F));
  midiWrite(note & 0x7F);
  midiWrite(vel & 0x7F);
}
void sendNoteOff(uint8_t ch, uint8_t note) {
  midiWrite(0x80 | (ch & 0x0F));
  midiWrite(note & 0x7F);
  midiWrite(0);
}
void sendProgramChange(uint8_t ch, uint8_t program) {
  midiWrite(0xC0 | (ch & 0x0F));
  midiWrite(program & 0x7F);
}
void sendControlChange(uint8_t ch, uint8_t cc, uint8_t val) {
  midiWrite(0xB0 | (ch & 0x0F));
  midiWrite(cc & 0x7F);
  midiWrite(val & 0x7F);
}
void sendPitchBend(uint8_t ch, uint16_t bend14) {
  if (bend14 > 16383) bend14 = 16383;
  midiWrite(0xE0 | (ch & 0x0F));
  midiWrite((uint8_t)(bend14 & 0x7F));
  midiWrite((uint8_t)((bend14 >> 7) & 0x7F));
}
void sendAllNotesOff(uint8_t ch) { sendControlChange(ch, 123, 0); }

void applyChordProgram() {
  sendProgramChange(MIDI_CH_CHORD, currentProgram);
  sendProgramChange(MIDI_CH_BASS,  BASS_PROGRAM);
}
void applyMixEffects() {
  uint8_t r = (uint8_t)(reverbMix * 127.0f);
  uint8_t c = (uint8_t)(chorusMix * 127.0f);
  for (uint8_t ch : {MIDI_CH_CHORD, MIDI_CH_BASS}) {
    sendControlChange(ch, 91, r);
    sendControlChange(ch, 93, c);
  }
}
void applyPitchBend() {
  uint16_t b = (uint16_t)(bendValue * 16383.0f);
  sendPitchBend(MIDI_CH_CHORD, b);
  sendPitchBend(MIDI_CH_BASS,  b);
}

// ==== Chord building ====

static void sortAscending(uint8_t* a, int n) {
  for (int i = 1; i < n; i++) {
    uint8_t v = a[i];
    int j = i - 1;
    while (j >= 0 && a[j] > v) { a[j + 1] = a[j]; j--; }
    a[j + 1] = v;
  }
}

// Base chord (Top + Bottom keys), sorted ascending. Up to ~7 notes.
int buildBaseChordNotes(uint8_t root, uint8_t* out) {
  int n = 0;
  for (int i = 0; i < 3 && n < MAX_NOTES; i++) {
    int v = (int)root + baseIntervals[currentBase][i];
    if (v >= 0 && v < 128) out[n++] = (uint8_t)v;
  }
  for (int i = 0; i < 4 && n < MAX_NOTES; i++) {
    if (currentMods & modFlags[i]) {
      int v = (int)root + modIntervals[i];
      if (v >= 0 && v < 128) out[n++] = (uint8_t)v;
    }
  }
  sortAscending(out, n);
  return n;
}

// Duplicate the chord +12 semitones. Used by Strum 2 oct and Arp 2 oct.
int expandTwoOctaves(uint8_t* notes, int n) {
  int orig = n;
  for (int i = 0; i < orig && n < MAX_NOTES; i++) {
    int v = (int)notes[i] + 12;
    if (v < 128) notes[n++] = (uint8_t)v;
  }
  sortAscending(notes, n);
  return n;
}

// Spread the chord across four octaves for the Harp glissando: -1, 0, +1, +2
// relative to the base chord. Telepathic describe Harp as "a harp being
// strummed to the sound of a given chord across a four-octave range".
int expandHarp(uint8_t* notes, int n) {
  if (n == 0) return 0;
  uint8_t base[MAX_NOTES];
  int baseN = n;
  for (int i = 0; i < baseN; i++) base[i] = notes[i];

  int outN = 0;
  for (int oct = -1; oct <= 2 && outN < MAX_NOTES; oct++) {
    for (int i = 0; i < baseN && outN < MAX_NOTES; i++) {
      int v = (int)base[i] + oct * 12;
      if (v >= 0 && v < 128) notes[outN++] = (uint8_t)v;
    }
  }
  sortAscending(notes, outN);
  return outN;
}

void buildChordName(char* out, size_t cap) {
  static const char* const noteNames[12] =
    {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  const char* rn = noteNames[((rootNote % 12) + 12) % 12];

  if (currentBase == BASE_MINOR && currentMods == (MOD_M7 | MOD_m7 | MOD_9)) {
    snprintf(out, cap, "%smJAZZ", rn); return;
  }
  if (currentBase == BASE_SUS && currentMods == (MOD_6 | MOD_m7 | MOD_9)) {
    snprintf(out, cap, "%ssusJAZZ", rn); return;
  }
  if (currentBase == BASE_DIM && currentMods == (MOD_6 | MOD_m7 | MOD_9)) {
    snprintf(out, cap, "%sdimJAZZ", rn); return;
  }
  if (currentBase == BASE_DIM && currentMods == (MOD_M7 | MOD_m7 | MOD_6 | MOD_9)) {
    snprintf(out, cap, "%sdimWTF", rn); return;
  }

  char suffix[16];
  int p = 0;
  if (currentMods & MOD_M7) { suffix[p++] = 'M'; suffix[p++] = '7'; }
  if (currentMods & MOD_m7) {
    if (currentBase == BASE_SUS) { suffix[p++] = 'm'; suffix[p++] = '7'; }
    else                          { suffix[p++] = '7'; }
  }
  if (currentMods & MOD_6) suffix[p++] = '6';
  if (currentMods & MOD_9) suffix[p++] = '9';
  suffix[p] = 0;

  snprintf(out, cap, "%s%s%s", rn, baseSuffix[currentBase], suffix);
}

// ==== Playback ====

void stopChord() {
  if (arpNoteActive) {
    sendNoteOff(MIDI_CH_CHORD, arpCurrentNote);
    arpNoteActive = false;
  }
  for (uint8_t i = 0; i < activeChordCount; i++) {
    sendNoteOff(MIDI_CH_CHORD, activeChordNotes[i]);
  }
  activeChordCount = 0;

  arpNoteCount = 0;
  arpIndex = 0;
  strumActive = false;
  strumCount = strumIndex = 0;

  if (bassNoteActive) {
    sendNoteOff(MIDI_CH_BASS, activeBassNote);
    bassNoteActive = false;
  }
}

bool chordIsSounding() {
  return activeChordCount > 0 || arpNoteActive || strumActive;
}

// Helper to choose an initial velocity per mode (Slop randomises).
static inline uint8_t pickStrumVelocity(uint8_t base) {
  if (strumMode == PERF_SLOP) return (uint8_t)random(70, 121);
  return base;
}

// Stash `notes` into the strum queue and emit the first note immediately.
// strumMode records which performance mode owns this strum so the tick code
// can apply per-mode timing / jitter.
static void startStrum(const uint8_t* notes, int n, uint8_t velocity, PerfMode mode) {
  strumMode = mode;
  strumCount = (uint8_t)((n < MAX_NOTES) ? n : MAX_NOTES);
  for (uint8_t i = 0; i < strumCount; i++) strumNotes[i] = notes[i];
  strumIndex = 0;
  if (strumCount > 0) {
    uint8_t v = pickStrumVelocity(velocity);
    sendNoteOn(MIDI_CH_CHORD, strumNotes[0], v);
    activeChordNotes[activeChordCount++] = strumNotes[0];
    strumIndex = 1;
    strumLastEmitMs = millis();
  }
  strumActive = (strumIndex < strumCount);
}

static void startArp(const uint8_t* notes, int n, uint8_t velocity, PerfMode mode) {
  arpMode = mode;
  arpNoteCount = (uint8_t)((n < MAX_NOTES) ? n : MAX_NOTES);
  for (uint8_t i = 0; i < arpNoteCount; i++) arpNotes[i] = notes[i];
  arpIndex = 0;
  patternStep = 0;
  uint8_t first;
  if (arpMode == PERF_PATTERN && arpNoteCount > 0) {
    first = arpNotes[patternSeq[0] % arpNoteCount];
  } else {
    first = arpNotes[0];
  }
  arpCurrentNote = first;
  sendNoteOn(MIDI_CH_CHORD, arpCurrentNote, velocity);
  arpNoteActive = true;
  arpLastTickMs = millis();
}

void playChord(uint8_t root, uint8_t velocity) {
  stopChord();

  uint8_t notes[MAX_NOTES];
  int n = buildBaseChordNotes(root, notes);
  if (n <= 0) return;

  switch (currentPerfMode) {
    case PERF_STRUM:
      startStrum(notes, n, velocity, PERF_STRUM);
      break;
    case PERF_STRUM_2OCT:
      n = expandTwoOctaves(notes, n);
      startStrum(notes, n, velocity, PERF_STRUM_2OCT);
      break;
    case PERF_SLOP:
      startStrum(notes, n, velocity, PERF_SLOP);
      break;
    case PERF_HARP:
      n = expandHarp(notes, n);
      startStrum(notes, n, velocity, PERF_HARP);
      break;

    case PERF_ARP:
      startArp(notes, n, velocity, PERF_ARP);
      break;
    case PERF_ARP_2OCT:
      n = expandTwoOctaves(notes, n);
      startArp(notes, n, velocity, PERF_ARP_2OCT);
      break;
    case PERF_PATTERN:
      startArp(notes, n, velocity, PERF_PATTERN);
      break;

    default: {
      for (int i = 0; i < n; i++) {
        sendNoteOn(MIDI_CH_CHORD, notes[i], velocity);
        activeChordNotes[activeChordCount++] = notes[i];
      }
    }
  }

  if (bassEnabled && root >= 24) {
    activeBassNote = (uint8_t)(root - 12);
    sendNoteOn(MIDI_CH_BASS, activeBassNote, velocity);
    bassNoteActive = true;
  }
}

void retriggerIfActive() {
  if (chordIsSounding()) playChord(rootNote, 100);
}

// Per-mode emission rate for the strum queue (ms between successive notes).
// Each mode picks a different range so they're audibly distinct, then Slop
// adds jitter on top.
static uint32_t strumIntervalMs() {
  uint32_t base;
  switch (strumMode) {
    case PERF_HARP:
      // Harp glissando: very fast, 6–28 ms regardless of perfParam.
      base = (uint32_t)(28.0f - 22.0f * perfParam);
      if (base < 6) base = 6;
      break;
    case PERF_SLOP:
      // Strum-ish, but a bit slower default to leave room for jitter to swing.
      base = (uint32_t)(220.0f - 200.0f * perfParam);
      if (base < 10) base = 10;
      break;
    case PERF_STRUM_2OCT:
      // Slightly tighter than Strum so the doubled note count doesn't drag.
      base = (uint32_t)(160.0f - 150.0f * perfParam);
      if (base < 5) base = 5;
      break;
    case PERF_STRUM:
    default:
      base = (uint32_t)(200.0f - 195.0f * perfParam);
      if (base < 5) base = 5;
      break;
  }
  if (strumMode == PERF_SLOP) {
    // ±60% jitter — looser than Strum so the swing is obvious.
    int span = (int)(base * 6 / 10);
    if (span < 2) span = 2;
    int jitter = (int)random(-span, span + 1);
    int v = (int)base + jitter;
    if (v < 5) v = 5;
    base = (uint32_t)v;
  }
  return base;
}

void tickStrum() {
  if (!strumActive || strumIndex >= strumCount) { strumActive = false; return; }
  uint32_t now = millis();
  if (now - strumLastEmitMs < strumIntervalMs()) return;
  uint8_t note = strumNotes[strumIndex];
  uint8_t vel = (strumMode == PERF_SLOP) ? (uint8_t)random(70, 121) : 100;
  sendNoteOn(MIDI_CH_CHORD, note, vel);
  if (activeChordCount < MAX_NOTES) {
    activeChordNotes[activeChordCount++] = note;
  }
  strumIndex++;
  strumLastEmitMs = now;
  if (strumIndex >= strumCount) strumActive = false;
}

void tickArp() {
  if (!arpNoteActive || arpNoteCount == 0) return;
  uint32_t now = millis();
  // 16th-note from BPM, then perfParam shrinks the interval up to ~4×.
  uint32_t base16 = 60000UL / ((uint32_t)bpm * 4);
  uint32_t interval = (uint32_t)((float)base16 / (0.25f + perfParam * 1.5f));
  if (interval < 25) interval = 25;
  if (now - arpLastTickMs < interval) return;

  sendNoteOff(MIDI_CH_CHORD, arpCurrentNote);

  if (arpMode == PERF_PATTERN) {
    // Step through the fixed pattern sequence (indices into the chord notes),
    // so Pattern doesn't sound like a plain ascending arpeggio.
    patternStep = (patternStep + 1) % PATTERN_LEN;
    uint8_t idx = (uint8_t)(patternSeq[patternStep] % arpNoteCount);
    arpCurrentNote = arpNotes[idx];
  } else {
    // Straight ascending arpeggio (Arp / Arp 2 oct).
    arpIndex = (uint8_t)((arpIndex + 1) % arpNoteCount);
    arpCurrentNote = arpNotes[arpIndex];
  }

  sendNoteOn(MIDI_CH_CHORD, arpCurrentNote, 100);
  arpLastTickMs = now;
}

// ==== Header stepper helpers ====
void stepProgram(int delta) {
  int p = (int)currentProgram + delta;
  if (p < 0)   p = 0;
  if (p > 127) p = 127;
  if ((uint8_t)p == currentProgram) return;
  currentProgram = (uint8_t)p;
  applyChordProgram();
  drawHeader();
}
void stepBpm(int delta) {
  int b = (int)bpm + delta * 5;
  if (b < 40)  b = 40;
  if (b > 240) b = 240;
  bpm = (uint16_t)b;
  drawHeader();
}
void cyclePerfMode() {
  currentPerfMode = (PerfMode)((currentPerfMode + 1) % PERF_COUNT);
  drawHeaderToggles();
  drawChordNameDisplay();
  retriggerIfActive();
}

void loadPreset(uint8_t presetNum) {
  if (presetNum >= PRESET_COUNT) return;
  const Preset& p = presets[presetNum];
  currentProgram = p.program;
  reverbMix      = p.reverb;
  chorusMix      = p.chorus;
  bassEnabled    = p.bass;
  currentPreset  = presetNum;
  applyChordProgram();
  applyMixEffects();
  drawUI();
}

// ==== MIDI IN (intentionally inert) ====
void processMIDI() {
  while (Serial2.available()) (void)Serial2.read();
}

// ==== UI drawing ====

void drawPianoKeyboard() {
  const int keyY  = PIANO_Y;
  const int wW    = PIANO_WHITE_W;
  const int wH    = PIANO_WHITE_H;
  const int bW    = PIANO_BLACK_W;
  const int bH    = PIANO_BLACK_H;
  const int startX = RIGHT_X + (RIGHT_W - 8 * wW) / 2;

  const bool    blackPat[] = {false,true,false,true,false,false,true,false,true,false,true,false,false};
  const uint8_t offsets[]  = {0,1,2,3,4,5,6,7,8,9,10,11,12};

  int whiteIdx = 0;
  for (int i = 0; i < 13; i++) {
    if (!blackPat[i]) {
      int x = startX + whiteIdx * wW;
      M5.Lcd.fillRect(x, keyY, wW - 2, wH, COL_WHITEKEY);
      M5.Lcd.drawRect(x, keyY, wW - 2, wH, TFT_DARKGREY);
      pianoKeys[i].r = {x, keyY, wW - 2, wH};
      pianoKeys[i].note = 60 + offsets[i];
      pianoKeys[i].isBlack = false;
      whiteIdx++;
    }
  }
  whiteIdx = 0;
  for (int i = 0; i < 13; i++) {
    if (blackPat[i]) {
      int x = startX + whiteIdx * wW - bW / 2;
      M5.Lcd.fillRect(x, keyY, bW, bH, COL_BLACKKEY);
      M5.Lcd.drawRect(x, keyY, bW, bH, TFT_BLACK);
      pianoKeys[i].r = {x, keyY, bW, bH};
      pianoKeys[i].note = 60 + offsets[i];
      pianoKeys[i].isBlack = true;
    } else {
      whiteIdx++;
    }
  }
}

void drawChordPad() {
  const int btnW = (CHORD_PAD_W - 3 * 10) / 4;

  for (int i = 0; i < BASE_COUNT; i++) {
    int x = CHORD_PAD_X + i * (btnW + 10);
    int y = BASE_BTN_Y;
    chordBaseButtons[i] = {x, y, btnW, BASE_BTN_H};
    uint16_t color = (currentBase == i) ? COL_BTN_ACTIVE : COL_BTN;
    M5.Lcd.fillRoundRect(x, y, btnW, BASE_BTN_H, 12, color);
    M5.Lcd.drawRoundRect(x, y, btnW, BASE_BTN_H, 12, TFT_WHITE);
    M5.Lcd.setFont(FONT_LARGE);
    M5.Lcd.setTextColor(COL_BTN_TXT);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.drawString(baseLabels[i], x + btnW / 2, y + BASE_BTN_H / 2);
  }

  for (int i = 0; i < 4; i++) {
    int x = CHORD_PAD_X + i * (btnW + 10);
    int y = MOD_BTN_Y;
    chordModButtons[i] = {x, y, btnW, MOD_BTN_H};
    bool on = (currentMods & modFlags[i]) != 0;
    uint16_t color = on ? COL_BTN_ACTIVE : COL_BTN;
    M5.Lcd.fillRoundRect(x, y, btnW, MOD_BTN_H, 12, color);
    M5.Lcd.drawRoundRect(x, y, btnW, MOD_BTN_H, 12, TFT_WHITE);
    M5.Lcd.setFont(FONT_TITLE);
    M5.Lcd.setTextColor(COL_BTN_TXT);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.drawString(modLabels[i], x + btnW / 2, y + MOD_BTN_H / 2);
  }
}

void drawChordNameDisplay() {
  // Clear the whole row first so the perf label doesn't leave artefacts
  M5.Lcd.fillRect(CHORD_NAME_X, CHORD_NAME_Y, RIGHT_X + RIGHT_W - CHORD_NAME_X, CHORD_NAME_H, COL_BG);

  // Chord name panel
  M5.Lcd.fillRoundRect(CHORD_NAME_X, CHORD_NAME_Y, CHORD_NAME_W, CHORD_NAME_H, 14, COL_PANEL);
  M5.Lcd.drawRoundRect(CHORD_NAME_X, CHORD_NAME_Y, CHORD_NAME_W, CHORD_NAME_H, 14, TFT_WHITE);
  char name[24];
  buildChordName(name, sizeof(name));
  M5.Lcd.setFont(FONT_TITLE);
  M5.Lcd.setTextColor(COL_ACCENT);
  M5.Lcd.setTextDatum(middle_center);
  M5.Lcd.drawString(name, CHORD_NAME_X + CHORD_NAME_W / 2, CHORD_NAME_Y + CHORD_NAME_H / 2);

  // Perf-mode label panel (read-only; the PERF button in the toggle row cycles it)
  M5.Lcd.fillRoundRect(PERF_LBL_X, CHORD_NAME_Y, PERF_LBL_W, CHORD_NAME_H, 14, COL_PANEL);
  M5.Lcd.drawRoundRect(PERF_LBL_X, CHORD_NAME_Y, PERF_LBL_W, CHORD_NAME_H, 14, TFT_WHITE);
  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(COL_VALUE);
  M5.Lcd.setTextDatum(middle_center);
  M5.Lcd.drawString(perfModeNames[currentPerfMode],
                    PERF_LBL_X + PERF_LBL_W / 2, CHORD_NAME_Y + CHORD_NAME_H / 2);
}

static void drawSlider(int idx, const char* label, float value, bool bipolar) {
  int y = LEFT_EFX_Y + idx * (EFX_SLIDER_H + EFX_GAP);
  int x = CHORD_PAD_X;
  int w = CHORD_PAD_W;
  int h = EFX_SLIDER_H;
  effectSliders[idx] = {x, y, w, h};

  M5.Lcd.fillRoundRect(x, y, w, h, 10, COL_PANEL);
  M5.Lcd.drawRoundRect(x, y, w, h, 10, TFT_WHITE);

  if (bipolar) {
    int cx = x + w / 2;
    if (value < 0.5f) {
      int fillW = (int)((0.5f - value) * w);
      if (fillW > 0) M5.Lcd.fillRoundRect(cx - fillW, y, fillW, h, 8, COL_ACCENT);
    } else if (value > 0.5f) {
      int fillW = (int)((value - 0.5f) * w);
      if (fillW > 0) M5.Lcd.fillRoundRect(cx, y, fillW, h, 8, COL_ACCENT);
    }
    M5.Lcd.drawFastVLine(cx, y + 4, h - 8, TFT_WHITE);
  } else {
    int fillW = (int)(w * value);
    if (fillW > 0) M5.Lcd.fillRoundRect(x, y, fillW, h, 10, COL_ACCENT);
  }

  M5.Lcd.setFont(FONT_MED);
  M5.Lcd.setTextColor(COL_BTN_TXT);
  M5.Lcd.setTextDatum(middle_left);
  M5.Lcd.drawString(label, x + 14, y + h / 2);

  char valStr[16];
  if (bipolar) {
    int pct = (int)((value - 0.5f) * 200.0f);
    snprintf(valStr, sizeof(valStr), "%+d%%", pct);
  } else {
    snprintf(valStr, sizeof(valStr), "%.0f%%", value * 100.0f);
  }
  M5.Lcd.setTextDatum(middle_right);
  M5.Lcd.drawString(valStr, x + w - 14, y + h / 2);
}

void drawEffectsPanel() {
  drawSlider(0, "Perf",   perfParam, false);
  drawSlider(1, "Bend",   bendValue, true);
  drawSlider(2, "Reverb", reverbMix, false);
  drawSlider(3, "Chorus", chorusMix, false);
}

static void drawHeaderBtn(const Rect& r, bool on, const char* label) {
  M5.Lcd.fillRoundRect(r.x, r.y, r.w, r.h, 12, on ? COL_BTN_ACTIVE : COL_BTN);
  M5.Lcd.drawRoundRect(r.x, r.y, r.w, r.h, 12, TFT_WHITE);
  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(COL_BTN_TXT);
  M5.Lcd.setTextDatum(middle_center);
  M5.Lcd.drawString(label, r.x + r.w / 2, r.y + r.h / 2);
}

void drawHeaderToggles() {
  drawHeaderBtn(bassToggleRect, bassEnabled, "BASS");

  // PERF button: shows "PERF: <mode name>"
  char buf[40];
  snprintf(buf, sizeof(buf), "PERF: %s", perfModeNames[currentPerfMode]);
  drawHeaderBtn(perfToggleRect, true, buf);
}

void drawHeader() {
  M5.Lcd.fillRect(RIGHT_X, 0, RIGHT_W, CHORD_NAME_Y - 4, COL_BG);

  drawHeaderBtn(bpmDownBtn, false, "-");
  M5.Lcd.fillRoundRect(bpmDisplayRect.x, bpmDisplayRect.y, bpmDisplayRect.w, bpmDisplayRect.h, 12, COL_PANEL);
  M5.Lcd.drawRoundRect(bpmDisplayRect.x, bpmDisplayRect.y, bpmDisplayRect.w, bpmDisplayRect.h, 12, TFT_WHITE);
  char bpmBuf[16];
  snprintf(bpmBuf, sizeof(bpmBuf), "BPM %u", (unsigned)bpm);
  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(COL_VALUE);
  M5.Lcd.setTextDatum(middle_center);
  M5.Lcd.drawString(bpmBuf, bpmDisplayRect.x + bpmDisplayRect.w / 2,
                    bpmDisplayRect.y + bpmDisplayRect.h / 2);
  drawHeaderBtn(bpmUpBtn, false, "+");

  M5.Lcd.fillRoundRect(presetDisplayRect.x, presetDisplayRect.y, presetDisplayRect.w, presetDisplayRect.h, 12, COL_PANEL);
  M5.Lcd.drawRoundRect(presetDisplayRect.x, presetDisplayRect.y, presetDisplayRect.w, presetDisplayRect.h, 12, TFT_WHITE);
  if (currentPreset < PRESET_COUNT) {
    M5.Lcd.setFont(FONT_MED);
    M5.Lcd.setTextColor(COL_MUTED);
    M5.Lcd.setTextDatum(middle_center);
    M5.Lcd.drawString(presets[currentPreset].name,
                      presetDisplayRect.x + presetDisplayRect.w / 2,
                      presetDisplayRect.y + presetDisplayRect.h / 2);
  }

  drawHeaderBtn(prgDownBtn, false, "PRG-");
  M5.Lcd.fillRoundRect(prgDisplayRect.x, prgDisplayRect.y, prgDisplayRect.w, prgDisplayRect.h, 12, COL_PANEL);
  M5.Lcd.drawRoundRect(prgDisplayRect.x, prgDisplayRect.y, prgDisplayRect.w, prgDisplayRect.h, 12, TFT_WHITE);
  char prgBuf[48];
  snprintf(prgBuf, sizeof(prgBuf), "%03u  %s",
           (unsigned)(currentProgram + 1), kGmInstrumentNames[currentProgram]);
  M5.Lcd.setFont(FONT_LARGE);
  M5.Lcd.setTextColor(COL_VALUE);
  M5.Lcd.setTextDatum(middle_left);
  M5.Lcd.drawString(prgBuf, prgDisplayRect.x + 16,
                    prgDisplayRect.y + prgDisplayRect.h / 2);
  drawHeaderBtn(prgUpBtn, false, "PRG+");

  drawHeaderToggles();
}

void drawUI() {
  M5.Lcd.fillScreen(COL_BG);
  drawHeader();
  drawChordPad();
  drawChordNameDisplay();
  drawEffectsPanel();
  drawPianoKeyboard();
}

// ==== Touch dispatch ====
bool touchInRect(int tx, int ty, const Rect& r) {
  return (tx >= r.x && tx < r.x + r.w && ty >= r.y && ty < r.y + r.h);
}

bool handleTouchPress(int tx, int ty) {
  // Piano keys (black first)
  for (int i = 12; i >= 0; i--) {
    if (pianoKeys[i].isBlack && touchInRect(tx, ty, pianoKeys[i].r)) {
      rootNote = pianoKeys[i].note;
      drawChordNameDisplay();
      playChord(rootNote, 100);
      return true;
    }
  }
  for (int i = 0; i < 13; i++) {
    if (!pianoKeys[i].isBlack && touchInRect(tx, ty, pianoKeys[i].r)) {
      rootNote = pianoKeys[i].note;
      drawChordNameDisplay();
      playChord(rootNote, 100);
      return true;
    }
  }

  // Chord base (radio)
  for (int i = 0; i < BASE_COUNT; i++) {
    if (touchInRect(tx, ty, chordBaseButtons[i])) {
      currentBase = (ChordBase)i;
      drawChordPad();
      drawChordNameDisplay();
      retriggerIfActive();
      return false;
    }
  }

  // Modifiers (toggle)
  for (int i = 0; i < 4; i++) {
    if (touchInRect(tx, ty, chordModButtons[i])) {
      currentMods ^= modFlags[i];
      drawChordPad();
      drawChordNameDisplay();
      retriggerIfActive();
      return false;
    }
  }

  // Header steppers
  if (touchInRect(tx, ty, prgDownBtn)) { stepProgram(-1); return false; }
  if (touchInRect(tx, ty, prgUpBtn))   { stepProgram(+1); return false; }
  if (touchInRect(tx, ty, bpmDownBtn)) { stepBpm(-1);     return false; }
  if (touchInRect(tx, ty, bpmUpBtn))   { stepBpm(+1);     return false; }

  if (touchInRect(tx, ty, bassToggleRect)) {
    bassEnabled = !bassEnabled;
    if (!bassEnabled && bassNoteActive) {
      sendNoteOff(MIDI_CH_BASS, activeBassNote);
      bassNoteActive = false;
    } else if (bassEnabled && chordIsSounding() && rootNote >= 24 && !bassNoteActive) {
      activeBassNote = (uint8_t)(rootNote - 12);
      sendNoteOn(MIDI_CH_BASS, activeBassNote, 100);
      bassNoteActive = true;
    }
    drawHeaderToggles();
    return false;
  }
  if (touchInRect(tx, ty, perfToggleRect)) {
    cyclePerfMode();
    return false;
  }

  return false;  // sliders are handled in updateSliderDrag()
}

bool anyTouchOnPiano() {
  int n = M5.Touch.getCount();
  for (int i = 0; i < n; i++) {
    auto t = M5.Touch.getDetail(i);
    if (!t.isPressed()) continue;
    for (int k = 0; k < 13; k++) {
      if (touchInRect(t.x, t.y, pianoKeys[k].r)) return true;
    }
  }
  return false;
}

// Continuous slider tracking. Only the Bend slider (index 1) springs back.
void updateSliderDrag() {
  static bool bendWasActive = false;
  bool bendActiveNow = false;

  int n = M5.Touch.getCount();
  for (int i = 0; i < n; i++) {
    auto t = M5.Touch.getDetail(i);
    if (!t.isPressed()) continue;
    for (int s = 0; s < EFX_COUNT; s++) {
      if (!touchInRect(t.x, t.y, effectSliders[s])) continue;
      float rel = (t.x - effectSliders[s].x) / (float)effectSliders[s].w;
      if (rel < 0.0f) rel = 0.0f;
      if (rel > 1.0f) rel = 1.0f;
      switch (s) {
        case 0:  // Perf — stays where it's set
          if (fabsf(rel - perfParam) > 0.01f) {
            perfParam = rel;
            drawSlider(0, "Perf", perfParam, false);
          }
          break;
        case 1:  // Bend — bipolar, springs back
          if (fabsf(rel - bendValue) > 0.005f) {
            bendValue = rel;
            applyPitchBend();
            drawSlider(1, "Bend", bendValue, true);
          }
          bendActiveNow = true;
          break;
        case 2:  // Reverb
          if (fabsf(rel - reverbMix) > 0.01f) {
            reverbMix = rel;
            applyMixEffects();
            drawSlider(2, "Reverb", reverbMix, false);
          }
          break;
        case 3:  // Chorus
          if (fabsf(rel - chorusMix) > 0.01f) {
            chorusMix = rel;
            applyMixEffects();
            drawSlider(3, "Chorus", chorusMix, false);
          }
          break;
      }
    }
  }

  if (bendWasActive && !bendActiveNow) {
    bendValue = 0.5f;
    applyPitchBend();
    drawSlider(1, "Bend", bendValue, true);
  }
  bendWasActive = bendActiveNow;
}

// ==== Setup ====
void setup() {
  M5.begin();
  M5.Lcd.setRotation(1);
  M5.Lcd.fillScreen(COL_BG);

  Serial2.begin(MIDI_BAUD, SERIAL_8N1, RXD2, TXD2);
  randomSeed(esp_random());

  sendAllNotesOff(MIDI_CH_CHORD);
  sendAllNotesOff(MIDI_CH_BASS);
  applyChordProgram();
  applyMixEffects();
  applyPitchBend();

  drawUI();
}

// ==== Loop ====
void loop() {
  M5.update();

  processMIDI();

  int n = M5.Touch.getCount();
  for (int i = 0; i < n; i++) {
    auto t = M5.Touch.getDetail(i);
    if (t.wasPressed()) handleTouchPress(t.x, t.y);
  }

  updateSliderDrag();

  static bool pianoHeldLast = false;
  static int  noPianoFrames = 0;
  bool pianoHeld = anyTouchOnPiano();
  if (pianoHeld) {
    noPianoFrames = 0;
    pianoHeldLast = true;
  } else if (pianoHeldLast) {
    if (++noPianoFrames >= 3) {
      stopChord();
      pianoHeldLast = false;
      noPianoFrames = 0;
    }
  }

  tickStrum();
  tickArp();

  delay(1);
}
