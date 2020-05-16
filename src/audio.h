#ifndef FINTRO_AUDIO_H
#define FINTRO_AUDIO_H

#include "mlib.h"

#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_VOL_MAX 128

enum Audio_ModEnum {
    Audio_ModEnum_FRONTIER_THEME = 2,
    Audio_ModEnum_SILENCE = 9,
    Audio_ModEnum_FRONTIER_THEME_INTRO = 10,
};

typedef struct sModChannelData {
    i16 channelId;

    // Time in ticks (1/50th second) to play the note
    i16 noteTimeRemaining;

    //  0 - Loop, 1 - end note when 'noteTimeRemaining' gets down to 1, 3 - set next sample in Amiga AUDxL registers
    i16 noteSetState;

    // Last period value written to hardware
    i16 periodHW;

    // Last volume value written to hardware, 0xffff if no value written
    u16 volumeHW;

    // Current volume ramp, each tick read a new volume value
    u16* volumeRamp;
    u16* volumeRampSet;

    // Pointer to track list
    u16* trackList;
    // Current track
    u16* nextTrack;

    // current position in track
    u16* nextEffect;

    // First sample to play after the first has finished
    u8* sampleData;
    u16 sampleLen;

    // Second sample to play after the first has finished
    // Typically this points to silence, so the first sample doesn't loop
    u8* nextSampleData;
    u16 nextSampleLen;

    // Vibrato effect
    i16 vibratoMode;
    i16 vibratoPitchChangeDelay;
    i16 vibratoCountdown;
    i16 vibratoAdjustDown;
    i16 vibratoAdjustUp;
    i16 vibratoInitialDelay;
    i16 adjustPeriodDelay;

    // Portamento effect
    // 0 - Disabled, 1 - Pitch down, 2 - Pitch up
    i16 portamentoMode;
    i16 portamentoSpeed;
    i16 portamentoFinalPeriod;
    i16 portamentoDelay;

    u32 unused1;
    i16 mod14Set;
} ModChannelData;

typedef struct sModAudio {
    i16 field_0;
    i16 field_2;
    u16 volume;
    u16 period;
    i16 field_8;
    u32 field_a;
    i16 channelId;
    i16 field_10;
} ModAudio;

#ifdef USE_SDL
typedef struct sSampleConvert {
    u8* samplePtr;
    u16 sampleLen;
    u16 period;

    i16* sampleConverted;
    u32 sampleConvertedLen;

    u32 cacheClock;
    u8 used;
} SampleConvert;
#endif

#ifdef AMIGA
typedef struct sChannelRegisters {
    volatile u8* pos;        // ptr to start of waveform data
    volatile u16 len;        // length of waveform in words
    volatile u16 period;     // sample period
    volatile u16 volume;     // volume
    volatile u16 sampleData; // sample pair
    volatile u16 unused[2];
} MSTRUCTPACKED ChannelRegisters;
#else
typedef struct sChannelRegisters {
    u8* pos;
    u16 len;
    u16 period;
    u16 volume;
}  ChannelRegisters;
#endif

#define AUDIO_SAMPLE_CACHE_SIZE 20

typedef struct sAudio {
    // Main file data
    u8* data;
    u8 dataSize;

    // Fixed up / overridden sample data
    u8* samplesData;
    u32 samplesDataSize;

    u8 volume; // 0-128
#ifdef USE_SDL
    SDL_AudioDeviceID sdlAudioID;
#endif

    u32 soundPlayingChannel[4];
    u16 channelClear[4];

    // Module playback
    u16 modInitializing;
    u16 modStartTickOffset;
    u32 modBankSelect;
    u32 modBankSelectStore;

    u32 modVolumeSuppress;
    u32 modVolumeFadeState;
    u16 modVolumeFadeStep;
    u16 modVolumeFadeSpeed;
    u16 modApplyVolumeSuppress;

    u16 mod1;
    u8 modDone;

    u32 modChannel[4];

    ModChannelData modChannelData[9];
    ModAudio modAudio[4];

    // Current mod tick time
    u32 modTick;

    u16* modDefaultEffects[4];

    // Amiga hardware emulation
    u16 channelMask;
    ChannelRegisters* hw[4];
    ChannelRegisters hwBypass;

    // low pass filter on/off (unused)
    u16 lowPassFilter;

    // Current sample positions
    u32 samplePos[4];

    // Last value written in previous tick for each channel
    i32 lastValue[4];

#ifdef USE_SDL
    ChannelRegisters channelRegisters[4];

    // Sample conversion cache
    SampleConvert* channelSamples[4];
    SampleConvert sampleCache[AUDIO_SAMPLE_CACHE_SIZE];
    u32 sampleCacheCount;
    u32 sampleConvertClock;

    i16* audioOutputBuffer;
    u32 audioOutputBufferSize;
    u32 audioOutputContentSize;
    u32 audioBytesWriten;
#endif

    u32 numSamples;
    u32 numMods;
} AudioContext;

u32 Audio_Init(AudioContext* audio, u8* data, u32 size);

void Audio_Exit(AudioContext* audio);

MINLINE void Audio_SetVolume(AudioContext* audio, u8 volume) {
    audio->volume = volume;
}

void Audio_PlaySample(AudioContext* audio, int sampleIndex);

void Audio_ProgressTick(AudioContext* audio);

void Audio_ModStartAt(AudioContext* audio, int modIndex, u32 tickOffset);

MINLINE void Audio_ModStart(AudioContext* audio, int modIndex) {
    Audio_ModStartAt(audio, modIndex, 0);
}

void Audio_ModStop(AudioContext* audio);

MINLINE b32 Audio_ModDone(AudioContext* audio) {
    return audio->modDone;
}

MINLINE u32 Audio_NumSamples(AudioContext* audio) {
    return audio->numSamples;
}

MINLINE u32 Audio_NumMods(AudioContext* audio) {
    return audio->numMods;
}

#ifdef __cplusplus
}
#endif

#endif