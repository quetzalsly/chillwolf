//
// ID Engine
// ID_SD.c - Sound Manager for Wolfenstein 3D
// v1.2
// By Jason Blochowiak
//
// This module handles dealing with generating sound on the appropriate
// hardware
//

#include "wl_def.h"

#ifdef _WIN32
#include "SDL_mixer.h"
#elif __linux__
#include <SDL/SDL_mixer.h>
#else
#include <SDL/SDL_mixer.h>
#endif

#include "fmopl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#pragma hdrstop

#define ORIGSAMPLERATE 7042

typedef struct
{
    char RIFF[4];
    longword filelenminus8;
    char WAVE[4];
    char fmt_[4];
    longword formatlen;
    word val0x0001;
    word channels;
    longword samplerate;
    longword bytespersec;
    word bytespersample;
    word bitspersample;
} headchunk;

typedef struct
{
    char chunkid[4];
    longword chunklength;
} wavechunk;

typedef struct
{
    uint32_t startpage;
    uint32_t length;
} digiinfo;

static Mix_Chunk *SoundChunks[STARTMUSIC - STARTDIGISOUNDS];
static byte *SoundBuffers[STARTMUSIC - STARTDIGISOUNDS];

globalsoundpos channelSoundPos[MIX_CHANNELS];

//
// Global variables
//

boolean AdLibPresent;
boolean SoundBlasterPresent;
boolean SBProPresent;
boolean SoundPositioned;

SDMode SoundMode;
SMMode MusicMode;
SDSMode DigiMode;

static byte **SoundTable;

int DigiMap[LASTSOUND];
int DigiChannel[STARTMUSIC - STARTDIGISOUNDS];

//
// Internal variables
//

static boolean SD_Started;
static boolean nextsoundpos;

static soundnames SoundNumber;
static soundnames DigiNumber;

static word SoundPriority;
static word DigiPriority;

static int LeftPosition;
static int RightPosition;

word NumDigi;

static digiinfo *DigiList;
static boolean DigiPlaying;

//
// PC Sound variables
//

static byte * volatile pcSound;
static volatile longword pcLengthLeft;
static volatile byte pcLastSample;
static volatile boolean pcActive;
static volatile boolean pcForceSpeakerUpdate;

static longword pcPhaseTick;
static longword pcPhaseLength;
static Sint16 pcOutputLevel;

static int pcServiceSamplesLeft;
static int pcServiceSamplesRemainder;

//
// AdLib variables
//

static byte * volatile alSound;
static byte alBlock;
static longword alLengthLeft;
static longword alTimeCount;
static Instrument alZeroInst;

//
// Sequencer variables
//

static volatile boolean sqActive;
static word *sqHack;
static word *sqHackPtr;
static int sqHackLen;
static int sqHackSeqLen;
static longword sqHackTime;

//
// IMF / OPL state
//

static int numreadysamples;
static byte *curAlSound;
static byte *curAlSoundPtr;
static longword curAlLengthLeft;
static int soundTimeCounter = 5;
static int samplesPerMusicTick;

static void SDL_SoundFinished(void)
{
    SoundNumber = (soundnames)0;
    SoundPriority = 0;
}

/*
=============================================================================

    PC SPEAKER EMULATOR

    Wolfenstein 3D PC speaker sound effects are byte streams serviced at
    140 Hz. A non-zero byte represents the PIT reload value divided by 60.
    This emulates the original PIT-driven square wave and mixes it into the
    existing SDL_mixer signed 16-bit stereo stream.

=============================================================================
*/

#define PC_BASE_TIMER       1193181
#define PC_SERVICE_RATE     140
#define PC_SQUARE_WAVE_AMP  0x2000

static void SDL_ResetPCSpeakerMixer(void)
{
    pcSound = 0;
    pcLengthLeft = 0;
    pcLastSample = 0;
    pcActive = false;
    pcForceSpeakerUpdate = true;

    pcPhaseTick = 0;
    pcPhaseLength = 1;
    pcOutputLevel = PC_SQUARE_WAVE_AMP;

    pcServiceSamplesLeft = 0;
    pcServiceSamplesRemainder = 0;
}

