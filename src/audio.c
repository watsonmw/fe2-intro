//
// Reverse Engineered from:
//   [Frontier: Elite II © David Braben 1993 & 1994](https://en.wikipedia.org/wiki/Frontier:_Elite_II)
//

#ifdef M_USE_SDL
#include <SDL2/SDL.h>
#elif AMIGA
#include <proto/exec.h>
#endif

#include "audio.h"

#define ENGINE_SOUND_DECAY_DELAY 51

#define MOD_CHANNEL_PROGRESS_BEGIN 0
#define MOD_CHANNEL_PROGRESS_END 4

#define AMIGA_CHIP_CLOCK_TICKS 3579545

#ifdef AUDIO_BUFFERED_PLAYBACK
#define SAMPLES_PER_TICK (AUDIO_PLAYBACK_FEQ / AUDIO_TICKS_PER_SECOND)
#define VOLUME_SCALE 128
#define AUDIO_BUFFER_SIZE 4096
#endif

// Sentinel value to indicate volume is not set yet
#define VOLUME_UNSET 0xffffu

// Sentinel value to indicate volume ramp should repeat
#define VOLUME_RAMP_END 0xffu

#define NOTE_STATE_PTRS_WRITTEN 1
#define NOTE_STATE_WRITE_NEXT 3

#include "platform/amiga/amigahw.h"

#ifdef WASM_DIRECT
#include "wasm_funcs.h"
#endif

// First hunk offset, this is to apply any fix-ups to relocated address pointers
#define FILE_MAP_OFFSET 0x228
#define DATA_OFFSET_TO_PTR(n) ((u8*)(audio->data + FILE_MAP_OFFSET + n))

// Offsets of key audio data in Amiga executable
#define AUDIO_SAMPLES_EFFECT_BASE 0x2808
#define AUDIO_MOD_TRACK_LIST_BASE 0x199a

#define AUDIO_SAMPLE1_PTR 0x9dd4
#define AUDIO_SAMPLE2_PTR 0x9d8e
#define AUDIO_SAMPLE3_PTR 0x9d1e

#ifdef AMIGA
#define WRITE_SAMPLE_PTR(hwReg, val) HW_WRITE32(hwReg, val)
#else
#define WRITE_SAMPLE_PTR(hwReg, val) *hwReg = val
#endif

enum EffectEnum {
    EffectType_NONE = 0,
    EffectType_NOTE = 1 << 2,
    EffectType_NEXT_TRACK = 2 << 2,
    EffectType_VOLUME_RAMP = 3 << 2,
    EffectType_PORTAMENTO = 4 << 2,
    EffectType_VIBRATO = 5 << 2,
    EffectType_PORTAMENTO_CLEAR = 6 << 2,
    EffectType_VIBRATO_CLEAR = 7 << 2,
    EffectType_SILENCE = 8 << 2,
    EffectType_STOP_SAMPLE_1 = 9 << 2,
    EffectType_STOP_SAMPLE_2 = 10 << 2,
    EffectType_LOWPASS_DISABLE = 11 << 2,
    EffectType_LOWPASS_ENABLE = 12 << 2,
    EffectType_13 = 13 << 2,
    EffectType_JMP = 14 << 2,
    EffectType_MODULE_DONE = 15 << 2,
};

static const u16 sModInitializeEffect[2] = { MBIGENDIAN16(EffectType_NEXT_TRACK) }; // Switch track
static const u16 sModInitialTracks[4] = { 0, 0, 0, 0 }; // Play first track
static const u16 sVolumeRampBlank[3] = { 0, 0, MBIGENDIAN16(VOLUME_RAMP_END) }; // Zero volume recurring
static const b32 sDebugLog = FALSE;

typedef struct sAudioSample {
    // Offsets are actual offsets in file (before relocation fixups)
    u32 origOffset;
    u16 origSize;

    u8* data;
    u16 len;
} AudioSample;

static AudioSample sSample8iPCM[] = {
        // Module instruments
        { 0x09e1a, 2960 },
        { 0x0a9aa, 2240 },
        { 0x0b26a, 7020 },
        { 0x0cdd6, 5550 },
        { 0x0e384, 3796 },
        { 0x0f258, 8668 },
        { 0x11434, 3338 },
        { 0x1213e, 6360 },
        { 0x13a16, 6350 },
        { 0x152e4, 5052 },
        { 0x166a0, 10594 },
        { 0x19002, 4032 },
        { 0x19fc2, 3636 },
        { 0x2647a, 16 },  // silence
        // Sound effects
        { 0x1adf6, 6870 },
        { 0x1c8d2, 2544 },
        { 0x1d2c4, 1386 },
        { 0x1d82e, 2800 },
        { 0x1e31e, 1440 },
        { 0x1edce, 6870 },
        { 0x1e8be, 1296 },
        { 0x1edce, 1950 },
        { 0x1f56c, 2832 },
        { 0x2007c, 2124 },
        { 0x208c8, 2440 },
        { 0x21250, 3272 },
        { 0x21f18, 1764 },
        { 0x225fc, 586 },
        { 0x22846, 7182 },
        { 0x24454, 7818 },
        { 0x26512, 8 },
};

static const u16 sAudioSamplesVolumeOffsets[] = {
        0,  // 0
        0,  // 1
        0,  // 2
        0,  // 3
        0,  // 4
        24, // 5
        24, // 6
        24, // 7
        22, // 8
        22, // 9
        22, // 10
        22, // 11
        46, // 12
        0,  // 13
        0,  // 14
        0,  // 15
        0,  // 16
        0,  // 17
        0,  // 18
        0,  // 19
        0,  // 20
        0,  // 21
        0,  // 22
        0,  // 23
        0,  // 24
        0,  // 25
        0,  // 26
        0,  // 27
        0,  // 28
        0,  // 29
        0,  // 30
        0,  // 31
        0,  // 32
        0,  // 33
        0,  // 34
        22, // 35
        22, // 36
        0,  // 37
        0,  // 38
        0,  // 39
};

static void Audio_ProgressTickInternal(AudioContext* audio);

#if defined(WASM_DIRECT) || defined(M_USE_SDL)
static void Audio_RenderInternal(AudioContext* audio, u32 numTicks, b32 bWriteFrames);
#endif

#ifdef M_USE_SDL
SDLCALL void Audio_Callback(void *userdata, Uint8 * stream, int bytesRequested) {
    AudioContext* audio = (AudioContext*)userdata;
    u32 sizeRemaining = audio->audioOutputContentSize - audio->audioBytesWriten;
    if (audio->audioOutputBuffer) {
        if (bytesRequested <= sizeRemaining) {
            memcpy(stream, audio->audioOutputBuffer + (audio->audioBytesWriten/2), bytesRequested);
            return;
        }

        if (sizeRemaining) {
            memcpy(stream, audio->audioOutputBuffer + (audio->audioBytesWriten/2), sizeRemaining);
            stream += sizeRemaining;
            bytesRequested -= sizeRemaining;
        }
    }

    u32 numSamples = bytesRequested / 4;
    u32 ticks = ((numSamples * 50) / AUDIO_PLAYBACK_FEQ) + 1;

    audio->audioBytesWriten = 0;

    if (audio->modStartTickOffset) {
        Audio_RenderInternal(audio, audio->modStartTickOffset, FALSE);
        audio->modStartTickOffset = 0;
    }

    Audio_RenderInternal(audio, ticks, TRUE);

    sizeRemaining = audio->audioOutputContentSize;
    if (bytesRequested <= sizeRemaining) {
        memcpy(stream, audio->audioOutputBuffer, bytesRequested);
        audio->audioBytesWriten = bytesRequested;
    } else {
        memcpy(stream, audio->audioOutputBuffer, sizeRemaining);
        audio->audioBytesWriten = sizeRemaining;
    }
}
#endif

