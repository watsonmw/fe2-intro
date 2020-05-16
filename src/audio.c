#ifdef USE_SDL
#include <SDL2/SDL.h>
#elif AMIGA
#include <proto/exec.h>
#endif

#include "audio.h"

#define MOD_CHANNEL_PROGRESS_BEGIN 0
#define MOD_CHANNEL_PROGRESS_END 4

#define AMIGA_CHIP_CLOCK_TICKS 3579545
#define PLAYBACK_FEQ 44100
#define SAMPLES_PER_TICK (PLAYBACK_FEQ / 50)

#define VOLUME_SCALE 128
#define AUDIO_BUFFER_SIZE 4096

#include "amigahw.h"

// First hunk offset, this is to apply any fixups to absolute address pointers
#define FILE_MAP_OFFSET 0x228
#define DATA_OFFSET_TO_PTR(n) ((u8*)(audio->data + FILE_MAP_OFFSET + n))

enum EffectEnum {
    EffectType_DONE = 0,
    EffectType_NOTE = 1 << 2,
    EffectType_NEXT_TRACK = 2 << 2,
    EffectType_VOLUME_RAMP = 3 << 2,
    EffectType_PORTAMENTO = 4 << 2,
    EffectType_VIBRATO = 5 << 2,
    EffectType_PORTAMENTO_CLEAR = 6 << 2,
    EffectType_VIBRATO_CLEAR = 7 << 2,
    EffectType_DELAY = 8 << 2,
    EffectType_STOP_AUDIO1 = 9 << 2,
    EffectType_STOP_AUDIO2 = 10 << 2,
    EffectType_LOWPASS_DISABLE = 11 << 2,
    EffectType_LOWPASS_ENABLE = 12 << 2,
    EffectType_13 = 13 << 2,
    EffectType_14 = 14 << 2,
    EffectType_FIN = 15 << 2,
};

static u16 sModInitializeEffect[2] = { MBIGENDIAN16(EffectType_NEXT_TRACK) }; // Switch track
static u16 sModInitialTracks[4] = { 0, 0, 0, 0 }; // Play first track
static u16 sVolumeRampBlank[3] = { 0, 0, MBIGENDIAN16(0xff) }; // Zero volume recurring
static b32 sModLog = FALSE;

typedef struct sAudioSample {
    u32 origOffset;
    u16 origSize;

    u8* data;
    u16 len;
} AudioSample;

static AudioSample sAudioSamples[] = {
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
    { 0x2647a, 16 }  // silence
};

static void Audio_ProgressTickInternal(AudioContext* audio);

#ifdef USE_SDL
void Audio_Render(AudioContext* audio, int ticks, b32 output);

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
    u32 ticks = ((numSamples * 50) / PLAYBACK_FEQ) + 1;

    audio->audioBytesWriten = 0;

    if (audio->modStartTickOffset) {
        Audio_Render(audio, audio->modStartTickOffset, FALSE);
        audio->modStartTickOffset = 0;
    }

    Audio_Render(audio, ticks, TRUE);

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

static void Audio_ModChannelInit(AudioContext* audio, u16 channelId);