static int SDL_PCGetNextTickSampleCount(void)
{
    int samples = param_samplerate / PC_SERVICE_RATE;

    pcServiceSamplesRemainder += param_samplerate % PC_SERVICE_RATE;

    if(pcServiceSamplesRemainder >= PC_SERVICE_RATE)
    {
        samples++;
        pcServiceSamplesRemainder -= PC_SERVICE_RATE;
    }

    if(samples < 1)
    {
        samples = 1;
    }

    return samples;
}

static void SDL_turnOnPCSpeaker(byte pcSample)
{
    pcPhaseLength = ((longword)pcSample * 60 * param_samplerate) / (2 * PC_BASE_TIMER);

    if(pcPhaseLength < 1)
    {
        pcPhaseLength = 1;
    }

    pcActive = true;
}

static void SDL_turnOffPCSpeaker(void)
{
    pcActive = false;
    pcPhaseTick = 0;
    pcOutputLevel = PC_SQUARE_WAVE_AMP;
}

static void SDL_SetPCSpeakerSample(byte sample)
{
    if(!pcForceSpeakerUpdate && sample == pcLastSample)
    {
        return;
    }

    pcForceSpeakerUpdate = false;
    pcLastSample = sample;

    if(sample)
    {
        SDL_turnOnPCSpeaker(sample);
    }
    else
    {
        SDL_turnOffPCSpeaker();
    }
}

static void SDL_PCService(void)
{
    if(pcSound && pcLengthLeft)
    {
        byte sample = *pcSound++;

        pcLengthLeft--;

        SDL_SetPCSpeakerSample(sample);

        if(!pcLengthLeft)
        {
            pcSound = 0;
            SoundNumber = (soundnames)0;
            SoundPriority = 0;
            SDL_turnOffPCSpeaker();
        }
    }
    else
    {
        SDL_turnOffPCSpeaker();
    }
}

static Sint16 SDL_SaturatingAdd16(Sint16 sample, Sint32 add)
{
    Sint32 mixed = (Sint32)sample + add;

    if(mixed < -32768)
    {
        mixed = -32768;
    }
    else if(mixed > 32767)
    {
        mixed = 32767;
    }

    return (Sint16)mixed;
}

static void SDL_PCMixCallback(void *udata, Uint8 *stream, int len)
{
    Sint16 *stream16 = (Sint16 *)(void *)stream;
    int samples = len / (sizeof(Sint16) * 2);

    (void)udata;

    if(SoundMode != sdm_PC && !pcSound && !pcActive)
    {
        return;
    }

    while(samples--)
    {
        if(pcServiceSamplesLeft <= 0)
        {
            SDL_PCService();
            pcServiceSamplesLeft += SDL_PCGetNextTickSampleCount();
        }

        Sint16 value = 0;

        if(pcActive)
        {
            value = pcOutputLevel;

            pcPhaseTick++;

            if(pcPhaseTick >= pcPhaseLength)
            {
                pcPhaseTick = 0;
                pcOutputLevel = -pcOutputLevel;
            }
        }

        stream16[0] = SDL_SaturatingAdd16(stream16[0], value);
        stream16[1] = SDL_SaturatingAdd16(stream16[1], value);

        stream16 += 2;
        pcServiceSamplesLeft--;
    }
}

static void SDL_PCPlaySound(PCSound *sound)
{
    if(!sound)
    {
        return;
    }

    SDL_LockAudio();

    SDL_ResetPCSpeakerMixer();

    pcLengthLeft = sound->common.length;
    pcSound = sound->data;
    pcForceSpeakerUpdate = true;

    SDL_UnlockAudio();
}

static void SDL_PCStopSound(void)
{
    SDL_LockAudio();
    SDL_ResetPCSpeakerMixer();
    SDL_UnlockAudio();
}