#ifndef AMIGA

void Audio_RenderFrames(AudioContext* audio, u32 ticks) {
    audio->audioBytesWriten = 0;
    if (audio->modStartTickOffset) {
        Audio_RenderInternal(audio, audio->modStartTickOffset, FALSE);
        audio->modStartTickOffset = 0;
    }

    Audio_RenderInternal(audio, ticks, TRUE);
}

void Audio_ClearCache(AudioContext* audio) {
    for (int i = 0; i < AUDIO_SAMPLE_CACHE_SIZE; ++i) {
        SampleConvert* s = audio->sampleCache + i;
        if (s->sampleConverted != NULL) {
            MFree(s->sampleConverted, s->sampleConvertedBufferSize);
            memset(s, 0, sizeof(SampleConvert));
        }
    }
}

#endif

static void Audio_CopyAndFixSamples(AudioContext* audio) {
    audio->samplesDataSize = 0x20000;
#ifdef AMIGA
    // Allocate some chip ram that will be used to store the fixed up samples
    audio->samplesData = (u8*) AllocMem(audio->samplesDataSize, MEMF_CHIP);
#else
    audio->samplesData = (u8*) MMalloc(audio->samplesDataSize);
#endif

    u8* samplesData = audio->samplesData;
    u32 numSamples = sizeof(sSample8iPCM) / sizeof(AudioSample);
    for (int i = 0; i < numSamples; ++i) {
        AudioSample* audioSample = ((AudioSample*)&(sSample8iPCM[i]));

        u32 origSize = audioSample->origSize;
        u32 afterSize = 0;

        audioSample->data = samplesData;
        u8* origData = audio->data + audioSample->origOffset;

        // Sample quality is pretty bad, do some basic cleanups to reduce clicking.
        // Some samples are changed to correctly line up phase when looping.
        switch (audioSample->origOffset) {
            case 0x9e1a:
                // last 2 samples are out of phase, remove to reduce clicks
                afterSize = origSize - 2;
                memcpy(samplesData, origData, afterSize);
                break;
            case 0xf258:
                // first sample byte is wrong, replace to reduce clicks
                afterSize = origSize;
                memcpy(samplesData + 1, origData + 1, origSize - 1);
                samplesData[0] = 0x0d;
                break;
            case 0x11434:
                // Looping fixes to reduce clicks
                afterSize = origSize;
                memcpy(samplesData + 4, origData, origSize - 4);
                samplesData[0] = 0xfa;
                samplesData[1] = 0xea;
                samplesData[2] = 0xe5;
                samplesData[3] = 0xd0;
                break;
            case 0x1213e:
                // last 4 samples are out of phase, remove to reduce clicks
                afterSize = origSize - 4;
                memcpy(samplesData, origData, afterSize);
                break;
            case 0x166a0:
                // Looping fixes to reduce clicks
                afterSize = origSize;
                memcpy(samplesData + 4, origData, origSize - 4);
                samplesData[0] = 0xfa;
                samplesData[1] = 0xea;
                samplesData[2] = 0xe5;
                samplesData[3] = 0xd0;
                break;
            case 0x152e4:
                // last 5 samples are out of phase, remove to reduce clicks
                afterSize = origSize - 4;
                memcpy(samplesData, origData, afterSize);
                samplesData[afterSize - 1] = 0x00;
                break;
            case 0x19fc2:
                // last 2 samples are out of phase, remove to reduce clicks
                afterSize = origSize - 2;
                memcpy(samplesData, origData, afterSize);
                break;
            default:
                afterSize = origSize;
                memcpy(samplesData, origData, origSize);
                break;
        }

        audioSample->len = afterSize;
        samplesData += afterSize;
    }

    u32 memRequired = samplesData - audio->samplesData;
    if (memRequired > audio->samplesDataSize) {
        MLogf("ERROR: audio chip mem required %x", memRequired);
        MBreakpoint("Sample space needs adjusting");
    }
}

static u8* Audio_GetSampleData(u32 sampleOffset) {
    sampleOffset += FILE_MAP_OFFSET;
    u32 numSamples = sizeof(sSample8iPCM) / sizeof(AudioSample);
    for (int i = 0; i < numSamples; ++i) {
        const AudioSample* audioSample = sSample8iPCM + i;

        if (audioSample->origOffset == sampleOffset) {
            return audioSample->data;
        }
    }
    MLogf("Unknown sample: %x", sampleOffset);
    return NULL;
}

u32 Audio_Init(AudioContext* audio, u8* data, u32 size) {
    memset(audio, 0, sizeof(AudioContext));

    audio->data = data;
    audio->dataSize = size;

    Audio_CopyAndFixSamples(audio);

    audio->masterVolume = 32;

    audio->playModOnly = 1;

    audio->numSamples = 39;
    audio->numMods = 10;

    for (int i = 0; i < 8; ++i) {
        AudioChannelControl* channel = audio->channelCtrl + i;
        channel->nextEffect = (u16*)sModInitializeEffect;
        channel->nextTrack = (u16*)sModInitialTracks;
        channel->trackList = (u16*)sModInitialTracks;
        channel->volumeHW = VOLUME_UNSET;
    }

    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        audio->engineSound[i].channelId = i + 1;
    }

#ifdef AMIGA
    ChannelRegisters* base = (ChannelRegisters*)((void*)AMIGA_AUD0_BASE);
    for (int i = 0; i < 4; i++) {
        audio->hw[i] = base + i;
    }
    HW_WRITE16(AMIGA_DMACON,AMIGA_CON_CLR | 0xf);
    HW_WRITE16(AMIGA_ADKCON,AMIGA_CON_CLR | 0xff);
#elif defined(AUDIO_BUFFERED_PLAYBACK)
    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        audio->hw[i] = audio->channelRegisters + i;
    }
#ifdef M_USE_SDL
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID sdlAudioID;

    memset(&want, 0, sizeof(want));
    want.freq = AUDIO_PLAYBACK_FEQ;
    want.format = AUDIO_S16;
    want.channels = 2;
    want.samples = AUDIO_BUFFER_SIZE;
    want.callback = Audio_Callback;
    want.userdata = audio;

    sdlAudioID = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (sdlAudioID == 0) {
        MLogf("Failed to open audio: %s", SDL_GetError());
        return 1;
    } else {
        if (have.format != want.format) {
            MLog("We didn't get S16 audio format.");
            return 2;
        }
    }

    audio->sdlAudioID = sdlAudioID;

    SDL_PauseAudioDevice(audio->sdlAudioID, 0);
#endif
#endif

    return 0;
}

void Audio_Exit(AudioContext* audio) {
#ifdef M_USE_SDL
    SDL_CloseAudioDevice(audio->sdlAudioID); audio->sdlAudioID = 0;

    if (audio->audioOutputBuffer != NULL) {
        MFree(audio->audioOutputBuffer, audio->audioOutputBufferSize); audio->audioOutputBuffer = NULL;
    }

    Audio_ClearCache(audio);
#endif

#ifdef AMIGA
    HW_WRITE16(AMIGA_DMACON, AMIGA_CON_CLR | 0xf);

    FreeMem(audio->samplesData, audio->samplesDataSize);
#else
    MFree(audio->samplesData, audio->samplesDataSize);
#endif
    audio->samplesData = 0;
}

