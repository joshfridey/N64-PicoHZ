#include <Arduino.h>
#include <I2S.h>
#include <FatFS.h>

#include "AudioPlayer.h"

// ============================================================
// PIN DEFINITIONS
// ============================================================

constexpr uint8_t I2S_DOUT = 9;
constexpr uint8_t I2S_BCLK = 10;
constexpr uint8_t AMP_SD = 6;

// ============================================================
// AUDIO CONFIGURATION
// ============================================================

constexpr uint32_t AUDIO_SAMPLE_RATE = 48000;

const char *STARTUP_WAV = "/startup.wav";
const char *CLOCK15_WAV = "/clock15.wav";
const char *CLOCK20_WAV = "/clock20.wav";

// ============================================================
// GLOBALS
// ============================================================

I2S audio(OUTPUT);

bool audioReady = false;

volatile bool filesystemBusy = false;

// ============================================================
// AUDIO CONTROL
// ============================================================

void stopAudio()
{
    digitalWrite(AMP_SD, LOW);

    audio.end();

    audioReady = false;
}

// ============================================================
// AUDIO SETUP
// ============================================================

void setupAudio()
{
    if (!FatFS.begin())
    {
        Serial.println("ERROR: FatFS.begin() failed.");
        return;
    }

    pinMode(AMP_SD, OUTPUT);
    digitalWrite(AMP_SD, LOW);

    audio.setBCLK(I2S_BCLK);
    audio.setDATA(I2S_DOUT);
    audio.setBitsPerSample(16);

    if (!audio.begin(AUDIO_SAMPLE_RATE))
    {
        Serial.println("ERROR: audio.begin() failed.");
        return;
    }

    audioReady = true;
}

// ============================================================
// OUTPUT SAMPLE RATE
// ============================================================

uint32_t getOutputSampleRate(uint32_t sampleRate)
{
    switch (sampleRate)
    {
        case 8000:
        return 8000;

        case 16000:
        return 16000;

        case 32000:
        return 32000;

        case 44100:
        return 44100;

        case 48000:
        return 48000;

        case 88200:
        return 88200;

        case 96000:
        return 96000;

        case 11025:
        case 22050:
        return 44100;

        case 12000:
        case 24000:
        return 48000;
    }


    // ----------------------------------------------------------
    // UNKNOWN RATE
    // ----------------------------------------------------------

    const uint32_t supportedRates[] =
    {
        8000,
        16000,
        32000,
        44100,
        48000,
        88200,
        96000
    };

    uint32_t closestRate = supportedRates[0];

    uint32_t closestDifference = abs((int32_t)sampleRate - (int32_t)closestRate);

    for (size_t i = 1; i < sizeof(supportedRates) / sizeof(supportedRates[0]); i++)
    {
        uint32_t difference =
        abs((int32_t)sampleRate - (int32_t)supportedRates[i]);

        if (difference < closestDifference)
        {
            closestDifference = difference;
            closestRate = supportedRates[i];
        }
    }

    return closestRate;
}

// ============================================================
// READ PCM SAMPLE
// ============================================================

bool readSample(
    File &wavFile,
    uint16_t audioFormat,
    uint16_t bitsPerSample,
    int16_t &sample
)
{

    // ----------------------------------------------------------
    // 32-BIT FLOAT
    // ----------------------------------------------------------

    if (
        audioFormat == 3 &&
        bitsPerSample == 32
    )
    {
        float value;

        if (wavFile.read((uint8_t *)&value, 4) != 4)
        {
            return false;
        }

        if (value > 1.0f)
        {
            value = 1.0f;
        }

        if (value < -1.0f)
        {
            value = -1.0f;
        }

        sample = (int16_t)(value * 32767.0f);

        return true;
    }


    // ----------------------------------------------------------
    // 8-BIT PCM
    // ----------------------------------------------------------

    if (bitsPerSample == 8)
    {
        uint8_t value;

        if (wavFile.read((uint8_t *)&value, 1) != 1)
        {
            return false;
        }

        sample = ((int16_t)value - 128) << 8;

        return true;
    }


    // ----------------------------------------------------------
    // 16-BIT PCM
    // ----------------------------------------------------------

    if (bitsPerSample == 16)
    {
        int16_t value;

        if (wavFile.read((uint8_t *)&value, 2) != 2)
        {
            return false;
        }

        sample = value;

        return true;
    }


    // ----------------------------------------------------------
    // 24-BIT PCM
    // ----------------------------------------------------------

    if (bitsPerSample == 24)
    {
        uint8_t bytes[3];

        if (wavFile.read(bytes, 3) != 3)
        {
            return false;
        }

        int32_t value =
            ((int32_t)bytes[0]) |
            ((int32_t)bytes[1] << 8) |
            ((int32_t)bytes[2] << 16);

        if (value & 0x00800000)
        {
            value |= 0xFF000000;
        }

        sample = (int16_t)(value >> 8);

        return true;
    }


    // ----------------------------------------------------------
    // 32-BIT PCM
    // ----------------------------------------------------------

    if (bitsPerSample == 32)
    {
        int32_t value;

        if (wavFile.read((uint8_t *)&value, 4) != 4)
        {
            return false;
        }

        sample = (int16_t)(value >> 16);

        return true;
    }

    return false;
}

// ============================================================
// WAV PLAYER
// ============================================================

