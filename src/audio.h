#ifndef FINTRO_AUDIO_H
#define FINTRO_AUDIO_H

#include "mlib.h"

#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Frontier Amiga sound emulation for SDL
// Also can be compiled for native Amiga playback

enum Audio_ModEnum {
    Audio_ModEnum_HALL_OF_THE_MOUNTAIN_KING = 1,
    Audio_ModEnum_FRONTIER_THEME = 2,
    Audio_ModEnum_BABA_YAGA = 3,
    Audio_ModEnum_NIGHT_ON_THE_BARE_MOUNTAIN = 4,
    Audio_ModEnum_FRONTIER_SECOND_THEME = 5,
    Audio_ModEnum_RIDE_OF_THE_VALKYRIES = 6,
    Audio_ModEnum_BLUE_DANUBE = 7,
    Audio_ModEnum_GREAT_GATES_OF_KIEV = 8,
    Audio_ModEnum_SILENCE = 9,
    Audio_ModEnum_FRONTIER_THEME_INTRO = 10,
};

enum Audio_SampleEnum {
    Audio_SampleEnum_Engine1 = 1,
    Audio_SampleEnum_Engine2 = 2,
    Audio_SampleEnum_Engine3 = 3,
    Audio_SampleEnum_Engine4 = 4,
    Audio_SampleEnum_Explosion1 = 5,
    Audio_SampleEnum_Explosion2 = 6,
    Audio_SampleEnum_Explosion3 = 7,
    Audio_SampleEnum_Explosion4 = 8,
    Audio_SampleEnum_Explosion5 = 9,
    Audio_SampleEnum_Lazer1 = 10,
    Audio_SampleEnum_Plasma1 = 11,
    Audio_SampleEnum_LandingGearUp = 12,
    Audio_SampleEnum_LandingGearDown = 13,
    Audio_SampleEnum_Wind1 = 14,
    Audio_SampleEnum_Station = 14,
    Audio_SampleEnum_AirLockClose1 = 18,
    Audio_SampleEnum_AirLockClose2 = 19,
    Audio_SampleEnum_UI_Beep = 20,
    Audio_SampleEnum_UI_Ring = 21,
    Audio_SampleEnum_UI_Connect = 22,
    Audio_SampleEnum_UI_Beep2 = 23,
    Audio_SampleEnum_UI_Eject = 23,
    Audio_SampleEnum_Alert = 30,
    Audio_SampleEnum_Explosion6 = 31,
    Audio_SampleEnum_Hyperspace = 33,
    Audio_SampleEnum_Lazer2 = 35,
    Audio_SampleEnum_Plasma2 = 36,
    Audio_SampleEnum_Wind2 = 37,
};

typedef struct sAudioChannelControl {
    i16 channelId;

    // Time in ticks (1/50th second) to play the note
    i16 noteTimeRemaining;

    //  0 - Loop, 1 - end note when 'noteTimeRemaining' gets down to 1, 3 - set next sample in Amiga AUDxL registers
    i16 noteSetState;

    // Last period value written to hardware
    i16 periodHW;

    // Last volume value written to hardware (or 0xffff if no value written)
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

    // First sample to play
    u8* sampleData;
    u16 sampleLen;

    // Second sample to play after the first has finished
    // Typically this points to silence, so the first sample doesn't loop
    u8* nextSampleData;
    u16 nextSampleLen;

    // Vibrato effect - oscillating pitch changes
    i16 vibratoMode;
    i16 vibratoPitchChangeDelay;
    i16 vibratoCountdown;
    i16 vibratoAdjustDown;
    i16 vibratoAdjustUp;
    i16 vibratoInitialDelay;
    i16 adjustPeriodDelay;

    // Portamento effect - linear pitch up / down changes
    // 0 - Disabled, 1 - Pitch down, 2 - Pitch up
    i16 portamentoMode;
    i16 portamentoSpeed;
    i16 portamentoFinalPeriod;
    i16 portamentoDelay;

    u32 volumeRampValue;
    i16 modEffect13Set;
} AudioChannelControl;

typedef struct sAudioEngineSound {
    i16 inUse;
    i16 engineSoundId;
    u16 volume;
    u16 period;
    i16 shipId;
    // u8* ship;
    i16 channelId;
    i16 volumeDecayDelay;
} AudioEngineSound;

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
    volatile u8* pos;        // ptr to start of 8bit PCM data
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

#define AUDIO_VOL_MAX 128
#define AUDIO_SAMPLE_CACHE_SIZE 30
#define AUDIO_NUM_CHANNELS 4

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

    u32 soundToPlayChannel[AUDIO_NUM_CHANNELS];
    u32 soundPlayingChannel[AUDIO_NUM_CHANNELS];
    u32 engineSoundPlayingChannel[AUDIO_NUM_CHANNELS];

    volatile u16 modificationsInProgress;

    u16 modStartTickOffset;
    u32 modSilenceDuringPlayback;
    u32 modSilenceDuringPlaybackStore;

    u32 modVolumeSuppress;
    u32 modVolumeFadeState;
    u16 modVolumeFadeStep;
    u16 modVolumeFadeSpeed;
    u16 modApplyVolumeSuppress;

    u16 playModOnly;
    u8 modDone;

    AudioChannelControl channelCtrl[9];
    AudioEngineSound engineSound[AUDIO_NUM_CHANNELS];

    // Current mod tick time
    u32 modTick;

    // Amiga hardware emulation
    u16 channelMask;
    ChannelRegisters* hw[AUDIO_NUM_CHANNELS];
    ChannelRegisters hwBypass;

    // low pass filter on/off (unused)
    u16 lowPassFilter;

    // Current sample positions
    u32 samplePos[AUDIO_NUM_CHANNELS];

    // Last value written in previous tick for each channel
    i32 lastValue[AUDIO_NUM_CHANNELS];

#ifdef USE_SDL
    ChannelRegisters channelRegisters[AUDIO_NUM_CHANNELS];

    // Sample conversion cache
    SampleConvert* channelSamples[AUDIO_NUM_CHANNELS];
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

void Audio_PlaySample(AudioContext* audio, u16 sampleIndex, u16 volume);

void Audio_PlayEngineSound(AudioContext* audio, u16 engineSoundId, u16 volume, u16 period, u16 shipId);

void Audio_CancelSample(AudioContext* audio, u16 sampleIndex);

void Audio_StopSamples(AudioContext* audio);

void Audio_SetFade(AudioContext* audio, i16 fadeSpeed);

void Audio_ProgressTick(AudioContext* audio);

void Audio_ModStartAt(AudioContext* audio, u16 modIndex, u32 tickOffset);

MINLINE void Audio_ModStart(AudioContext* audio, int modIndex) {
    Audio_ModStartAt(audio, modIndex, 0);
}

void Audio_ModStop(AudioContext* audio);

#ifdef USE_SDL
void Audio_Pause(AudioContext* audio);
void Audio_Resume(AudioContext* audio);
#endif

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