u16* Audio_GetSampleEffect(AudioContext* audio, u16 sampleId) {
    if (sampleId >= sizeof(sAudioSamplesVolumeOffsets) / sizeof(u16)) {
        MLogf("Sample outside range %d", sampleId);
        return 0;
    }

    u32* sampleBase = (u32*)(audio->data + AUDIO_SAMPLES_EFFECT_BASE);
    u32 sampleOffset = MBIGENDIAN32(sampleBase[sampleId]);
    return (u16*)(DATA_OFFSET_TO_PTR(sampleOffset));
}

static u16 Audio_PlaySample2(AudioContext* audio, u16 sampleId, u16 volume, u16 engineSound) {
    u16 restartSample = 0;

    u16* effect = Audio_GetSampleEffect(audio, sampleId);
    if (effect == 0) {
        return 0;
    }
    switch (sampleId) {
        case 14:
            effect[68] = MBIGENDIAN16(0x7d00);
            break;
        case 15:
            effect[9] = MBIGENDIAN16(0x1);
            break;
        case 16:
            effect[9] = MBIGENDIAN16(0x2);
            break;
        case 17:
            effect[14] = MBIGENDIAN16(0x2);
            break;
        default:
            break;
    }

    u16 channelId = 0;
    for (int i = AUDIO_NUM_CHANNELS - 1; i >= 0; --i) {
        if (audio->soundPlayingChannel[i] == sampleId) {
            restartSample = 1;
            u16 volumeOffset = sAudioSamplesVolumeOffsets[sampleId];
            if (volumeOffset) {
                u16* volumeForEffect = effect + (volumeOffset/2);
                if (MBIGENDIAN16(*volumeForEffect) < volume) {
                    restartSample = 1;
                } else {
                    return 0;
                }
            }
            switch (sampleId) {
                case 14:
                case 15:
                case 16:
                case 17:
                    return 0;
                default:
                    break;
            }
            channelId = i + 1;
            break;
        }
    }

    u16 volumeOffset = sAudioSamplesVolumeOffsets[sampleId] / 2;
    if (volumeOffset) {
        u16* volumeForEffect = effect + volumeOffset;
        *volumeForEffect = MBIGENDIAN16(volume);
    }

    if (restartSample == 0) {
        // Look for free channel
        for (int i = AUDIO_NUM_CHANNELS - 1; i >= 0; --i) {
            if (!audio->soundPlayingChannel[i] && !audio->engineSoundPlayingChannel[i]) {
                channelId = i + 1;
                break;
            }
        }
    }

    if (channelId == 0) {
        if (engineSound) {
            channelId = 1; // Fallback if no other channel is used
            for (int i = AUDIO_NUM_CHANNELS - 1; i >= 0; --i) {
                if (!audio->engineSoundPlayingChannel[i]) {
                    channelId = i + 1;
                    break;
                }
            }
        } else {
            return 0xff;
        }
    }

    if (channelId) {
        audio->soundToPlayChannel[channelId - 1] = sampleId;
        if (engineSound) {
            audio->engineSoundPlayingChannel[channelId - 1] = 1;
        } else {
            audio->soundPlayingChannel[channelId - 1] = sampleId;
        }
        AudioChannelControl* channelData = audio->channelCtrl + 3 + channelId;
        memset(channelData, 0, sizeof(AudioChannelControl));
        channelData->channelId = channelId + 3;
    }

    return channelId;
}

void Audio_PlaySample(AudioContext* audio, u16 sampleId, u16 volume) {
    audio->playModOnly = 0;
    audio->modificationsInProgress = 1;
    Audio_PlaySample2(audio, sampleId, volume, 0);
    audio->modificationsInProgress = 0;
}

static void Audio_SetTone(AudioEngineSound* engineSound, u16 engineSoundId, u16 volume, u16 period, u16 shipId) {
    engineSound->inUse = 1;
    engineSound->engineSoundId = engineSoundId;
    engineSound->volume = volume;
    engineSound->period = period;
    engineSound->shipId = shipId;
    engineSound->volumeDecayDelay = 0;
}

static void Audio_UpdateVolumeAndPeriod(AudioContext* audio, AudioEngineSound* engineSound) {
    u16 channelId = engineSound->channelId - 1;
    if (audio->soundPlayingChannel[channelId]) {
        engineSound->inUse = 0;
        engineSound->shipId = 0;
    } else {
        ChannelRegisters* hw = audio->hw[channelId];
        HW_WRITE16(&hw->volume, engineSound->volume);
        HW_WRITE16(&hw->period, engineSound->period);
    }
}

void Audio_PlayEngineSound(AudioContext* audio, u16 engineSoundId, u16 volume, u16 period, u16 shipId) {
    if (volume == 0 || volume > 64) {
        return;
    }

    audio->modificationsInProgress = 1;

    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        AudioEngineSound* engineSound = audio->engineSound + i;
        if (engineSound->inUse) {
            if (engineSound->shipId == shipId) {
                Audio_SetTone(engineSound, engineSoundId, volume, period, shipId);
                Audio_UpdateVolumeAndPeriod(audio, engineSound);
                goto done;
            }
        }
    }

    u32 effectOffset = 0;
    switch(engineSoundId) {
        case 1:
            effectOffset = AUDIO_SAMPLE1_PTR - FILE_MAP_OFFSET;
            break;
        case 2:
            effectOffset = AUDIO_SAMPLE2_PTR - FILE_MAP_OFFSET;
            break;
        case 3:
        default:
            effectOffset = AUDIO_SAMPLE3_PTR - FILE_MAP_OFFSET;
            break;
    }

    AudioEngineSound* freeAudio = 0;
    int fixedSample = 0;
    for (int i = AUDIO_NUM_CHANNELS - 1; i >= 0; --i) {
        AudioEngineSound* engineSound = audio->engineSound + i;
        if (!engineSound->inUse) {
            freeAudio = engineSound;
            fixedSample = i + 1;
            break;
        }
    }

    if (!freeAudio) {
        // Find channel with lowest volume
        u16 lowestVolume = 0xffff;
        for (int i = AUDIO_NUM_CHANNELS - 1; i >= 0; --i) {
            AudioEngineSound* engineSound = audio->engineSound + i;
            if (lowestVolume > engineSound->volume) {
                lowestVolume = engineSound->volume;
                fixedSample = i + 1;
                freeAudio = engineSound;
            }
        }
    }

    freeAudio->inUse = 1;
    audio->engineSoundPlayingChannel[freeAudio->channelId - 1] = 0;
    Audio_SetTone(freeAudio, engineSoundId, volume, period, shipId);

    u16* effectData = Audio_GetSampleEffect(audio, fixedSample);

    *(u32*)(effectData + 4) = MBIGENDIAN32(effectOffset);
    *(effectData + 6) = MBIGENDIAN16(period);
    *(effectData + 7) = MBIGENDIAN16(200);
    *(effectData + 10) = MBIGENDIAN16(volume);

    u16 channelId = Audio_PlaySample2(audio, fixedSample, volume, 1);
    if (channelId == 0xff) {
        freeAudio->inUse = 0;
        freeAudio->shipId = 0;
    }

    done:
    audio->modificationsInProgress = 0;
}