static void SDL_ShutPC(void)
{
    SDL_PCStopSound();
}

/*
=============================================================================

    DIGITIZED SOUND

=============================================================================
*/

void SD_StopDigitized(void)
{
    DigiPlaying = false;
    DigiNumber = (soundnames)0;
    DigiPriority = 0;
    SoundPositioned = false;

    if((DigiMode == sds_PC) && (SoundMode == sdm_PC))
    {
        SDL_SoundFinished();
    }

    switch(DigiMode)
    {
        case sds_PC:
            SDL_PCStopSound();
            break;

        case sds_SoundBlaster:
            Mix_HaltChannel(-1);
            break;

        default:
            break;
    }
}

int SD_GetChannelForDigi(int which)
{
    if(DigiChannel[which] != -1)
    {
        return DigiChannel[which];
    }

    int channel = Mix_GroupAvailable(1);

    if(channel == -1)
    {
        channel = Mix_GroupOldest(1);
    }

    if(channel == -1)
    {
        return Mix_GroupAvailable(1);
    }

    return channel;
}

void SD_SetPosition(int channel, int leftpos, int rightpos)
{
    if((leftpos < 0) || (leftpos > 15) || (rightpos < 0) || (rightpos > 15)
        || ((leftpos == 15) && (rightpos == 15)))
    {
        Quit("SD_SetPosition: Illegal position");
    }

    switch(DigiMode)
    {
        case sds_SoundBlaster:
            Mix_SetPanning(channel, ((15 - leftpos) << 4) + 15, ((15 - rightpos) << 4) + 15);
            break;

        default:
            break;
    }
}

static Sint16 GetSample(float csample, byte *samples, int size)
{
    float s0 = 0;
    float s1 = 0;
    float s2 = 0;

    int cursample = (int)csample;
    float sf = csample - (float)cursample;

    if(cursample - 1 >= 0)
    {
        s0 = (float)(samples[cursample - 1] - 128);
    }

    s1 = (float)(samples[cursample] - 128);

    if(cursample + 1 < size)
    {
        s2 = (float)(samples[cursample + 1] - 128);
    }

    float val = s0 * sf * (sf - 1) / 2 - s1 * (sf * sf - 1) + s2 * (sf + 1) * sf / 2;
    int32_t intval = (int32_t)(val * 256);

    if(intval < -32768)
    {
        intval = -32768;
    }
    else if(intval > 32767)
    {
        intval = 32767;
    }

    return (Sint16)intval;
}

void SD_PrepareSound(int which)
{
    if(DigiList == NULL)
    {
        Quit("SD_PrepareSound(%i): DigiList not initialized!\n", which);
    }

    if(which < 0 || which >= NumDigi)
    {
        Quit("SD_PrepareSound(%i): Bad sound number!\n", which);
    }

    if(SoundChunks[which] != NULL)
    {
        return;
    }

    int page = DigiList[which].startpage;
    int size = DigiList[which].length;

    byte *origsamples = PM_GetSound(page);

    if(origsamples + size >= PM_GetEnd())
    {
        Quit("SD_PrepareSound(%i): Sound reaches out of page file!\n", which);
    }

    int destsamples = (int)((float)size * (float)param_samplerate / (float)ORIGSAMPLERATE);

    byte *wavebuffer = (byte *)malloc(sizeof(headchunk) + sizeof(wavechunk) + destsamples * 2);

    if(wavebuffer == NULL)
    {
        Quit("Unable to allocate wave buffer for sound %i!\n", which);
    }

    headchunk head =
    {
        {'R','I','F','F'},
        0,
        {'W','A','V','E'},
        {'f','m','t',' '},
        0x10,
        0x0001,
        1,
        (longword)param_samplerate,
        (longword)param_samplerate * 2,
        2,
        16
    };

    wavechunk dhead =
    {
        {'d', 'a', 't', 'a'},
        (longword)destsamples * 2
    };

    head.filelenminus8 = sizeof(head) + destsamples * 2;

    memcpy(wavebuffer, &head, sizeof(head));
    memcpy(wavebuffer + sizeof(head), &dhead, sizeof(dhead));

    Sint16 *newsamples = (Sint16 *)(void *)(wavebuffer + sizeof(headchunk) + sizeof(wavechunk));

    for(int i = 0; i < destsamples; i++)
    {
        newsamples[i] = GetSample((float)size * (float)i / (float)destsamples, origsamples, size);
    }

    SoundBuffers[which] = wavebuffer;
    SoundChunks[which] = Mix_LoadWAV_RW
    (
        SDL_RWFromMem(wavebuffer, sizeof(headchunk) + sizeof(wavechunk) + destsamples * 2),
        1
    );
}