void playWav(const char *filename)
{
    if (!audioReady)
    {
        return;
    }

    filesystemBusy = true;

    File wavFile = FatFS.open(filename, "r");

    if (!wavFile)
    {
        filesystemBusy = false;
        return;
    }

    // ==========================================================
    // VERIFY RIFF / WAVE
    // ==========================================================

    char riff[4];
    char wave[4];

    if (wavFile.read((uint8_t *)riff, 4) != 4)
    {
        wavFile.close();
        filesystemBusy = false;
        return;
    }

    wavFile.seek(8);

    if (wavFile.read((uint8_t *)wave, 4) != 4)
    {
        wavFile.close();
        filesystemBusy = false;
        return;
    }

    if (
        memcmp(riff, "RIFF", 4) != 0 ||
        memcmp(wave, "WAVE", 4) != 0
    )
    {
        wavFile.close();
        filesystemBusy = false;
        return;
    }

    // ==========================================================
    // WAV FORMAT
    // ==========================================================

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;

    uint32_t dataSize = 0;
    uint32_t dataStart = 0;

    while (wavFile.available())
    {
        char chunkID[4];
        uint32_t chunkSize;

        if (wavFile.read((uint8_t *)chunkID, 4) != 4)
        {
            break;
        }

        if (wavFile.read((uint8_t *)&chunkSize, 4) != 4)
        {
            break;
        }


        // --------------------------------------------------------
        // FORMAT CHUNK
        // --------------------------------------------------------

        if (memcmp(chunkID, "fmt ", 4) == 0)
        {
            if (chunkSize < 16)
            {
                break;
            }

            wavFile.read((uint8_t *)&audioFormat, 2);

            wavFile.read((uint8_t *)&channels, 2);

            wavFile.read((uint8_t *)&sampleRate, 4);


            // Skip byte rate and block alignment

            wavFile.seek(wavFile.position() + 6);

            wavFile.read((uint8_t *)&bitsPerSample, 2);


            // Skip additional format information

            if (chunkSize > 16)
            {
                wavFile.seek(wavFile.position() + (chunkSize - 16));
            }
        }


        // --------------------------------------------------------
        // DATA CHUNK
        // --------------------------------------------------------

        else if (memcmp(chunkID, "data", 4) == 0)
        {
            dataSize = chunkSize;
            dataStart = wavFile.position();

            break;
        }


        // --------------------------------------------------------
        // UNKNOWN CHUNK
        // --------------------------------------------------------

        else
        {
            wavFile.seek(wavFile.position() + chunkSize + (chunkSize & 1));
        }

    }

    // ==========================================================
    // VALIDATE WAV
    // ==========================================================

    bool supportedFormat = false;

    if (
        audioFormat == 1 && (
            bitsPerSample == 8 ||
            bitsPerSample == 16 ||
            bitsPerSample == 24 ||
            bitsPerSample == 32
        )
    )
    {
        supportedFormat = true;
    }


    if (
        audioFormat == 3 &&
        bitsPerSample == 32
    )
    {
        supportedFormat = true;
    }


    if (
        !supportedFormat ||
        (channels != 1 && channels != 2) ||
        sampleRate < 8000 ||
        sampleRate > 96000 ||
        dataStart == 0 ||
        dataSize == 0
    )
    {
        wavFile.close();
        filesystemBusy = false;
        return;
    }

    // ==========================================================
    // CONFIGURE OUTPUT RATE
    // ==========================================================

    uint32_t outputSampleRate = getOutputSampleRate(sampleRate);

    audio.setFrequency(outputSampleRate);

    // ==========================================================
    // ENABLE AMPLIFIER
    // ==========================================================

    digitalWrite(AMP_SD, HIGH);

    delay(10);

    // ==========================================================
    // PLAY WAV
    // ==========================================================

    wavFile.seek(dataStart);

    uint32_t bytesPerSample = bitsPerSample / 8;

    uint32_t bytesPerFrame = bytesPerSample * channels;

    uint32_t frameCount = dataSize / bytesPerFrame;

    uint64_t resampleAccumulator = 0;

    for (uint32_t frame = 0; frame < frameCount; frame++)
    {
        int16_t leftSample = 0;
        int16_t rightSample = 0;


        // --------------------------------------------------------
        // LEFT / MONO SAMPLE
        // --------------------------------------------------------

        if (!readSample(wavFile, audioFormat, bitsPerSample, leftSample))
        {
            break;
        }


        // --------------------------------------------------------
        // RIGHT SAMPLE
        // --------------------------------------------------------

        if (channels == 2)
        {
            if (!readSample(
            wavFile,
            audioFormat,
            bitsPerSample,
            rightSample
            ))
            {
                break;
            }
        }
        else
        {
            rightSample = leftSample;
        }


        // --------------------------------------------------------
        // SAMPLE RATE CONVERSION
        // --------------------------------------------------------

        resampleAccumulator +=
        outputSampleRate;

        while (resampleAccumulator >= sampleRate)
        {
            audio.write16(leftSample, rightSample);

            resampleAccumulator -=
            sampleRate;
        }

    }

    // ==========================================================
    // FINISH AUDIO
    // ==========================================================

    uint32_t silenceFrames = outputSampleRate / 100;

    for (uint32_t i = 0; i < silenceFrames; i++)
    {
        audio.write16(0, 0);
    }

    audio.flush();

    digitalWrite(AMP_SD, LOW);

    wavFile.close();

    filesystemBusy = false;
}