static void Audio_ChannelControl(AudioContext* audio, u16 dmaCon) {
#ifdef AMIGA
    HW_WRITE16(AMIGA_DMACON, dmaCon);
#else
    if (dmaCon & AMIGA_CON_SET) {
        // Enable channel(s)
        audio->channelMask |= (dmaCon & 0xf);
    } else {
        // Disable channel(s)
        audio->channelMask &= ~(dmaCon & 0xf);
    }
#endif
}

static void Audio_StopChannel(AudioContext* audio, ChannelRegisters* hw, u16 channelId) {
    Audio_ChannelControl(audio, AMIGA_CON_CLR | (1 << channelId));
    HW_WRITE16(&hw->volume, 0);
    HW_WRITE16(&hw->period, 1);
}

static void Audio_StopSamplePlayingChannel(AudioContext* audio, u16 channelId) {
    Audio_StopChannel(audio, audio->hw[channelId], channelId);

    audio->soundPlayingChannel[channelId] = 0;
}

static void Audio_StopTonePlayingChannel(AudioContext* audio, u16 channelId) {
    audio->engineSoundPlayingChannel[channelId] = 0;
    Audio_StopSamplePlayingChannel(audio, channelId);
}

void Audio_CancelSample(AudioContext* audio, u16 sampleId) {
    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        if (audio->soundPlayingChannel[i] == sampleId) {
            Audio_StopSamplePlayingChannel(audio, i);
            return;
        }
    }
}

void Audio_StopSamples(AudioContext* audio) {
    audio->modificationsInProgress = 1;
    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        Audio_StopSamplePlayingChannel(audio, i);
        AudioChannelControl* channelData = audio->channelCtrl + i + 4;
        memset(channelData, 0, sizeof(AudioChannelControl));
        channelData->channelId = i + 4;
    }
    audio->playModOnly = 1;
    audio->modificationsInProgress = 0;
}

void Audio_SetFade(AudioContext* audio, i16 fadeSpeed) {
    audio->modVolumeFadeState = -1;
    audio->modVolumeFadeSpeed = fadeSpeed;
    audio->modVolumeSuppress = 0x40;
}

void Audio_FadeIn(AudioContext* audio, u16 fadeSpeed) {
    audio->modVolumeFadeState = 1;
    audio->modVolumeFadeSpeed = fadeSpeed;
    audio->modVolumeSuppress = 0x40;
}

void Audio_ModStartAt(AudioContext* audio, u16 modIndex, u32 tickOffset) {
    audio->modVolumeSuppress = 0;

    audio->modificationsInProgress = 1;
    audio->modSilenceDuringPlaybackStore = audio->modSilenceDuringPlayback;
    audio->modSilenceDuringPlayback = 1;
    audio->modTick = 0;
    audio->modDone = 0;

    AudioChannelControl* channelCtrl = audio->channelCtrl;
    memset(channelCtrl, 0, sizeof(audio->channelCtrl) / 2);

    if (modIndex) {
        u32* trackListOffset = (u32*)(audio->data + AUDIO_MOD_TRACK_LIST_BASE);
        trackListOffset += ((modIndex - 1) * 4);

        for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
            u32 offset = MBIGENDIAN32(*(trackListOffset++));

            AudioChannelControl* channel = audio->channelCtrl + i;
            channel->trackList = (u16*)DATA_OFFSET_TO_PTR(offset);
            channel->nextEffect = (u16*)sModInitializeEffect;
            channel->nextTrack = (u16*)sModInitialTracks;
        }
    }

    for (int i = 0; i < 8; ++i) {
        AudioChannelControl* channel = audio->channelCtrl + i;
        channel->channelId = i;
        channel->volumeHW = VOLUME_UNSET;
    }

#ifdef AUDIO_BUFFERED_PLAYBACK
    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        if (audio->channelSamples[i]) {
            audio->channelSamples[i]->used = 0;
        }
        audio->channelSamples[i] = NULL;
        audio->samplePos[i] = 0;
    }
#endif

    audio->modSilenceDuringPlayback = audio->modSilenceDuringPlaybackStore;

    audio->modVolumeSuppress = 0;
    audio->modVolumeFadeState = 0;

    if (modIndex == Audio_ModEnum_FRONTIER_THEME_INTRO) {
        Audio_FadeIn(audio, 0x13);
    }

    audio->modStartTickOffset = tickOffset;

    audio->modificationsInProgress = 0;
}

void Audio_ModStop(AudioContext* audio) {
    Audio_ModStart(audio, Audio_ModEnum_SILENCE);
}

#ifdef M_USE_SDL
void Audio_Pause(AudioContext* audio) {
    SDL_PauseAudioDevice(audio->sdlAudioID, 1);
}

void Audio_Resume(AudioContext* audio) {
    SDL_PauseAudioDevice(audio->sdlAudioID, 0);
}
#elif WASM_DIRECT
void Audio_Pause(AudioContext* audio) {
    WASM_AudioPause();
}

void Audio_Resume(AudioContext* audio) {
    WASM_AudioResume();
}
#endif

static void Audio_ChannelCtrlAdjustVolume(AudioContext* audio, u32 channelId, AudioChannelControl* channelCtrl, ChannelRegisters* hw) {
    u16 volume = MBIGENDIAN16(*channelCtrl->volumeRamp);
    if (volume == VOLUME_RAMP_END) {
        // volume ramp end, replay last value
        channelCtrl->volumeRamp -= 1;
    }

    volume = MBIGENDIAN16(*(channelCtrl->volumeRamp++));

    if (audio->modApplyVolumeSuppress) {
        if (audio->modVolumeSuppress > volume) {
            volume = 0;
        } else {
            volume -= audio->modVolumeSuppress;
        }
    }

    if (channelCtrl->volumeHW != volume) {
        HW_WRITE16(&hw->volume, volume);
        channelCtrl->volumeHW = volume;
        if (sDebugLog) {
            MLogf("channelId: %d: set volume %d", channelId, hw->volume);
        }
    }
}

