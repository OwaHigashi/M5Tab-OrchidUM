# M5Tab-Orchid

M5Stack Tab5 Chord Synthesizer inspired by Telepathic Instruments Orchid

## Overview

Orchid-style chord generation synthesizer for M5Stack Tab5, featuring:
- 16-voice polyphonic synthesis
- Chord generation from root note + chord type
- Multiple synth engines (Virtual Analog, FM, Lead/Piano)
- Bass synth engine
- Built-in effects (Delay, Reverb, Chorus)
- Touch-optimized 1280x720 UI

## Hardware

- **Board**: M5Stack Tab5 (ESP32-P4)
- **Display**: 1280x720 touch screen
- **Audio**: Built-in speaker

## Features

### Synth Engines
- **Virtual Analog**: Sawtooth oscillator
- **FM**: 2-operator FM synthesis
- **Lead/Piano**: Sine wave with harmonics

### Chord Types
- Major, Minor
- Major 7th, Minor 7th, Dominant 7th
- Sus2, Sus4
- Diminished, Augmented
- Add9

### Effects
- Delay
- Reverb
- Chorus

### Bass Engine
- Optional bass note (one octave below root)
- Toggle on/off

## Usage

1. Select synth engine (Analog/FM/Piano)
2. Adjust effects with sliders
3. Enable/disable bass
4. Select chord type
5. Touch piano keys to play chords

## Building

Use Arduino IDE with M5Unified library:
- Install M5Unified library
- Select "M5Stack Tab5" board
- Compile and upload

## License

MIT