int SD_PlayDigitized(word which, int leftpos, int rightpos)
{
    if(!DigiMode)
    {
        return 0;
    }

    if(which >= NumDigi)
    {
        Quit("SD_PlayDigitized: bad sound number %i", which);
    }

    int channel = SD_GetChannelForDigi(which);

    SD_SetPosition(channel, leftpos, rightpos);

    DigiPlaying = true;

    if(SoundChunks[which] == NULL)
    {
        SD_PrepareSound(which);
    }

    Mix_Chunk *sample = SoundChunks[which];

    if(sample == NULL)
    {
        printf("SoundChunks[%i] is NULL!\n", which);
        return 0;
    }

    if(Mix_PlayChannel(channel, sample, 0) == -1)
    {
        printf("Unable to play sound: %s\n", Mix_GetError());
        return 0;
    }

    return channel;
}

void SD_ChannelFinished(int channel)
{
    if(channel >= 0 && channel < MIX_CHANNELS)
    {
        channelSoundPos[channel].valid = 0;
    }
}

void SD_SetDigiDevice(SDSMode mode)
{
    boolean devicenotpresent;

    if(mode == DigiMode)
    {
        return;
    }

    SD_StopDigitized();

    devicenotpresent = false;

    switch(mode)
    {
        case sds_SoundBlaster:
            if(!SoundBlasterPresent)
            {
                devicenotpresent = true;
            }
            break;

        case sds_Off:
        case sds_PC:
            break;

        default:
            devicenotpresent = true;
            break;
    }

    if(!devicenotpresent)
    {
        DigiMode = mode;
    }
}

static void SDL_SetupDigi(void)
{
    word *soundInfoPage = (word *)(void *)PM_GetPage(ChunksInFile - 1);

    NumDigi = (word)PM_GetPageSize(ChunksInFile - 1) / 4;
    DigiList = (digiinfo *)malloc(NumDigi * sizeof(digiinfo));

    if(DigiList == NULL)
    {
        Quit("Unable to allocate DigiList!");
    }

    int i;

    for(i = 0; i < NumDigi; i++)
    {
        DigiList[i].startpage = soundInfoPage[i * 2];

        if((int)DigiList[i].startpage >= ChunksInFile - 1)
        {
            NumDigi = i;
            break;
        }

        int lastPage;

        if(i < NumDigi - 1)
        {
            lastPage = soundInfoPage[i * 2 + 2];

            if(lastPage == 0 || lastPage + PMSoundStart > ChunksInFile - 1)
            {
                lastPage = ChunksInFile - 1;
            }
            else
            {
                lastPage += PMSoundStart;
            }
        }
        else
        {
            lastPage = ChunksInFile - 1;
        }

        int size = 0;

        for(int page = PMSoundStart + DigiList[i].startpage; page < lastPage; page++)
        {
            size += PM_GetPageSize(page);
        }

        if(lastPage == ChunksInFile - 1 && PMSoundInfoPagePadded)
        {
            size--;
        }

        if((size & 0xffff0000) != 0 && (size & 0xffff) < soundInfoPage[i * 2 + 1])
        {
            size -= 0x10000;
        }

        size = (size & 0xffff0000) | soundInfoPage[i * 2 + 1];

        DigiList[i].length = size;
    }

    for(i = 0; i < LASTSOUND; i++)
    {
        DigiMap[i] = -1;
        DigiChannel[i] = -1;
    }
}