void Audio_ChannelProgress(AudioContext* audio, AudioChannelControl* channelCtrl, ChannelRegisters* hw, u16 channelId, u16 dmaChannel) {
    if (channelCtrl->noteTimeRemaining) {
        if (channelCtrl->noteTimeRemaining == 1) {
            if (channelCtrl->noteSetState) {
                Audio_ChannelControl(audio, AMIGA_CON_CLR | dmaChannel);
            }
        } else {
            if (channelCtrl->noteSetState == NOTE_STATE_WRITE_NEXT) {
                WRITE_SAMPLE_PTR(&hw->pos, channelCtrl->nextSampleData);
                HW_WRITE16(&hw->len, channelCtrl->nextSampleLen);
                if (sDebugLog) {
                    u32 fileOffset = hw->pos - audio->data;
                    MLogf("channelId: %d: set sample1 %x %x", channelId, fileOffset, hw->len);
                }
                channelCtrl->noteSetState = NOTE_STATE_PTRS_WRITTEN;
            }
        }

        channelCtrl->noteTimeRemaining--;

        b32 periodChange = FALSE;
        u16 period = channelCtrl->periodHW;
        if (channelCtrl->portamentoMode) {
            if (channelCtrl->portamentoDelay) {
                channelCtrl->portamentoDelay--;
            } else {
                periodChange = TRUE;
                if (channelCtrl->portamentoMode == 1) {
                    period += channelCtrl->portamentoSpeed;
                    if (period > channelCtrl->portamentoFinalPeriod) {
                        period = channelCtrl->portamentoFinalPeriod;
                    }
                } else {
                    period -= channelCtrl->portamentoSpeed;
                    if (period < channelCtrl->portamentoFinalPeriod) {
                        period = channelCtrl->portamentoFinalPeriod;
                    }
                }
            }
        }

        if (channelCtrl->vibratoMode) {
            if (channelCtrl->vibratoInitialDelay) {
                channelCtrl->vibratoInitialDelay--;
            } else {
                if (channelCtrl->vibratoPitchChangeDelay) {
                    channelCtrl->vibratoPitchChangeDelay--;
                } else {
                    channelCtrl->vibratoPitchChangeDelay = channelCtrl->vibratoCountdown;
                    periodChange = TRUE;
                    if (channelCtrl->vibratoMode > 3) {
                        period += channelCtrl->vibratoAdjustDown;
                        channelCtrl->vibratoMode++;
                        if (channelCtrl->vibratoMode == 5) {
                            channelCtrl->vibratoMode = 1;
                        }
                    } else {
                        period -= channelCtrl->vibratoAdjustUp;
                        channelCtrl->vibratoMode++;
                    }
                }
            }
        }

        if (periodChange) {
            HW_WRITE16(&hw->period, period);
            channelCtrl->periodHW = period;
        }
    } else {
        // Process/Parse mod data
        channelCtrl->modEffect13Set = 0;

        u16* effectPtr = channelCtrl->nextEffect;

        b32 done = FALSE;
        do {
            u16 wordCode = MBIGENDIAN16(*(effectPtr++));
            if (wordCode <= 0x64) {
                u16 effectCmd = wordCode;
                if (sDebugLog) {
                    MLogf("channelId: %d: cmd: %d", channelCtrl->channelId, effectCmd);
                }
                switch (effectCmd) {
                    case EffectType_NONE:
                        return;
                    case EffectType_NOTE: {
                        u32 offset = MBIGENDIAN32(*((u32*)effectPtr));
                        effectPtr += 2;
                        u16* noteData = (u16*)DATA_OFFSET_TO_PTR(offset);
                        if (*noteData == 0) {
                            channelCtrl->noteSetState = MBIGENDIAN16(*(noteData++));
                            WRITE_SAMPLE_PTR(&hw->pos, Audio_GetSampleData(MBIGENDIAN32(*((u32*)noteData))));
                            noteData += 2;
                            HW_WRITE16(&hw->len, MBIGENDIAN16(*(noteData)));
                            if (sDebugLog) {
                                u32 fileOffset = hw->pos - audio->data;
                                MLogf("channelId: %d, cmd: %d, set sample2 %x %x", channelId, channelCtrl->noteSetState,
                                      fileOffset, hw->len);
                            }
                        } else {
                            channelCtrl->noteSetState = MBIGENDIAN16(*(noteData++));
                            channelCtrl->sampleData = Audio_GetSampleData(MBIGENDIAN32(*((u32*)noteData)));
                            noteData += 2;
                            channelCtrl->sampleLen = MBIGENDIAN16(*(noteData++));
                            channelCtrl->nextSampleData = Audio_GetSampleData(MBIGENDIAN32(*((u32*)noteData)));
                            noteData += 2;
                            channelCtrl->nextSampleLen = MBIGENDIAN16(*(noteData));
                        }
                        break;
                    }
                    case EffectType_NEXT_TRACK: {
                        u32 offset = *((u32*)channelCtrl->nextTrack);
                        if (offset == 0) {
                            channelCtrl->nextTrack = channelCtrl->trackList;
                            offset = MBIGENDIAN32(*((u32*)channelCtrl->nextTrack));
                            channelCtrl->nextTrack += 2;
                            effectPtr = (u16*)DATA_OFFSET_TO_PTR(offset);
                        } else {
                            offset = MBIGENDIAN32(offset);
                            channelCtrl->nextTrack += 2;
                            effectPtr = (u16*)DATA_OFFSET_TO_PTR(offset);
                        }
                        break;
                    }
                    case EffectType_VOLUME_RAMP: {
                        u32 offset = MBIGENDIAN32(*((u32*)effectPtr));
                        u32* volumeRamp = (u32*)DATA_OFFSET_TO_PTR(offset);
                        effectPtr += 2;
                        channelCtrl->volumeRampValue = MBIGENDIAN32(*(volumeRamp++));
                        channelCtrl->volumeRampSet = (u16*)volumeRamp;
                        break;
                    }
                    case EffectType_PORTAMENTO: {
                        channelCtrl->portamentoMode = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->portamentoSpeed = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->portamentoFinalPeriod = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->portamentoDelay = MBIGENDIAN16(*(effectPtr++));
                        break;
                    }
                    case EffectType_VIBRATO: {
                        channelCtrl->vibratoMode = 1;
                        channelCtrl->vibratoPitchChangeDelay = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->vibratoCountdown = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->vibratoAdjustDown = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->vibratoAdjustUp = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->vibratoInitialDelay = MBIGENDIAN16(*(effectPtr++));
                        channelCtrl->adjustPeriodDelay = MBIGENDIAN16(*(effectPtr++));
                        break;
                    }
                    case EffectType_PORTAMENTO_CLEAR: {
                        channelCtrl->portamentoMode = 0;
                        break;
                    }
                    case EffectType_VIBRATO_CLEAR: {
                        channelCtrl->vibratoMode = 0;
                        break;
                    }
                    case EffectType_SILENCE: {
                        channelCtrl->noteTimeRemaining = MBIGENDIAN16(*(effectPtr++));
                        if (sDebugLog) {
                            MLogf("channelId: %d: cmdTimeRemaining: %d", channelId,
                                  channelCtrl->noteTimeRemaining);
                        }

                        channelCtrl->noteTimeRemaining--;
                        channelCtrl->nextEffect = effectPtr;
                        channelCtrl->volumeRamp =  (u16*)sVolumeRampBlank;
                        done = TRUE;
                        break;
                    }
                    case EffectType_STOP_SAMPLE_1:
                    case EffectType_STOP_SAMPLE_2: {
                        if (channelCtrl->channelId >= 4) {
                            Audio_StopSamplePlayingChannel(audio, channelCtrl->channelId - 4);
                        }
                        return;
                    }
                    case EffectType_LOWPASS_DISABLE: {
                        audio->lowPassFilter = 0;
                        break;
                    }
                    case EffectType_LOWPASS_ENABLE: {
                        audio->lowPassFilter = 1;
                        break;
                    }
                    case EffectType_13: {
                        channelCtrl->modEffect13Set = 1;
                        break;
                    }
                    case EffectType_JMP: {
                        u16 p = MBIGENDIAN16(*effectPtr);
                        if (p) {
                            *effectPtr = MBIGENDIAN16(p - 1);
                            effectPtr++;
                        } else {
                            effectPtr += 3;
                        }
                        u32 offset = MBIGENDIAN32(*((u32*)effectPtr));
                        effectPtr = (u16*)DATA_OFFSET_TO_PTR(offset);
                        break;
                    }
                    case EffectType_MODULE_DONE: {
                        audio->modDone = 0xff;
                        return;
                    }
                    default:
                        MLogf("Unknown command %d", effectCmd);
                        break;
                }
            } else {
                channelCtrl->periodHW = wordCode;
                channelCtrl->noteTimeRemaining = MBIGENDIAN16(*(effectPtr++));
                if (sDebugLog) {
                    MLogf("channelId: %d: period: %x cmdTimeRemaining: %d", channelId, wordCode,
                          channelCtrl->noteTimeRemaining);
                }
                channelCtrl->noteTimeRemaining--;
                channelCtrl->nextEffect = effectPtr;
                channelCtrl->volumeRamp = channelCtrl->volumeRampSet;
                channelCtrl->vibratoInitialDelay = channelCtrl->adjustPeriodDelay;
                if (channelCtrl->vibratoMode) {
                    channelCtrl->vibratoMode = 1;
                }
                HW_WRITE16(&hw->period, wordCode);
                if (channelCtrl->noteSetState) {
                    channelCtrl->noteSetState = NOTE_STATE_WRITE_NEXT;
                    WRITE_SAMPLE_PTR(&hw->pos, channelCtrl->sampleData);
                    HW_WRITE16(&hw->len, channelCtrl->sampleLen);
                }

                // AMIGA_DMACON_DMAEN must be set DMA to happen for sound channels
                Audio_ChannelControl(audio, AMIGA_CON_SET | AMIGA_DMACON_DMAEN | dmaChannel);
                done = TRUE;
            }
        } while (!done);
    }

    Audio_ChannelCtrlAdjustVolume(audio, channelId, channelCtrl, hw);
}