static void Audio_CopyAndFixSamples(AudioContext* audio) {
    audio->samplesDataSize = 0x18000;
#ifdef AMIGA
    // Allocate some chip ram that will be used to store the fixed up samples
    audio->samplesData = (u8*) AllocMem(audio->samplesDataSize, MEMF_PUBLIC | MEMF_CHIP);
#else
    audio->samplesData = (u8*) MMalloc(audio->samplesDataSize);
#endif

    u8* samplesData = audio->samplesData;
    u32 numSamples = sizeof(sAudioSamples) / sizeof(AudioSample);
    for (int i = 0; i < numSamples; ++i) {
        AudioSample* audioSample = sAudioSamples + i;

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

    if (sModLog) {
        MLogf("audio chip mem required %x", samplesData - audio->samplesData);
    }
}

static u8* Audio_GetSampleData(u32 sampleOffset) {
    sampleOffset += FILE_MAP_OFFSET;
    u32 numSamples = sizeof(sAudioSamples) / sizeof(AudioSample);
    for (int i = 0; i < numSamples; ++i) {
        AudioSample* audioSample = sAudioSamples + i;

        if (audioSample->origOffset == sampleOffset) {
            return audioSample->data;
        }
    }
    MLogf("Unknown sample: %x", sampleOffset);
    return NULL;
}

u32 Audio_Init(AudioContext* audio, u8* data, u32 size) {
    memset(audio, 0, sizeof(AudioContext));

#ifdef USE_SDL
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID sdlAudioID;

    memset(&want, 0, sizeof(want));
    want.freq = PLAYBACK_FEQ;
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
#endif

    audio->data = data;
    audio->dataSize = size;

    Audio_CopyAndFixSamples(audio);

    audio->volume = 32;

    audio->mod1 = 1;

    audio->modBankSelect = 0;
    audio->modBankSelectStore = 0;

    audio->numSamples = 10;
    audio->numMods = 10;

    audio->modDefaultEffects[0] = (u16*)(audio->data + 0xc78);
    audio->modDefaultEffects[1] = (u16*)(audio->data + 0xc90);
    audio->modDefaultEffects[2] = (u16*)(audio->data + 0xca8);
    audio->modDefaultEffects[3] = (u16*)(audio->data + 0xcc0);

    for (int i = 0; i < 8; ++i) {
        audio->modChannelData[i].nextEffect = (u16*)sModInitializeEffect;
        audio->modChannelData[i].nextTrack = (u16*)sModInitialTracks;
        audio->modChannelData[i].trackList = (u16*)sModInitialTracks;
    }

#ifdef AMIGA
    ChannelRegisters* base = (ChannelRegisters*)((void*)AMIGA_AUD0_BASE);
    for (int i = 0; i < 4; i++) {
        audio->hw[i] = base + i;
    }
    HW_WRITE16(AMIGA_DMACON,AMIGA_CON_CLR | 0xf);
    HW_WRITE16(AMIGA_ADKCON,AMIGA_CON_CLR | 0xff);
#else
    for (int i = 0; i < 4; i++) {
        audio->hw[i] = audio->channelRegisters + i;
    }
#endif

#ifdef USE_SDL
    SDL_PauseAudioDevice(audio->sdlAudioID, 0);
#endif

    return 0;
}

void Audio_Exit(AudioContext* audio) {
#ifdef USE_SDL
    SDL_CloseAudioDevice(audio->sdlAudioID); audio->sdlAudioID = 0;

    if (audio->audioOutputBuffer != NULL) {
        MFree(audio->audioOutputBuffer); audio->audioOutputBuffer = NULL;
    }

    for (int i = 0; i < AUDIO_SAMPLE_CACHE_SIZE; ++i) {
        SampleConvert* s = audio->sampleCache + i;
        if (s->sampleConverted != NULL) {
            MFree(s->sampleConverted); s->sampleConverted = NULL;
        }
    }
#endif

#ifdef AMIGA
    HW_WRITE16(AMIGA_DMACON, AMIGA_CON_CLR | 0xf);

    FreeMem(audio->samplesData, audio->samplesDataSize);
#else
    MFree(audio->samplesData);
#endif
    audio->samplesData = 0;
}

// Play audio sample
void Audio_PlaySample(AudioContext* audio, int sample) {
}

void Audio_FadeIn(AudioContext* audio, u16 fadeSpeed) {
    audio->modVolumeFadeState = 1;
    audio->modVolumeFadeSpeed = fadeSpeed;
    audio->modVolumeSuppress = 0x40;
}

void Audio_ModStartAt(AudioContext* audio, int modIndex, u32 tickOffset) {
    audio->modVolumeSuppress = 0;

    audio->modInitializing = 1;
    audio->modBankSelectStore = audio->modBankSelect;
    audio->modBankSelect = 1;
    audio->modTick = 0;
    audio->modDone = 0;

    ModChannelData* modChannelData = audio->modChannelData;
    memset(modChannelData, 0, sizeof(audio->modChannelData));

    if (modIndex) {
        u32* moduleData = (u32*)(audio->data + 0x199a);
        moduleData += ((modIndex - 1) * 4);

        for (int i = 0; i < 4; ++i) {
            u32 offset = MBIGENDIAN32(*(moduleData++));
            modChannelData[i].trackList = (u16*)DATA_OFFSET_TO_PTR(offset);
            modChannelData[i].nextEffect = sModInitializeEffect;
            modChannelData[i].nextTrack = sModInitialTracks;
        }
    }

    for (int i = 1; i <= 4; ++i) {
        modChannelData[i].channelId = i;
    }

    for (int i = 8; i > 4; --i) {
        modChannelData[i].channelId = i;
    }

#ifdef USE_SDL
    for (int i = 0; i < 4; i++) {
        if (audio->channelSamples[i]) {
            audio->channelSamples[i]->used = 0;
        }
        audio->channelSamples[i] = NULL;
        audio->samplePos[i] = 0;
    }
#endif

    audio->modBankSelect = audio->modBankSelectStore;

    audio->modVolumeSuppress = 0;
    audio->modVolumeFadeState = 0;

    if (modIndex == Audio_ModEnum_FRONTIER_THEME_INTRO) {
        Audio_FadeIn(audio, 0x13);
    }

    audio->modStartTickOffset = tickOffset;

    audio->modInitializing = 0;
}

void Audio_ModStop(AudioContext* audio) {
    Audio_ModStart(audio, Audio_ModEnum_SILENCE);
}

void Audio_UpdateVolumeAndPeriod(AudioContext* audio, ModAudio* modAudio) {
    int channelId = modAudio->channelId - 1;
    if (channelId < 0 || channelId > 3) {
        return;
    }

    if (!audio->soundPlayingChannel[channelId]) {
        ChannelRegisters* hw = audio->hw[channelId];

        HW_WRITE16(&hw->volume, modAudio->volume);
        HW_WRITE16(&hw->period, modAudio->period);
    }

    modAudio->field_0 = 0;
    modAudio->field_8 = 0;
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

static void Audio_StopAudioChannel(AudioContext* audio, u16 channelId) {
    channelId = channelId - 1;
    if (channelId > 3) {
        return;
    }

    Audio_ChannelControl(audio, AMIGA_CON_CLR | (1 << channelId));
    HW_WRITE16(&audio->hw[channelId]->volume, 0);
    HW_WRITE16(&audio->hw[channelId]->period, 1);

    audio->soundPlayingChannel[channelId] = 0;
}

void Audio_ModStopChannel(AudioContext* audio, ChannelRegisters* hw, u16 channelId) {
    Audio_ChannelControl(audio, AMIGA_CON_CLR | (1 << channelId));
    HW_WRITE16(&hw->volume, 0);
    HW_WRITE16(&hw->period, 1);
}

void Audio_ModChannelAdjustVolume(AudioContext* audio, u32 channelId, ModChannelData* modChannelData, ChannelRegisters* hw) {
    u16 volume = MBIGENDIAN16(*modChannelData->volumeRamp);
    if (volume == 0xff) {
        // volume ramp end, replay last value
        modChannelData->volumeRamp -= 1;
    }

    volume = MBIGENDIAN16(*(modChannelData->volumeRamp++));

    if (audio->modApplyVolumeSuppress) {
        if (audio->modVolumeSuppress > volume) {
            volume = 0;
        } else {
            volume -= audio->modVolumeSuppress;
        }
    }

    if (modChannelData->volumeHW != volume) {
        HW_WRITE16(&hw->volume, volume);
        modChannelData->volumeHW = volume;
        if (sModLog) {
            MLogf("channelId: %d: set volume %d", channelId, hw->volume);
        }
    }
}

void Audio_ModChannelProgress(AudioContext* audio, ModChannelData* modChannelData, ChannelRegisters* hw, u16 channelId, u16 dmaChannel) {
    if (modChannelData->noteTimeRemaining) {
        if (modChannelData->noteTimeRemaining == 1) {
            if (modChannelData->noteSetState) {
                Audio_ChannelControl(audio, AMIGA_CON_CLR | dmaChannel);
            }
        } else {
            if (modChannelData->noteSetState == 3) {
                HW_WRITE32(&hw->pos, modChannelData->nextSampleData);
                HW_WRITE16(&hw->len, modChannelData->nextSampleLen);
                if (sModLog) {
                    u32 fileOffset = hw->pos - audio->data;
                    MLogf("channelId: %d: set sample1 %x %x", channelId, fileOffset, hw->len);
                }
                modChannelData->noteSetState = 1;
            }
        }

        modChannelData->noteTimeRemaining--;

        b32 periodChange = FALSE;
        u16 period = modChannelData->periodHW;
        if (modChannelData->portamentoMode) {
            if (modChannelData->portamentoDelay) {
                modChannelData->portamentoDelay--;
            } else {
                periodChange = TRUE;
                if (modChannelData->portamentoMode == 1) {
                    period += modChannelData->portamentoSpeed;
                    if (period > modChannelData->portamentoFinalPeriod) {
                        period = modChannelData->portamentoFinalPeriod;
                    }
                } else {
                    period -= modChannelData->portamentoSpeed;
                    if (period < modChannelData->portamentoFinalPeriod) {
                        period = modChannelData->portamentoFinalPeriod;
                    }
                }
            }
        }

        if (modChannelData->vibratoMode) {
            if (modChannelData->vibratoInitialDelay) {
                modChannelData->vibratoInitialDelay--;
            } else {
                if (modChannelData->vibratoPitchChangeDelay) {
                    modChannelData->vibratoPitchChangeDelay--;
                } else {
                    modChannelData->vibratoPitchChangeDelay = modChannelData->vibratoCountdown;
                    periodChange = TRUE;
                    if (modChannelData->vibratoMode > 3) {
                        period += modChannelData->vibratoAdjustDown;
                        modChannelData->vibratoMode++;
                        if (modChannelData->vibratoMode == 5) {
                            modChannelData->vibratoMode = 1;
                        }
                    } else {
                        period -= modChannelData->vibratoAdjustUp;
                        modChannelData->vibratoMode++;
                    }
                }
            }
        }

        if (periodChange) {
            HW_WRITE16(&hw->period, period);
            modChannelData->periodHW = period;
        }
    } else {
        // Process/Parse mod data
        modChannelData->mod14Set = 0;

        u16* effectPtr = modChannelData->nextEffect;

        b32 done = FALSE;
        do {
            u16 wordCode = MBIGENDIAN16(*(effectPtr++));
            if (wordCode <= 0x64) {
                u16 effectCmd = wordCode;
                if (sModLog) {
                    MLogf("channelId: %d: cmd: %d", channelId, effectCmd);
                }
                switch (effectCmd) {
                    case EffectType_DONE:
                        return;
                    case EffectType_NOTE: {
                        u32 offset = MBIGENDIAN32(*((u32*)effectPtr));
                        effectPtr += 2;
                        u16* noteData = (u16*)DATA_OFFSET_TO_PTR(offset);
                        if (*noteData == 0) {
                            modChannelData->noteSetState = MBIGENDIAN16(*(noteData++));
                            HW_WRITE32(&hw->pos, Audio_GetSampleData(MBIGENDIAN32(*((u32*)noteData))));
                            noteData += 2;
                            HW_WRITE16(&hw->len, MBIGENDIAN16(*(noteData)));
                            if (sModLog) {
                                u32 fileOffset = hw->pos - audio->data;
                                MLogf("channelId: %d, cmd: %d, set sample2 %x %x", channelId, modChannelData->noteSetState,
                                      fileOffset, hw->len);
                            }
                        } else {
                            modChannelData->noteSetState = MBIGENDIAN16(*(noteData++));
                            modChannelData->sampleData = Audio_GetSampleData(MBIGENDIAN32(*((u32*)noteData)));
                            noteData += 2;
                            modChannelData->sampleLen = MBIGENDIAN16(*(noteData++));
                            modChannelData->nextSampleData = Audio_GetSampleData(MBIGENDIAN32(*((u32*)noteData)));
                            noteData += 2;
                            modChannelData->nextSampleLen = MBIGENDIAN16(*(noteData));
                        }
                        break;
                    }
                    case EffectType_NEXT_TRACK: {
                        u32 offset = *((u32*)modChannelData->nextTrack);
                        if (offset == 0) {
                            modChannelData->nextTrack = modChannelData->trackList;
                            offset = MBIGENDIAN32(*((u32*)modChannelData->nextTrack));
                            modChannelData->nextTrack += 2;
                            effectPtr = (u16*)DATA_OFFSET_TO_PTR(offset);
                        } else {
                            offset = MBIGENDIAN32(offset);
                            modChannelData->nextTrack += 2;
                            effectPtr = (u16*)DATA_OFFSET_TO_PTR(offset);
                        }
                        break;
                    }
                    case EffectType_VOLUME_RAMP: {
                        u32 offset = MBIGENDIAN32(*((u32*)effectPtr));
                        u32* volumeRamp = (u32*)DATA_OFFSET_TO_PTR(offset);
                        effectPtr += 2;
                        modChannelData->unused1 = MBIGENDIAN32(*(volumeRamp++));
                        modChannelData->volumeRampSet = (u16*)volumeRamp;
                        break;
                    }
                    case EffectType_PORTAMENTO: {
                        modChannelData->portamentoMode = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->portamentoSpeed = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->portamentoFinalPeriod = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->portamentoDelay = MBIGENDIAN16(*(effectPtr++));
                        break;
                    }
                    case EffectType_VIBRATO: {
                        modChannelData->vibratoMode = 1;
                        modChannelData->vibratoPitchChangeDelay = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->vibratoCountdown = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->vibratoAdjustDown = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->vibratoAdjustUp = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->vibratoInitialDelay = MBIGENDIAN16(*(effectPtr++));
                        modChannelData->adjustPeriodDelay = MBIGENDIAN16(*(effectPtr++));
                        break;
                    }
                    case EffectType_PORTAMENTO_CLEAR: {
                        modChannelData->portamentoMode = 0;
                        break;
                    }
                    case EffectType_VIBRATO_CLEAR: {
                        modChannelData->vibratoMode = 0;
                        break;
                    }
                    case EffectType_DELAY: {
                        modChannelData->noteTimeRemaining = MBIGENDIAN16(*(effectPtr++));
                        if (sModLog) {
                            MLogf("channelId: %d: cmdTimeRemaining: %d", channelId,
                                  modChannelData->noteTimeRemaining);
                        }

                        modChannelData->noteTimeRemaining--;
                        modChannelData->nextEffect = effectPtr;
                        modChannelData->volumeRamp = sVolumeRampBlank;
                        done = TRUE;
                        break;
                    }
                    case EffectType_STOP_AUDIO1:
                    case EffectType_STOP_AUDIO2: {
                        if (modChannelData->channelId == 6) {
                            Audio_StopAudioChannel(audio, 4);
                        } else if (modChannelData->channelId == 5) {
                            Audio_StopAudioChannel(audio, 3);
                        } else if (modChannelData->channelId == 7) {
                            Audio_StopAudioChannel(audio, 2);
                        } else if (modChannelData->channelId == 8) {
                            Audio_StopAudioChannel(audio, 1);
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
                        modChannelData->mod14Set = 1;
                        break;
                    }
                    case EffectType_14: {
                        u16 p = MBIGENDIAN16(*effectPtr);

                        if (p) {
                            (*effectPtr) = MBIGENDIAN16(p - 1);
                            effectPtr++;
                        } else {
                            effectPtr += 3;
                        }
                        effectPtr = (u16*)DATA_OFFSET_TO_PTR(*((u32*)effectPtr));
                        break;
                    }
                    case EffectType_FIN: {
                        audio->modDone = 0xff;
                        return;
                    }
                    default:
                        MLogf("Unknown command %d", effectCmd);
                        break;
                }
            } else {
                modChannelData->periodHW = wordCode;
                modChannelData->noteTimeRemaining = MBIGENDIAN16(*(effectPtr++));
                if (sModLog) {
                    MLogf("channelId: %d: period: %x cmdTimeRemaining: %d", channelId, wordCode,
                          modChannelData->noteTimeRemaining);
                }
                modChannelData->noteTimeRemaining--;
                modChannelData->nextEffect = effectPtr;
                modChannelData->volumeRamp = modChannelData->volumeRampSet;
                modChannelData->vibratoInitialDelay = modChannelData->adjustPeriodDelay;
                if (modChannelData->vibratoMode) {
                    modChannelData->vibratoMode = 1;
                }
                HW_WRITE16(&hw->period, wordCode);
                if (modChannelData->noteSetState) {
                    modChannelData->noteSetState = 3;
                    HW_WRITE32(&hw->pos, modChannelData->sampleData);
                    HW_WRITE16(&hw->len, modChannelData->sampleLen);
                }

                // AMIGA_DMACON_DMAEN must be set DMA to happen for sound channels
                Audio_ChannelControl(audio, AMIGA_CON_SET | AMIGA_DMACON_DMAEN | dmaChannel);
                done = TRUE;
            }
        } while (!done);
    }

    Audio_ModChannelAdjustVolume(audio, channelId, modChannelData, hw);
}

void Audio_ModChannelProgressBank1(AudioContext* audio, ModChannelData* modChannelData, ChannelRegisters* hw, u16 channelId) {
    audio->modApplyVolumeSuppress = 1;

    Audio_ModChannelProgress(audio, modChannelData, hw, channelId, 1 << channelId);
}

void Audio_ModChannelProgressBack1Off(AudioContext* audio, ModChannelData* modChannelData, u16 channelId) {
    audio->modApplyVolumeSuppress = 1;

    Audio_ModChannelProgress(audio, modChannelData, &audio->hwBypass, channelId, 0);
}

void Audio_ModChannelProgressBack2(AudioContext* audio, ModChannelData* modChannelData, ChannelRegisters* hw, u16 channelId) {
    audio->modApplyVolumeSuppress = 0;

    Audio_ModChannelProgress(audio, modChannelData, hw, channelId, 1 << channelId);
}

static void Audio_ModChannelInit(AudioContext* audio, u16 channelId) {
    if (channelId == 0) {
        Audio_ModStopChannel(audio, audio->hw[channelId], channelId);

        ModAudio* modAudio = audio->modAudio;

        for (int i = 0; i < 4; ++i) {
            if (modAudio->channelId == channelId + 1) {
                modAudio->field_0 = 0;
                modAudio->field_8 = 0;
            }
            modAudio++;
        }
    }

    ModChannelData* modData = audio->modChannelData + 4 + channelId;
    modData->nextEffect = audio->modDefaultEffects[channelId];
    audio->modChannel[channelId] = 0;
    modData->noteTimeRemaining = 0;
    modData->volumeHW = 0xff;
}

static void Audio_ProgressTickInternal(AudioContext* audio) {
    if (audio->modInitializing) {
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

    ModAudio* modAudio = audio->modAudio;
    for (int i = MOD_CHANNEL_PROGRESS_BEGIN; i < MOD_CHANNEL_PROGRESS_END; ++i) {
        if (modAudio->field_0) {
            modAudio->field_10++;
            if (modAudio->field_10 >= 0x33) {
                modAudio->volume--;
                Audio_UpdateVolumeAndPeriod(audio, modAudio);
            }
        }
        modAudio++;
    }

    modAudio = audio->modAudio;
    for (int i = MOD_CHANNEL_PROGRESS_BEGIN; i < MOD_CHANNEL_PROGRESS_END; ++i) {
        if (modAudio->field_0) {
            if (modAudio->volume > 1) {
                modAudio->field_0 = 0;
                modAudio->field_8 = 0;
                modAudio->field_10 = 0;
                Audio_StopAudioChannel(audio, modAudio->channelId);
            }
        }
        modAudio++;
    }

    for (int i = MOD_CHANNEL_PROGRESS_BEGIN; i < MOD_CHANNEL_PROGRESS_END; ++i) {
        ModChannelData* modChannelData = audio->modChannelData + i;
        if (audio->mod1) {
            goto checkOther;
        } else {
            if (audio->modChannel[i]) {
                Audio_ModChannelInit(audio, i);
            } else {
                if (!audio->soundPlayingChannel[i]) {
                    goto checkOther;
                }
            }
            if (audio->modBankSelect) {
                Audio_ModChannelProgressBack2(audio, modChannelData + 4, audio->hw[i], i);
            } else {
                Audio_ModChannelProgressBack1Off(audio, modChannelData, i);
                Audio_ModChannelProgressBack2(audio, modChannelData + 4, audio->hw[i], i);
            }
            continue;
        }

checkOther:
        if (audio->channelClear[i] == 0) {
            if (audio->modBankSelect == 0) {
                Audio_ModChannelProgressBank1(audio, modChannelData, audio->hw[i], i);
            }
        } else {
            Audio_ModChannelProgressBack1Off(audio, modChannelData, i);
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

#ifdef USE_SDL

// Covert an source 8-bit sample at the given AMIGA sound hardware period to a 16bit sample at the device playback
// sample rate.
// Converted samples are cached, so we dont have to keep converting.
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
        MFree(sampleConvert->sampleConverted); sampleConvert->sampleConverted = NULL;
    }

    // period = ticks per second / samples per second
    // samples per second = ticks per second / period
    u32 samplesPerSecond = (AMIGA_CHIP_CLOCK_TICKS / hw->period);

    SDL_AudioCVT cvt;
    SDL_BuildAudioCVT(&cvt,
                      AUDIO_S8, 1, samplesPerSecond,
                      AUDIO_S16, 1, PLAYBACK_FEQ);

    cvt.len = hw->len * 2;
    cvt.buf = (Uint8 *) MMalloc(cvt.len * cvt.len_mult);

    // Sample quality is pretty bad, do some basic cleanups to reduce clicking.
    // Some samples are changed to correctly line up phase when looping.
    memcpy(cvt.buf, hw->pos, cvt.len);

    int r = SDL_ConvertAudio(&cvt);
    if (r == 0) {
        sampleConvert->sampleConverted = (i16*)cvt.buf;
        sampleConvert->sampleConvertedLen = cvt.len_cvt / 2;
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
        sampleConvert->used = 0;
    }

    if (sModLog) {
        char buffer[40];

        u32 sampleId = hw->pos - audio->data;
        if (writeOrig) {
            snprintf(buffer, 40, "music/orig-%x-%x.raw", sampleId, hw->len);

            MFileWriteDataFully(buffer, (u8*) hw->pos, hw->len * 2);
        }

        if (r == 0) {
            snprintf(buffer, 40, "music/mod-%x-%x.raw", sampleId, hw->period);

            MFileWriteDataFully(buffer, (u8*) sampleConvert->sampleConverted, cvt.len_cvt);
        }
    }

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

void Audio_Render(AudioContext* audio, int ticks, b32 output) {
    u32 overallSamples = (PLAYBACK_FEQ * ticks) / 50;
    u32 outputSize = overallSamples * 2 * 2;

    if (output) {
        if (audio->audioOutputBufferSize < outputSize) {
            audio->audioOutputBuffer = (i16*)MRealloc(audio->audioOutputBuffer, outputSize);
            audio->audioOutputBufferSize = outputSize;
        }
        audio->audioOutputContentSize = outputSize;
    }

    i16* out = audio->audioOutputBuffer;

    i32 channelOut[4];

    for (int tick = 0; tick < ticks; tick++) {
        audio->modTick++;
        if (sModLog) {
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
                // todo: either period is wrong or this calculation is wrong, sounds bad.
                u32 oldPos = audio->samplePos[i];
                u32 newPos = (oldPos * sampleConvert->period) / audio->hw[i]->period;
                Audio_ReloadSampleIfChanged(audio, i, audio->hw[i], sampleConvert);
                audio->samplePos[i] = newPos;
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
                        i32 value = (((i32)*val) * hwVolume * audio->volume) / AUDIO_VOL_MAX;
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

            if (output) {
                i32 leftValue = (channelOut[0] + channelOut[3]) / VOLUME_SCALE;
                i32 rightValue = (channelOut[1] + channelOut[2]) / VOLUME_SCALE;

                // Do some stereo mixing, it sounds a little fuller.
                *(out++) = (leftValue * 3 + rightValue) / 4;
                *(out++) = (rightValue * 3 + leftValue) / 4;

//                *(out++) = leftValue;
//                *(out++) = rightValue;
            }
        }

        audio->lastValue[0] = channelOut[0];
        audio->lastValue[1] = channelOut[1];
        audio->lastValue[2] = channelOut[2];
        audio->lastValue[3] = channelOut[3];
    }

    if (output && sModLog) {
        MFileWriteDataFully("music/output.raw", (u8*) audio->audioOutputBuffer, overallSamples * 4);
    }
}
#endif