/*
=============================================================================

    ADLIB SOUND EFFECTS

=============================================================================
*/

static void SDL_ALStopSound(void)
{
    alSound = 0;
    alOut(alFreqH + 0, 0);
}

static void SDL_AlSetFXInst(Instrument *inst)
{
    byte c;
    byte m;

    m = 0;
    c = 3;

    alOut(m + alChar, inst->mChar);
    alOut(m + alScale, inst->mScale);
    alOut(m + alAttack, inst->mAttack);
    alOut(m + alSus, inst->mSus);
    alOut(m + alWave, inst->mWave);

    alOut(c + alChar, inst->cChar);
    alOut(c + alScale, inst->cScale);
    alOut(c + alAttack, inst->cAttack);
    alOut(c + alSus, inst->cSus);
    alOut(c + alWave, inst->cWave);

    alOut(alFeedCon, 0);
}

static void SDL_ALPlaySound(AdLibSound *sound)
{
    SDL_ALStopSound();

    alLengthLeft = sound->common.length;

    byte *data = sound->data;
    Instrument *inst = &sound->inst;

    alBlock = ((sound->block & 7) << 2) | 0x20;

    if(!(inst->mSus | inst->cSus))
    {
        Quit("SDL_ALPlaySound() - Bad instrument");
    }

    SDL_AlSetFXInst(inst);

    alSound = (byte *)data;
}

static void SDL_ShutAL(void)
{
    alSound = 0;
    alOut(alEffects, 0);
    alOut(alFreqH + 0, 0);
    SDL_AlSetFXInst(&alZeroInst);
}

static void SDL_CleanAL(void)
{
    alOut(alEffects, 0);

    for(int i = 1; i < 0xf5; i++)
    {
        alOut(i, 0);
    }
}

static void SDL_StartAL(void)
{
    alOut(alEffects, 0);
    SDL_AlSetFXInst(&alZeroInst);
}

static boolean SDL_DetectAdLib(void)
{
    for(int i = 1; i <= 0xf5; i++)
    {
        alOut(i, 0);
    }

    alOut(1, 0x20);

    return true;
}

static void SDL_AdvanceAdLibSound(void)
{
    if(curAlSound != alSound)
    {
        curAlSound = curAlSoundPtr = alSound;
        curAlLengthLeft = alLengthLeft;
    }

    if(curAlSound)
    {
        if(*curAlSoundPtr)
        {
            alOut(alFreqL + 0, *curAlSoundPtr);
            alOut(alFreqH + 0, alBlock);
        }
        else
        {
            alOut(alFreqH + 0, 0);
        }

        curAlSoundPtr++;
        curAlLengthLeft--;

        if(!curAlLengthLeft)
        {
            alSound = 0;
            curAlSound = 0;
            curAlSoundPtr = 0;
            SoundNumber = (soundnames)0;
            SoundPriority = 0;
            alOut(alFreqH + 0, 0);
        }
    }
}

/*
=============================================================================

    DEVICE CONTROL

=============================================================================
*/

static void SDL_ShutDevice(void)
{
    switch(SoundMode)
    {
        case sdm_PC:
            SDL_ShutPC();
            break;

        case sdm_AdLib:
            SDL_ShutAL();
            break;

        default:
            break;
    }

    SoundMode = sdm_Off;
}

static void SDL_CleanDevice(void)
{
    if((SoundMode == sdm_AdLib) || (MusicMode == smm_AdLib))
    {
        SDL_CleanAL();
    }
}

static void SDL_StartDevice(void)
{
    switch(SoundMode)
    {
        case sdm_PC:
            SDL_ResetPCSpeakerMixer();
            break;

        case sdm_AdLib:
            SDL_StartAL();
            break;

        default:
            break;
    }

    SoundNumber = (soundnames)0;
    SoundPriority = 0;
}