static void Audio_ChannelCtrlProgressForeground(AudioContext* audio, AudioChannelControl* channelCtrl, ChannelRegisters* hw, u16 channelId) {
    audio->modApplyVolumeSuppress = 1;

    Audio_ChannelProgress(audio, channelCtrl, hw, channelId, 1 << channelId);
}

static void Audio_ChannelCtrlProgressBackground(AudioContext* audio, AudioChannelControl* channelCtrl, u16 channelId) {
    audio->modApplyVolumeSuppress = 1;

    Audio_ChannelProgress(audio, channelCtrl, &audio->hwBypass, channelId, 0);
}

static void Audio_ChannelCtrlProgressSample(AudioContext* audio, AudioChannelControl* channelCtrl, ChannelRegisters* hw, u16 channelId) {
    audio->modApplyVolumeSuppress = 0;

    Audio_ChannelProgress(audio, channelCtrl, hw, channelId, 1 << channelId);
}

static void Audio_SampleSetup(AudioContext* audio, u16 channelId) {
    Audio_StopChannel(audio, audio->hw[channelId], channelId);

    u32 sampleId = audio->soundToPlayChannel[channelId];

    AudioChannelControl* channelCtrl = audio->channelCtrl + 4 + channelId;
    channelCtrl->nextEffect = Audio_GetSampleEffect(audio, sampleId);
    audio->soundToPlayChannel[channelId] = 0;
    channelCtrl->noteTimeRemaining = 0;
    channelCtrl->volumeHW = VOLUME_UNSET;
}

static void Audio_ProgressTickInternal(AudioContext* audio) {
    if (audio->modificationsInProgress) {
        return;
    }

    // Volume fade in / fade out
    if (audio->modVolumeFadeState) {
        audio->modVolumeFadeStep++;
        if (audio->modVolumeFadeState == 1) {
            if (audio->modVolumeFadeSpeed == audio->modVolumeFadeStep) {
                audio->modVolumeSuppress--;
                audio->modVolumeFadeStep = 0;
                if (audio->modVolumeSuppress <= 0) {
                    audio->modVolumeFadeState = 0;
                }
            }
        } else {
            if (audio->modVolumeFadeSpeed == audio->modVolumeFadeStep) {
                audio->modVolumeSuppress++;
                audio->modVolumeFadeStep = 0;
                if (audio->modVolumeSuppress >= 0x40) {
                    audio->modVolumeFadeState = 0;
                }
            }
        }
    }

    AudioEngineSound* engineSound = audio->engineSound;
    for (int i = 0; i < AUDIO_NUM_CHANNELS; ++i) {
        if (engineSound->inUse) {
            engineSound->volumeDecayDelay++;
            // Fade out volume on engines after delay
            if (engineSound->volumeDecayDelay >= ENGINE_SOUND_DECAY_DELAY) {
                engineSound->volume--;
                Audio_UpdateVolumeAndPeriod(audio, engineSound);
            }
            // Stop engine sound, if volume is zero
            if (engineSound->volume == 0) {
                engineSound->inUse = 0;
                engineSound->shipId = 0;
                engineSound->volumeDecayDelay = 0;
                Audio_StopTonePlayingChannel(audio, engineSound->channelId - 1);
            }
        }
        engineSound++;
    }

    for (int i = MOD_CHANNEL_PROGRESS_BEGIN; i < MOD_CHANNEL_PROGRESS_END; ++i) {
        AudioChannelControl* channelCtrl = audio->channelCtrl + i;
        if (!audio->playModOnly) {
            if (audio->soundToPlayChannel[i]) {
                Audio_SampleSetup(audio, i);
            } else {
                if (!audio->soundPlayingChannel[i]) {
                    goto modPlayingOnly;
                }
            }
            if (audio->modSilenceDuringPlayback) {
                Audio_ChannelCtrlProgressSample(audio, channelCtrl, audio->hw[i], i);
            } else {
                Audio_ChannelCtrlProgressBackground(audio, channelCtrl, i);
                Audio_ChannelCtrlProgressSample(audio, channelCtrl + 4, audio->hw[i], i);
            }
            continue;
        }

        modPlayingOnly:
        if (audio->engineSoundPlayingChannel[i] == 0) {
            if (audio->modSilenceDuringPlayback == 0) {
                Audio_ChannelCtrlProgressForeground(audio, channelCtrl, audio->hw[i], i);
            }
        } else {
            Audio_ChannelCtrlProgressBackground(audio, channelCtrl, i);
        }
    }
}

void Audio_ProgressTick(AudioContext* audio) {
    if (audio->modStartTickOffset) {
        for (int tick = 0; tick < audio->modStartTickOffset; tick++) {
            audio->modTick++;
            Audio_ProgressTickInternal(audio);
        }
        audio->modStartTickOffset = 0;
        Audio_ProgressTickInternal(audio);
    } else {
        Audio_ProgressTickInternal(audio);
    }
    audio->modTick++;
}

#ifdef AUDIO_BUFFERED_PLAYBACK

// Function to up convert audio sample to a sample buffer at the specified higher sample rate
// Returns the length samples written to the output buffer
u32 ConvertAudio8to16(i8* srcSampleBuf, u16 srcSampleSize, i16* outputSampleBuf, u32 convertedFrames) {
    i8* srcSample = srcSampleBuf;
    i16* dstSample = outputSampleBuf;
    int dout = convertedFrames - 1;
    int din = srcSampleSize - 1;
    int d = 2 * din - dout;

    for (int i = 0; i < convertedFrames; i++) {
        i16 s = ((i16)(*srcSample));
        *dstSample = (i16)(s << 8);
        if (d > 0) {
            srcSample++;
            d -= 2 * dout;
        }
        d += 2 * din;
        dstSample++;
    }

    return convertedFrames;
}

