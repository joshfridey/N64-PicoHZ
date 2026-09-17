#include <Arduino.h>
#include <FatFS.h>
#include <FatFSUSB.h>

#include "AudioPlayer.h"
#include "SoundStorage.h"

// ============================================================
// GLOBALS
// ============================================================

bool updateMode = false;

// ============================================================
// FATFS USB CALLBACKS
// ============================================================

void usbUnplug(uint32_t i)
{
    (void)i;

    Serial.println("USB drive ejected, remounting FatFS...");

    if (FatFS.begin())
    {
        Serial.println("FatFS remounted.");
    }
    else
    {
        Serial.println("ERROR: FatFS remount failed.");
    }
}

void usbPlug(uint32_t i)
{
    (void)i;

    Serial.println("USB drive mounted by PC, releasing FatFS.");

    FatFS.end();
}

bool usbMountable(uint32_t i)
{
    (void)i;

    return !filesystemBusy;
}

// ============================================================
// SOUND UPDATE MODE
// ============================================================

void startSoundUpdateMode()
{
    Serial.println();
    Serial.println("Entering Sound Update Mode...");

    updateMode = true;

    stopAudio();

    if (!FatFS.begin())
    {
        Serial.println("ERROR: FatFS failed before USB mode.");

        updateMode = false;
        return;

    }

    FatFSUSB.onUnplug(usbUnplug);
    FatFSUSB.onPlug(usbPlug);
    FatFSUSB.driveReady(usbMountable);

    FatFSUSB.begin();

    Serial.println("Sound Update Mode started.");
}