/*
=============================================================================

    PUBLIC SOUND MODE ROUTINES

=============================================================================
*/

boolean SD_SetSoundMode(SDMode mode)
{
    boolean result = false;
    word tableoffset = STARTADLIBSOUNDS;

    SD_StopSound();

    if((mode == sdm_AdLib) && !AdLibPresent)
    {
        mode = sdm_PC;
    }

    switch(mode)
    {
        case sdm_Off:
            tableoffset = STARTADLIBSOUNDS;
            result = true;
            break;

        case sdm_PC:
            tableoffset = STARTPCSOUNDS;
            result = true;
            break;

        case sdm_AdLib:
            tableoffset = STARTADLIBSOUNDS;

            if(AdLibPresent)
            {
                result = true;
            }

            break;

        default:
            Quit("SD_SetSoundMode: Invalid sound mode %i", mode);
            return false;
    }

    SoundTable = &audiosegs[tableoffset];

    if(result && (mode != SoundMode))
    {
        SDL_ShutDevice();
        SoundMode = mode;
        SDL_StartDevice();
    }

    return result;
}

boolean SD_SetMusicMode(SMMode mode)
{
    boolean result = false;

    SD_FadeOutMusic();

    while(SD_MusicPlaying())
    {
        SDL_Delay(5);
    }

    switch(mode)
    {
        case smm_Off:
            result = true;
            break;

        case smm_AdLib:
            if(AdLibPresent)
            {
                result = true;
            }
            break;
    }

    if(result)
    {
        MusicMode = mode;
    }

    return result;
}

/*
=============================================================================

    MUSIC CALLBACK

=============================================================================
*/

void SDL_IMFMusicPlayer(void *udata, Uint8 *stream, int len)
{
    int stereolen = len >> 1;
    int sampleslen = stereolen >> 1;
    Sint16 *stream16 = (Sint16 *)(void *)stream;

    (void)udata;

    while(sampleslen > 0)
    {
        if(numreadysamples)
        {
            if(numreadysamples < sampleslen)
            {
                YM3812UpdateOne(0, stream16, numreadysamples);

                stream16 += numreadysamples * 2;
                sampleslen -= numreadysamples;
                numreadysamples = 0;
            }
            else
            {
                YM3812UpdateOne(0, stream16, sampleslen);

                numreadysamples -= sampleslen;
                return;
            }
        }

        soundTimeCounter--;

        if(!soundTimeCounter)
        {
            soundTimeCounter = 5;
            SDL_AdvanceAdLibSound();
        }

        if(sqActive && sqHackPtr)
        {
            do
            {
                if(sqHackTime > alTimeCount)
                {
                    break;
                }

                sqHackTime = alTimeCount + *(sqHackPtr + 1);

                alOut(*(byte *)sqHackPtr, *(((byte *)sqHackPtr) + 1));

                sqHackPtr += 2;
                sqHackLen -= 4;
            }
            while(sqHackLen > 0);

            alTimeCount++;

            if(!sqHackLen)
            {
                sqHackPtr = sqHack;
                sqHackLen = sqHackSeqLen;
                sqHackTime = 0;
                alTimeCount = 0;
            }
        }

        numreadysamples = samplesPerMusicTick;
    }
}

/*
=============================================================================

    STARTUP / SHUTDOWN

=============================================================================
*/