u32 ConvertAudio8to16_Linear(i8* srcSampleBuf, u32 srcSampleSize, i16* outputSampleBuf, u32 convertedFrames) {
    // Just try linear interpolation
    i8* srcSample = srcSampleBuf;
    i16* dstSample = outputSampleBuf;

    int dx = convertedFrames / (srcSampleSize-1);
    int dxa = convertedFrames % (srcSampleSize-1);
    int acc = 0;

    int prev = ((int)(*srcSample++)) << 8;
    int ext = dx;
    acc += dxa;
    for (int i = 1; i < srcSampleSize; i++) {
        int cur = ((int)(*srcSample++)) << 8;
        for (int j = 0; j < ext; j++) {
            int new = ((cur * j) + (prev * (ext - j))) / (ext);
            *dstSample++ = (i16)(new);
        }
        acc += dxa;
        if (acc >= srcSampleSize) {
            ext = dx + 1;
            acc -= srcSampleSize;
        } else {
            ext = dx;
        }
        prev = cur;
    }

    if (dstSample > outputSampleBuf + convertedFrames) {
//        dstSample -= convertedFrames;
//        asm("int $3");
        MLog("converted error");
    }

    return convertedFrames;
}

u32 ConvertAudio8to16_LinearFilter(i8* srcSampleBuf, u16 srcSampleSize, i16* outputSampleBuf, u32 convertedFrames) {
    // Linear interpolation with filter
    i8* srcSample = srcSampleBuf;
    i8* srcSampleEnd = srcSampleBuf + srcSampleSize;
    i16* dstSample = outputSampleBuf;
    int dout = convertedFrames - 1;
    int din = srcSampleSize - 1;
    int d = 2 * din - dout;

    for (int i = 0; i < convertedFrames; i++) {
        i16 s = ((i16)(*srcSample));
        *dstSample = (i16)(s << 8);
        if (d > 0) {
            srcSample++;
            d -= 2 * dout;
        }
        d += 2 * din;
        dstSample++;
    }

    return convertedFrames;
}

u32 ConvertAudio8to16_Sinc(i8* srcSampleBuf, u16 srcSampleSize, i16* outputSampleBuf, u32 convertedFrames) {
    // Linear interpolation with filter
    i8* srcSample = srcSampleBuf;
    i8* srcSampleEnd = srcSampleBuf + srcSampleSize;
    i16* dstSample = outputSampleBuf;
    int dout = convertedFrames - 1;
    int din = srcSampleSize - 1;
    int d = 2 * din - dout;

    for (int i = 0; i < convertedFrames; i++) {
        i16 s = ((i16)(*srcSample));
        *dstSample = (i16)(s << 8);
        if (d > 0) {
            srcSample++;
            d -= 2 * dout;
        }
        d += 2 * din;
        dstSample++;
    }

    return convertedFrames;
}

// Covert a source 8-bit sample at the given Amiga sound hardware period to a 16-bit sample at the device playback
// sample rate.
// Converted samples are cached, so we don't have to keep converting.
SampleConvert* Audio_ConvertSample(AudioContext* audio, ChannelRegisters* hw) {
    b32 writeOrig = TRUE;
    for (int i = 0; i < audio->sampleCacheCount; ++i) {
        SampleConvert* s = audio->sampleCache + i;
        if (s->samplePtr == hw->pos) {
            writeOrig = FALSE;
            if (s->period == hw->period) {
                s->cacheClock = audio->sampleConvertClock++;
                s->used++;
                return s;
            }
        }
    }

    SampleConvert* sampleConvert = NULL;

    if (audio->sampleCacheCount == AUDIO_SAMPLE_CACHE_SIZE) {
        SampleConvert* oldest = NULL;
        u32 earliestTick = ~0;
        for (int i = 0; i < audio->sampleCacheCount; ++i) {
            SampleConvert* s = audio->sampleCache + i;
            if (!s->used && s->cacheClock < earliestTick) {
                earliestTick = s->cacheClock;
                oldest = s;
            }
        }

        if (oldest == NULL) {
            // Unable to find
            MBreakpoint("unable to find space for sample in cache, consider making it larger / looking for logic bugs");
            MLog("unable to find space for sample in cache, consider making it larger / looking for logic bugs");
            return NULL;
        } else {
            sampleConvert = oldest;
        }
    } else {
        for (int i = 0; i < AUDIO_SAMPLE_CACHE_SIZE; ++i) {
            SampleConvert* s = audio->sampleCache + i;
            if (s->sampleConverted == NULL) {
                sampleConvert = s;
                break;
            }
        }
        audio->sampleCacheCount++;
    }

    if (sampleConvert->sampleConverted) {
        MFree(sampleConvert->sampleConverted, sampleConvert->sampleConvertedBufferSize);
        sampleConvert->sampleConverted = NULL;
    }

    // period = ticks per second / samples per second
    // samples per second = ticks per second / period
    u32 amigaFramesPerSecond = (AMIGA_CHIP_CLOCK_TICKS / hw->period);

#ifdef WASM_DIRECT
    u32 srcSamples = hw->len * 2;
    int convertedFrames = (srcSamples * AUDIO_PLAYBACK_FEQ) / amigaFramesPerSecond;
    int buffReqSize = convertedFrames * sizeof(i16);

    i16* buffer = (i16*)MMalloc(buffReqSize);
    int r = ConvertAudio8to16_Linear((i8*)hw->pos, srcSamples, buffer, convertedFrames);
    if (r != 0) {
        sampleConvert->sampleConverted = buffer;
        sampleConvert->sampleConvertedLen = convertedFrames;
        sampleConvert->sampleConvertedBufferSize = buffReqSize;
        sampleConvert->samplePtr = hw->pos;
        sampleConvert->sampleLen = hw->len;
        sampleConvert->period = hw->period;
        sampleConvert->cacheClock = audio->sampleConvertClock++;
        sampleConvert->used++;
    } else {
        sampleConvert->samplePtr = NULL;
        sampleConvert->sampleLen = 0;
        sampleConvert->period = 0;
        sampleConvert->sampleConvertedLen = 0;
        sampleConvert->sampleConvertedBufferSize = 0;
        sampleConvert->used = 0;
        MFree(buffer, buffReqSize);
    }
#else

    SDL_AudioCVT cvt;
    SDL_BuildAudioCVT(&cvt,
                      AUDIO_S8, 1, amigaFramesPerSecond,
                      AUDIO_S16, 1, AUDIO_PLAYBACK_FEQ);

    cvt.len = hw->len * 2;
    int buffReqSize = cvt.len * cvt.len_mult;
    cvt.buf = (Uint8 *) MMalloc(buffReqSize);

    memcpy(cvt.buf, hw->pos, cvt.len);

    int r = SDL_ConvertAudio(&cvt);
    if (r == 0) {
        sampleConvert->sampleConverted = (i16*)cvt.buf;
        sampleConvert->sampleConvertedLen = cvt.len_cvt / 2;
        sampleConvert->sampleConvertedBufferSize = buffReqSize;
        sampleConvert->samplePtr = hw->pos;
        sampleConvert->sampleLen = hw->len;
        sampleConvert->period = hw->period;
        sampleConvert->cacheClock = audio->sampleConvertClock++;
        sampleConvert->used++;
    } else {
        sampleConvert->samplePtr = NULL;
        sampleConvert->sampleLen = 0;
        sampleConvert->period = 0;
        sampleConvert->sampleConvertedLen = 0;
        sampleConvert->sampleConvertedBufferSize = 0;
        sampleConvert->used = 0;
        MFree(cvt.buf, buffReqSize);
    }

    if (sDebugLog) {
        char buffer[40];

        u32 sampleId = hw->pos - audio->data;
        if (writeOrig) {
            snprintf(buffer, 40, "music/orig-%x-%x.raw", sampleId, hw->len);

            MFileWriteDataFully(buffer, (u8*) hw->pos, hw->len * 2);
        }

        if (r == 0) {
            snprintf(buffer, 40, "music/mod-old-%x-%x.raw", sampleId, hw->period);

            MFileWriteDataFully(buffer, (u8*) sampleConvert->sampleConverted, cvt.len_cvt);
        }
    }
#endif
    return sampleConvert;
}

