#pragma once

#include <Arduino.h>

// ============================================================
// AUDIO FILES
// ============================================================

extern const char *STARTUP_WAV;
extern const char *CLOCK15_WAV;
extern const char *CLOCK20_WAV;

// ============================================================
// SHARED STATE
// ============================================================

extern volatile bool filesystemBusy;

// ============================================================
// AUDIO FUNCTIONS
// ============================================================

void setupAudio();
void stopAudio();
void playWav(const char *filename);