void SD_Startup(void)
{
    if(SD_Started)
    {
        return;
    }

    memset(SoundChunks, 0, sizeof(SoundChunks));
    memset(SoundBuffers, 0, sizeof(SoundBuffers));
    memset(channelSoundPos, 0, sizeof(channelSoundPos));

    if(Mix_OpenAudio(param_samplerate, AUDIO_S16, 2, param_audiobuffer))
    {
        printf("Unable to open audio: %s\n", Mix_GetError());
        return;
    }

    Mix_ReserveChannels(2);
    Mix_GroupChannels(2, MIX_CHANNELS - 1, 1);

    samplesPerMusicTick = param_samplerate / 700;

    if(samplesPerMusicTick < 1)
    {
        samplesPerMusicTick = 1;
    }

    if(YM3812Init(1, 3579545, param_samplerate))
    {
        printf("Unable to create virtual OPL!!\n");
    }

    for(int i = 1; i < 0xf6; i++)
    {
        YM3812Write(0, i, 0);
    }

    YM3812Write(0, 1, 0x20);

    Mix_HookMusic(SDL_IMFMusicPlayer, 0);
    Mix_ChannelFinished(SD_ChannelFinished);
    Mix_SetPostMix(SDL_PCMixCallback, NULL);

    AdLibPresent = SDL_DetectAdLib();
    SoundBlasterPresent = true;
    SBProPresent = true;

    alTimeCount = 0;

    SDL_ResetPCSpeakerMixer();

    SD_SetSoundMode(sdm_Off);
    SD_SetMusicMode(smm_Off);

    SDL_SetupDigi();

    SD_Started = true;
}

void SD_Shutdown(void)
{
    if(!SD_Started)
    {
        return;
    }

    SD_MusicOff();
    SD_StopSound();

    Mix_SetPostMix(NULL, NULL);
    Mix_HookMusic(NULL, NULL);

    for(int i = 0; i < STARTMUSIC - STARTDIGISOUNDS; i++)
    {
        if(SoundChunks[i])
        {
            Mix_FreeChunk(SoundChunks[i]);
            SoundChunks[i] = NULL;
        }

        if(SoundBuffers[i])
        {
            free(SoundBuffers[i]);
            SoundBuffers[i] = NULL;
        }
    }

    free(DigiList);
    DigiList = NULL;

    SDL_CleanDevice();
    YM3812Shutdown();

    Mix_CloseAudio();

    SD_Started = false;
}

/*
=============================================================================

    SOUND EFFECT PLAYBACK

=============================================================================
*/

void SD_PositionSound(int leftvol, int rightvol)
{
    LeftPosition = leftvol;
    RightPosition = rightvol;
    nextsoundpos = true;
}

boolean SD_PlaySound(soundnames sound)
{
    boolean ispos;
    SoundCommon *s;
    int lp;
    int rp;

    lp = LeftPosition;
    rp = RightPosition;

    LeftPosition = 0;
    RightPosition = 0;

    ispos = nextsoundpos;
    nextsoundpos = false;

    if(sound == -1 || (DigiMode == sds_Off && SoundMode == sdm_Off))
    {
        return 0;
    }

    s = (SoundCommon *)SoundTable[sound];

    if((SoundMode != sdm_Off) && !s)
    {
        Quit("SD_PlaySound() - Uncached sound");
    }

    if((DigiMode != sds_Off) && (DigiMap[sound] != -1))
    {
        if((DigiMode == sds_PC) && (SoundMode == sdm_PC))
        {
            if(s && s->priority < SoundPriority)
            {
                return 0;
            }

            SDL_PCStopSound();

            SoundPositioned = ispos;
            SoundNumber = sound;
            SoundPriority = s ? s->priority : 0;

            return true;
        }
        else
        {
            if(s && s->priority < DigiPriority)
            {
                return false;
            }

            int channel = SD_PlayDigitized(DigiMap[sound], lp, rp);

            SoundPositioned = ispos;
            DigiNumber = sound;
            DigiPriority = s ? s->priority : 0;

            return channel + 1;
        }
    }

    if(SoundMode == sdm_Off)
    {
        return 0;
    }

    if(!s->length)
    {
        Quit("SD_PlaySound() - Zero length sound");
    }

    if(s->priority < SoundPriority)
    {
        return 0;
    }

    switch(SoundMode)
    {
        case sdm_PC:
            SDL_PCPlaySound((PCSound *)s);
            break;

        case sdm_AdLib:
            SDL_ALPlaySound((AdLibSound *)s);
            break;

        default:
            break;
    }

    SoundNumber = sound;
    SoundPriority = s->priority;

    return 0;
}