void Audio_ReloadSampleIfChanged(AudioContext* audio, u16 channelId, ChannelRegisters* hw, SampleConvert* sampleConvert) {
    if (!hw->pos || !hw->period || !hw->len) {
        return;
    }

    if (sampleConvert == NULL || hw->pos != sampleConvert->samplePtr ||
        hw->len != sampleConvert->sampleLen || hw->period != sampleConvert->period) {

        sampleConvert = Audio_ConvertSample(audio, hw);

        SampleConvert* oldSample = audio->channelSamples[channelId];
        if (oldSample) {
            oldSample->used--;
        }
        audio->channelSamples[channelId] = sampleConvert;
    }
}

void Audio_RenderInternal(AudioContext* audio, u32 numTicks, b32 bWriteFrames) {
    u32 overallSamples = (AUDIO_PLAYBACK_FEQ * numTicks) / AUDIO_TICKS_PER_SECOND;
#ifdef WASM_DIRECT
    u32 outputSize = overallSamples * sizeof(float);
#else
    u32 outputSize = overallSamples * 2 * sizeof(i16);
#endif
    if (bWriteFrames) {
        if (audio->audioOutputBufferSize < outputSize) {
#ifdef WASM_DIRECT
            audio->audioOutputBufferLeft = (float*)MRealloc(audio->audioOutputBufferLeft, audio->audioOutputBufferSize, outputSize);
            audio->audioOutputBufferRight = (float*)MRealloc(audio->audioOutputBufferRight, audio->audioOutputBufferSize, outputSize);
#else
            audio->audioOutputBuffer = (i16*)MRealloc(audio->audioOutputBuffer, audio->audioOutputBufferSize, outputSize);
#endif
            audio->audioOutputBufferSize = outputSize;
        }
        audio->audioOutputContentSize = outputSize;
    }

#ifdef WASM_DIRECT
    float* outLeft = audio->audioOutputBufferLeft;
    float* outRight = audio->audioOutputBufferRight;
#else
    i16* out = audio->audioOutputBuffer;
#endif

    i32 channelOut[4];

    for (int tick = 0; tick < numTicks; tick++) {
        audio->modTick++;
        if (sDebugLog) {
            MLogf("tick: %d %x", audio->modTick, audio->channelMask);
        }

        u16 channelMaskBefore = audio->channelMask;
        Audio_ProgressTickInternal(audio);
        u16 channelReload = (~channelMaskBefore) & audio->channelMask;

        for (int i = MOD_CHANNEL_PROGRESS_BEGIN; i < MOD_CHANNEL_PROGRESS_END; ++i) {
            SampleConvert* sampleConvert = audio->channelSamples[i];
            if (channelReload & (1 << i)) {
                audio->samplePos[i] = 0;
                Audio_ReloadSampleIfChanged(audio, i, audio->hw[i], sampleConvert);
            } else if (sampleConvert && sampleConvert->period != audio->hw[i]->period) {
                u32 oldPos = audio->samplePos[i];
                // Update the samplePos to be the approximate same sample position within the sample
                // for the new period.
                u32 newPos = (oldPos * sampleConvert->period) / audio->hw[i]->period;
                Audio_ReloadSampleIfChanged(audio, i, audio->hw[i], sampleConvert);
                audio->samplePos[i] = newPos  % audio->hw[i]->len;
            }
        }

        channelOut[0] = 0;
        channelOut[1] = 0;
        channelOut[2] = 0;
        channelOut[3] = 0;

        for (int i = 0; i < SAMPLES_PER_TICK; ++i) {
            for (int j = MOD_CHANNEL_PROGRESS_BEGIN; j < MOD_CHANNEL_PROGRESS_END; j++) {
                SampleConvert* sampleConvert = audio->channelSamples[j];
                b32 channelEnabled = audio->channelMask & (1 << j);
                if (!channelEnabled || !sampleConvert) {
                    if (i < 8) {
                        channelOut[j] = ((8 - i) * audio->lastValue[j]) / 8;
                    } else {
                        channelOut[j] = 0;
                    }
                } else if (sampleConvert->sampleConverted) {
                    if (audio->samplePos[j] > sampleConvert->sampleConvertedLen) {
                        channelOut[j] = 0;
                    } else {
                        i16* val = sampleConvert->sampleConverted + audio->samplePos[j];
                        i32 hwVolume = audio->hw[j]->volume;
                        i32 value = (((i32)*val) * hwVolume * audio->masterVolume) / AUDIO_VOLUME_MAX;
                        audio->samplePos[j]++;
                        if (audio->samplePos[j] >= sampleConvert->sampleConvertedLen) {
                            audio->samplePos[j] = 0;
                            // trigger reload when sample ends
                            Audio_ReloadSampleIfChanged(audio, j, audio->hw[j], sampleConvert);
                        }
                        channelOut[j] = value;
                    }
                } else {
                    channelOut[j] = 0;
                }
            }

            if (bWriteFrames) {
                // Write out the audio frames for left and right.  Also do some stereo mixing, so samples are not heard
                // entirely in one ear - later Amiga models did this.
#ifdef WASM_DIRECT
                float scale = 1.0 / (1L <<(
                     1 +  // two channels
                     2 +  // left right mixing
                     7 +  // volume (0-128)
                     15   // 16 bit signed sample range;
                ));
                float leftValue = (channelOut[0] + channelOut[3]) * scale;
                float rightValue = (channelOut[1] + channelOut[2]) * scale;
                *(outLeft++) = (leftValue * 3 + rightValue);
                *(outRight++) = (rightValue * 3 + leftValue);
#else
                i32 leftValue = (channelOut[0] + channelOut[3]) / VOLUME_SCALE;
                i32 rightValue = (channelOut[1] + channelOut[2]) / VOLUME_SCALE;
                *(out++) = (leftValue * 3 + rightValue) / 4;
                *(out++) = (rightValue * 3 + leftValue) / 4;
#endif
            }
        }

        audio->lastValue[0] = channelOut[0];
        audio->lastValue[1] = channelOut[1];
        audio->lastValue[2] = channelOut[2];
        audio->lastValue[3] = channelOut[3];
    }

#ifndef WASM_DIRECT
    if (bWriteFrames && sDebugLog) {
        static MFile sOutputRaw;
        if (!sOutputRaw.open) {
            sOutputRaw = MFileWriteOpen("music/output.raw");
        }
        MFileWriteData(&sOutputRaw, (u8*) audio->audioOutputBuffer, overallSamples * 4);
    }
#endif
}
#endif