word SD_SoundPlaying(void)
{
    boolean result = false;

    switch(SoundMode)
    {
        case sdm_PC:
            result = pcSound ? true : false;
            break;

        case sdm_AdLib:
            result = alSound ? true : false;
            break;

        default:
            break;
    }

    if(result)
    {
        return SoundNumber;
    }

    return false;
}

void SD_StopSound(void)
{
    if(DigiPlaying)
    {
        SD_StopDigitized();
    }

    switch(SoundMode)
    {
        case sdm_PC:
            SDL_PCStopSound();
            break;

        case sdm_AdLib:
            SDL_ALStopSound();
            break;

        default:
            break;
    }

    SoundPositioned = false;
    SDL_SoundFinished();
}

void SD_WaitSoundDone(void)
{
    while(SD_SoundPlaying())
    {
        SDL_Delay(5);
    }
}

/*
=============================================================================

    MUSIC

=============================================================================
*/

void SD_MusicOn(void)
{
    sqActive = true;
}

int SD_MusicOff(void)
{
    int offset = 0;

    sqActive = false;

    if(sqHack && sqHackPtr)
    {
        offset = (int)(sqHackPtr - sqHack);
    }

    switch(MusicMode)
    {
        case smm_AdLib:
            alOut(alEffects, 0);

            for(word i = 0; i < sqMaxTracks; i++)
            {
                alOut(alFreqH + i + 1, 0);
            }

            break;

        default:
            break;
    }

    return offset;
}

void SD_StartMusic(int chunk)
{
    SD_MusicOff();

    if(MusicMode == smm_AdLib)
    {
        int32_t chunkLen = CA_CacheAudioChunk(chunk);

        sqHack = (word *)(void *)audiosegs[chunk];

        if(*sqHack == 0)
        {
            sqHackLen = sqHackSeqLen = chunkLen;
        }
        else
        {
            sqHackLen = sqHackSeqLen = *sqHack++;
        }

        sqHackPtr = sqHack;
        sqHackTime = 0;
        alTimeCount = 0;

        SD_MusicOn();
    }
}

void SD_ContinueMusic(int chunk, int startoffs)
{
    SD_MusicOff();

    if(MusicMode == smm_AdLib)
    {
        int32_t chunkLen = CA_CacheAudioChunk(chunk);

        sqHack = (word *)(void *)audiosegs[chunk];

        if(*sqHack == 0)
        {
            sqHackLen = sqHackSeqLen = chunkLen;
        }
        else
        {
            sqHackLen = sqHackSeqLen = *sqHack++;
        }

        sqHackPtr = sqHack;

        if(startoffs >= sqHackLen)
        {
            Quit("SD_StartMusic: Illegal startoffs provided!");
        }

        for(int i = 0; i < startoffs; i += 2)
        {
            byte reg = *(byte *)sqHackPtr;
            byte val = *(((byte *)sqHackPtr) + 1);

            if(reg >= 0xb1 && reg <= 0xb8)
            {
                val &= 0xdf;
            }
            else if(reg == 0xbd)
            {
                val &= 0xe0;
            }

            alOut(reg, val);

            sqHackPtr += 2;
            sqHackLen -= 4;
        }

        sqHackTime = 0;
        alTimeCount = 0;

        SD_MusicOn();
    }
}

void SD_FadeOutMusic(void)
{
    switch(MusicMode)
    {
        case smm_AdLib:
            SD_MusicOff();
            break;

        default:
            break;
    }
}

boolean SD_MusicPlaying(void)
{
    boolean result;

    switch(MusicMode)
    {
        case smm_AdLib:
            result = sqActive;
            break;

        default:
            result = false;
            break;
    }

    return result;
}
