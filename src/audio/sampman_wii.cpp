#ifdef AUDIO_WII

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "crossplatform.h"
#include "sampman.h"
#include "AudioManager.h"
#include "wii-port/WiiLog.h"
#include "WiiAudioStreaming.h"
#include "WiiSfxCache.h"

#ifdef WII_AUDIO_DEBUG
#define WII_AUDIO_TRACE_LOG(...) wiiLog(__VA_ARGS__)
#else
#define WII_AUDIO_TRACE_LOG(...) ((void)0)
#endif

cSampleManager SampleManager;
bool8 _bSampmanInitialised = FALSE;
uint32 BankStartOffset[MAX_SFX_BANKS];
uint32 nNumMP3s;

namespace {

const char *SampleBankDescFilename = "audio/sfx.SDT";
const char *SampleBankDataFilename = "audio/sfx.RAW";
const int32 FirstSfxVoice = MAX_STREAMS;
const uint32 InvalidSample = (uint32)-1;
const uint32 SampleCacheBudget = 6 * 1024 * 1024;
const uint32 SfxTraceLimit = 20;
const uint32 PedCommentReadChunk = 16 * 1024;

struct WiiChannel
{
	const uint8 *data;
	uint32 sample;
	uint32 fileOffset;
	uint32 size;
	uint32 baseFrequency;
	uint32 frequency;
	uint32 volume;
	uint32 pan;
	uint32 loopStart;
	int32 loopEnd;
	uint32 loopCount;
	int32 voice;
	bool8 configured;
	bool8 fileBacked;
	uint32 startTime;
};

struct WiiVoiceBuffer
{
	const uint8 *sampleData;
	uint8 *sampleStorage;
	uint32 sampleCapacity;
	uint32 sampleSize;
	const uint8 *loopData;
	uint8 *loopStorage;
	uint32 loopCapacity;
	uint32 loopSize;
	uint32 cachedSample;
	bool8 loopUsesSample;
	volatile int32 owner;
};

struct WiiPedCommentLoad
{
	uint32 sample;
	uint32 offset;
	uint32 size;
	uint32 loaded;
	uint8 slot;
};

WiiChannel channels[NUM_CHANNELS];
WiiVoiceBuffer voiceBuffers[MAX_SND_VOICES];
FILE *sampleDataFile;
uint32 sampleDataSize;
uint32 sampleBankDiscStart[MAX_SFX_BANKS];
uint32 sampleBankSize[MAX_SFX_BANKS];
int8 sampleBankStatus[MAX_SFX_BANKS];
WiiSfxCache sampleCache;
WiiAudioStreaming audioStreaming;
uint8 *pedCommentData;
uint32 pedCommentSamples[MAX_PEDSFX];
uint8 currentPedSlot;
WiiPedCommentLoad pedCommentLoad;
uint8 *missionAudioData;
uint32 missionAudioCapacity;
uint32 missionAudioSample;
uint32 sfxStartTraceCount;
uint32 sfxFailureTraceCount;
bool8 audioServiceLogged;

static uint32
align32(uint32 size)
{
	return (size + 31u) & ~31u;
}

static bool8
ensureBuffer(uint8 **buffer, uint32 *capacity, uint32 size)
{
	uint32 required = align32(size);
	if(required == 0)
		required = 32;
	if(*buffer && *capacity >= required)
		return TRUE;
	free(*buffer);
	*buffer = (uint8*)memalign(32, required);
	if(*buffer == nil){
		*capacity = 0;
		return FALSE;
	}
	*capacity = required;
	return TRUE;
}

static bool8
isSampleDataRangeValid(uint32 offset, uint32 size)
{
	return sampleDataFile != nil && offset <= sampleDataSize &&
	       size <= sampleDataSize - offset;
}

static void
closeSampleDataFile(void)
{
	if(sampleDataFile){
		fclose(sampleDataFile);
		sampleDataFile = nil;
	}
	sampleDataSize = 0;
}

static void
resetPedCommentLoad(void)
{
	pedCommentLoad.sample = InvalidSample;
	pedCommentLoad.offset = 0;
	pedCommentLoad.size = 0;
	pedCommentLoad.loaded = 0;
	pedCommentLoad.slot = 0;
}

static void
servicePedCommentLoad(void)
{
	if(pedCommentLoad.sample == InvalidSample || pedCommentData == nil ||
	   sampleDataFile == nil)
		return;

	uint32 remaining = pedCommentLoad.size - pedCommentLoad.loaded;
	uint32 chunk = remaining > PedCommentReadChunk ?
	               PedCommentReadChunk : remaining;
	uint8 *destination = pedCommentData +
	                     PED_BLOCKSIZE * pedCommentLoad.slot +
	                     pedCommentLoad.loaded;
	uint32 fileOffset = pedCommentLoad.offset + pedCommentLoad.loaded;
	if(fseek(sampleDataFile, fileOffset, SEEK_SET) != 0 ||
	   fread(destination, 1, chunk, sampleDataFile) != chunk){
		wiiLog("[WII][AUDIO][PED] load failed sample=%u offset=%u "
		       "loaded=%u size=%u\n", pedCommentLoad.sample,
		       fileOffset, pedCommentLoad.loaded, pedCommentLoad.size);
		resetPedCommentLoad();
		return;
	}

	pedCommentLoad.loaded += chunk;
	if(pedCommentLoad.loaded == pedCommentLoad.size){
		pedCommentSamples[pedCommentLoad.slot] = pedCommentLoad.sample;
		currentPedSlot = (pedCommentLoad.slot + 1) % MAX_PEDSFX;
		resetPedCommentLoad();
	}
}

static void
copyPcmBuffer(uint8 *destination, const uint8 *source, uint32 size)
{
	memcpy(destination, source, size);
	DCFlushRange(destination, align32(size));
}

static void
releaseVoiceSample(WiiVoiceBuffer &buffer)
{
	if(buffer.cachedSample != InvalidSample)
		sampleCache.Release(buffer.cachedSample);
	buffer.sampleData = nil;
	buffer.sampleSize = 0;
	buffer.loopData = nil;
	buffer.loopSize = 0;
	buffer.cachedSample = InvalidSample;
	buffer.loopUsesSample = FALSE;
}

static void
clearVoiceOwner(int32 voice, bool8 stop)
{
	if(voice < FirstSfxVoice || voice >= MAX_SND_VOICES)
		return;
	if(stop)
		ASND_StopVoice(voice);
	int32 owner = voiceBuffers[voice].owner;
	voiceBuffers[voice].owner = -1;
	if(owner >= 0 && owner < NUM_CHANNELS && channels[owner].voice == voice)
		channels[owner].voice = -1;
	releaseVoiceSample(voiceBuffers[voice]);
}

static void
reclaimFinishedVoices(void)
{
	for(int32 voice = FirstSfxVoice; voice < MAX_SND_VOICES; voice++){
		if(voiceBuffers[voice].owner >= 0 &&
		   ASND_StatusVoice(voice) == SND_UNUSED)
			clearVoiceOwner(voice, FALSE);
	}
}

static int32
acquireVoice(void)
{
	reclaimFinishedVoices();
	for(int32 voice = FirstSfxVoice; voice < MAX_SND_VOICES; voice++){
		if(voiceBuffers[voice].owner < 0)
			return voice;
	}

	int32 victim = FirstSfxVoice;
	uint32 victimVolume = MAX_VOLUME + 1;
	uint32 victimAge = 0;
	uint32 now = ASND_GetTime();
	for(int32 voice = FirstSfxVoice; voice < MAX_SND_VOICES; voice++){
		int32 owner = voiceBuffers[voice].owner;
		if(owner < 0 || owner >= NUM_CHANNELS)
			return voice;
		uint32 volume = channels[owner].volume;
		uint32 age = now - channels[owner].startTime;
		if(volume < victimVolume ||
		   (volume == victimVolume && age > victimAge)){
			victim = voice;
			victimVolume = volume;
			victimAge = age;
		}
	}
	clearVoiceOwner(victim, TRUE);
	return victim;
}

static void
getVoiceVolumes(const WiiChannel &channel, uint32 effectsVolume,
	uint32 fadeVolume, bool8 mono, int32 *left, int32 *right)
{
	uint32 volume = channel.volume;
	if(volume > MAX_VOLUME)
		volume = MAX_VOLUME;
	volume = fadeVolume * volume * effectsVolume >> 14;
	if(volume > MAX_VOLUME)
		volume = MAX_VOLUME;

	uint32 pan = channel.pan > MAX_VOLUME ? MAX_VOLUME : channel.pan;
	uint32 leftVolume = volume;
	uint32 rightVolume = volume;
	if(!mono){
		if(pan < 64)
			rightVolume = volume * pan / 63;
		else
			leftVolume = volume * (127 - pan) / 63;
	}
	*left = leftVolume * 2;
	*right = rightVolume * 2;
}

static void
loopVoiceCallback(s32 voice)
{
	if(voice < FirstSfxVoice || voice >= MAX_SND_VOICES)
		return;
	WiiVoiceBuffer &buffer = voiceBuffers[voice];
	if(buffer.owner < 0 || buffer.loopData == nil || buffer.loopSize == 0)
		return;
	ASND_AddVoice(voice, (void*)buffer.loopData, buffer.loopSize);
}

static bool8
prepareVoiceBuffer(int32 voice, const WiiChannel &channel)
{
	WiiVoiceBuffer &buffer = voiceBuffers[voice];
	releaseVoiceSample(buffer);
	uint32 sampleSize = channel.size & ~1u;
	if(sampleSize == 0)
		return FALSE;
	if(channel.fileBacked){
		buffer.sampleData = sampleCache.Acquire(channel.sample,
		                                            channel.fileOffset,
		                                            sampleSize);
		if(buffer.sampleData == nil)
			return FALSE;
		buffer.cachedSample = channel.sample;
	}else{
		if(channel.data == nil ||
		   !ensureBuffer(&buffer.sampleStorage, &buffer.sampleCapacity,
		                 sampleSize))
			return FALSE;
		copyPcmBuffer(buffer.sampleStorage, channel.data, sampleSize);
		buffer.sampleData = buffer.sampleStorage;
	}

	uint32 loopStart = channel.loopStart & ~1u;
	uint32 loopEnd = channel.loopEnd < 0 ? sampleSize :
	                 (uint32)channel.loopEnd;
	if(loopEnd > sampleSize)
		loopEnd = sampleSize;
	loopEnd &= ~1u;
	if(loopStart >= loopEnd){
		loopStart = 0;
		loopEnd = sampleSize;
	}

	buffer.sampleSize = channel.loopCount == 1 ? sampleSize : loopEnd;
	buffer.loopUsesSample = loopStart == 0 &&
	                        loopEnd == buffer.sampleSize;
	if(buffer.loopUsesSample){
		buffer.loopData = buffer.sampleData;
		buffer.loopSize = buffer.sampleSize;
		return TRUE;
	}

	uint32 loopSize = loopEnd - loopStart;
	if(!ensureBuffer(&buffer.loopStorage, &buffer.loopCapacity, loopSize)){
		releaseVoiceSample(buffer);
		return FALSE;
	}
	buffer.loopData = buffer.loopStorage;
	copyPcmBuffer(buffer.loopStorage, buffer.sampleData + loopStart, loopSize);
	buffer.loopSize = loopSize;
	return TRUE;
}

static void
releaseVoiceBuffers(void)
{
	for(int32 voice = 0; voice < MAX_SND_VOICES; voice++){
		releaseVoiceSample(voiceBuffers[voice]);
		free(voiceBuffers[voice].sampleStorage);
		free(voiceBuffers[voice].loopStorage);
		memset(&voiceBuffers[voice], 0, sizeof(voiceBuffers[voice]));
		voiceBuffers[voice].owner = -1;
		voiceBuffers[voice].cachedSample = InvalidSample;
	}
}

}

cSampleManager::cSampleManager(void)
{
	m_nEffectsVolume = MAX_VOLUME;
	m_nMusicVolume = MAX_VOLUME;
	m_nMP3BoostVolume = 0;
	m_nEffectsFadeVolume = MAX_VOLUME;
	m_nMusicFadeVolume = MAX_VOLUME;
	m_nMonoMode = FALSE;
	for(uint32 i = 0; i < TOTAL_AUDIO_SAMPLES; i++){
		m_aSamples[i].nOffset = 0;
		m_aSamples[i].nSize = 0;
		m_aSamples[i].nFrequency = 22050;
		m_aSamples[i].nLoopStart = 0;
		m_aSamples[i].nLoopEnd = -1;
	}
}

cSampleManager::~cSampleManager(void)
{
}

bool8
cSampleManager::IsMP3RadioChannelAvailable(void)
{
	return FALSE;
}

void
cSampleManager::ReleaseDigitalHandle(void)
{
	if(_bSampmanInitialised){
		wiiLog("[WII][AUDIO] digital handle released; ASND paused\n");
		ASND_Pause(1);
	}
}

void
cSampleManager::ReacquireDigitalHandle(void)
{
	if(_bSampmanInitialised){
		wiiLog("[WII][AUDIO] digital handle reacquired; ASND resumed\n");
		ASND_Pause(0);
	}
}

bool8
cSampleManager::Initialise(void)
{
	if(_bSampmanInitialised)
		return TRUE;
	wiiLog("[WII][AUDIO] sample manager initialise effects=%u fade=%u "
	       "music=%u musicFade=%u mono=%s\n", (uint32)m_nEffectsVolume,
	       (uint32)m_nEffectsFadeVolume, (uint32)m_nMusicVolume,
	       (uint32)m_nMusicFadeVolume,
	       m_nMonoMode ? "yes" : "no");

	memset(channels, 0, sizeof(channels));
	memset(voiceBuffers, 0, sizeof(voiceBuffers));
	memset(sampleBankDiscStart, 0, sizeof(sampleBankDiscStart));
	memset(sampleBankSize, 0, sizeof(sampleBankSize));
	memset(sampleBankStatus, LOADING_STATUS_NOT_LOADED,
	       sizeof(sampleBankStatus));
	for(uint32 i = 0; i < NUM_CHANNELS; i++){
		channels[i].voice = -1;
		channels[i].pan = 63;
		channels[i].loopCount = 1;
	}
	for(int32 voice = 0; voice < MAX_SND_VOICES; voice++){
		voiceBuffers[voice].owner = -1;
		voiceBuffers[voice].cachedSample = InvalidSample;
	}
	for(uint32 i = 0; i < MAX_PEDSFX; i++)
		pedCommentSamples[i] = InvalidSample;
	currentPedSlot = 0;
	resetPedCommentLoad();
	missionAudioSample = InvalidSample;
	nNumMP3s = 0;
	sfxStartTraceCount = 0;
	sfxFailureTraceCount = 0;
	audioServiceLogged = FALSE;

	bool8 sfxReady = InitialiseSampleBanks();
	if(!sfxReady)
		wiiLog("[WII][AUDIO] SFX banks unavailable; continuing with "
		       "streaming audio\n");
	if(sfxReady &&
	   !sampleCache.Initialise(sampleDataFile, SAMPLEBANK_MAX,
	                           SampleCacheBudget)){
		wiiLog("[WII][AUDIO] failed to initialise SFX cache; continuing "
		       "with streaming audio\n");
		sfxReady = FALSE;
	}
	if(sfxReady){
		pedCommentData = (uint8*)memalign(32, PED_BLOCKSIZE * MAX_PEDSFX);
		if(pedCommentData == nil)
			wiiLog("[WII][AUDIO] pedestrian buffer unavailable bytes=%u; "
			       "generic SFX remain enabled\n",
			       (uint32)(PED_BLOCKSIZE * MAX_PEDSFX));
		if(!LoadSampleBank(SFX_BANK_0)){
			wiiLog("[WII][AUDIO] generic SFX bank could not be enabled; "
			       "continuing with streaming audio\n");
			sfxReady = FALSE;
		}
	}

	ASND_Init();
	wiiLog("[WII][AUDIO] ASND initialised rate=%u voices=%u paused=yes\n",
	       (uint32)ASND_GetAudioRate(), (uint32)MAX_SND_VOICES);
	_bSampmanInitialised = TRUE;
	bool8 streamingReady =
		audioStreaming.Initialise(m_nEffectsVolume, m_nEffectsFadeVolume,
		                          m_nMusicVolume, m_nMusicFadeVolume);
	if(!streamingReady)
		wiiLog("[WII][AUDIO] streaming unavailable; SFX ready=%s\n",
		       sfxReady ? "yes" : "no");
	ASND_Pause(0);
	wiiLog("[WII][AUDIO] ASND ready sfxVoices=%d streams=%d bank0=%uKB "
	       "cache=%uKB samples=%u rate=%u sfx=%s streaming=%s\n",
	       MAX_SND_VOICES - FirstSfxVoice, MAX_STREAMS,
	       sampleBankSize[SFX_BANK_0] / 1024,
	       sampleCache.GetBudget() / 1024,
	       (uint32)TOTAL_AUDIO_SAMPLES, (uint32)ASND_GetAudioRate(),
	       sfxReady ? "ready" : "disabled",
	       streamingReady ? "ready" : "disabled");
	return TRUE;
}

void
cSampleManager::Terminate(void)
{
	if(_bSampmanInitialised)
		wiiLog("[WII][AUDIO] sample manager terminate\n");
	if(_bSampmanInitialised){
		ASND_Pause(1);
		audioStreaming.Shutdown();
		for(int32 voice = FirstSfxVoice; voice < MAX_SND_VOICES; voice++)
			clearVoiceOwner(voice, TRUE);
		ASND_End();
	}
	_bSampmanInitialised = FALSE;
	if(sampleCache.GetBudget() != 0)
		wiiLog("[WII][AUDIO][CACHE] resident=%uKB hits=%u misses=%u "
		       "evictions=%u\n", sampleCache.GetResidentBytes() / 1024,
		       sampleCache.GetHits(), sampleCache.GetMisses(),
		       sampleCache.GetEvictions());
	releaseVoiceBuffers();
	sampleCache.Shutdown();
	for(uint32 bank = 0; bank < MAX_SFX_BANKS; bank++)
		sampleBankStatus[bank] = LOADING_STATUS_NOT_LOADED;
	resetPedCommentLoad();
	free(pedCommentData);
	pedCommentData = nil;
	free(missionAudioData);
	missionAudioData = nil;
	missionAudioCapacity = 0;
	missionAudioSample = InvalidSample;
	closeSampleDataFile();
}

bool8 cSampleManager::CheckForAnAudioFileOnCD(void) { return sampleDataFile != nil; }
char cSampleManager::GetCDAudioDriveLetter(void) { return '\0'; }

void
cSampleManager::UpdateEffectsVolume(void)
{
	for(uint32 channel = 0; channel < NUM_CHANNELS; channel++){
		int32 voice = channels[channel].voice;
		if(voice < FirstSfxVoice)
			continue;
		int32 left, right;
		getVoiceVolumes(channels[channel], m_nEffectsVolume,
		                m_nEffectsFadeVolume, m_nMonoMode, &left, &right);
		ASND_ChangeVolumeVoice(voice, left, right);
	}
	audioStreaming.UpdateEffectsVolume(m_nEffectsVolume,
	                                  m_nEffectsFadeVolume,
	                                  m_nMusicVolume,
	                                  m_nMusicFadeVolume);
}

void cSampleManager::SetEffectsMasterVolume(uint8 volume) { m_nEffectsVolume = volume; UpdateEffectsVolume(); }
void
cSampleManager::SetMusicMasterVolume(uint8 volume)
{
	m_nMusicVolume = volume;
	audioStreaming.UpdateMusicVolume(m_nEffectsVolume,
	                                m_nEffectsFadeVolume,
	                                m_nMusicVolume,
	                                m_nMusicFadeVolume);
}
void cSampleManager::SetMP3BoostVolume(uint8 volume) { m_nMP3BoostVolume = volume; }
void cSampleManager::SetEffectsFadeVolume(uint8 volume) { m_nEffectsFadeVolume = volume; UpdateEffectsVolume(); }
void
cSampleManager::SetMusicFadeVolume(uint8 volume)
{
	m_nMusicFadeVolume = volume;
	audioStreaming.UpdateMusicVolume(m_nEffectsVolume,
	                                m_nEffectsFadeVolume,
	                                m_nMusicVolume,
	                                m_nMusicFadeVolume);
}
void cSampleManager::SetMonoMode(bool8 mono) { m_nMonoMode = mono; UpdateEffectsVolume(); }

bool8
cSampleManager::LoadSampleBank(uint8 bank)
{
	ASSERT(bank < MAX_SFX_BANKS);
	if(bank != SFX_BANK_0 || sampleDataFile == nil)
		return FALSE;
	if(sampleBankStatus[bank] == LOADING_STATUS_LOADED)
		return TRUE;
	sampleBankStatus[bank] = LOADING_STATUS_LOADED;
	return TRUE;
}

void
cSampleManager::UnloadSampleBank(uint8 bank)
{
	ASSERT(bank < MAX_SFX_BANKS);
	if(bank != SFX_BANK_0)
		return;
	sampleBankStatus[bank] = LOADING_STATUS_NOT_LOADED;
}

int8
cSampleManager::IsSampleBankLoaded(uint8 bank)
{
	ASSERT(bank < MAX_SFX_BANKS);
	return sampleBankStatus[bank];
}

#if defined(GTA_PS2) || defined(FIX_BUGS)
uint8
cSampleManager::IsMissionAudioLoaded(uint8 slot, uint32 sample)
{
	ASSERT(slot < MISSION_AUDIO_COUNT);
	return missionAudioSample == sample ? LOADING_STATUS_LOADED :
	       LOADING_STATUS_NOT_LOADED;
}

bool8
cSampleManager::LoadMissionAudio(uint8 slot, uint32 sample)
{
	ASSERT(slot < MISSION_AUDIO_COUNT);
	ASSERT(sample < TOTAL_AUDIO_SAMPLES);
	if(!isSampleDataRangeValid(m_aSamples[sample].nOffset,
	                           m_aSamples[sample].nSize) ||
	   !ensureBuffer(&missionAudioData, &missionAudioCapacity,
	                 m_aSamples[sample].nSize))
		return FALSE;
	if(fseek(sampleDataFile, m_aSamples[sample].nOffset, SEEK_SET) != 0 ||
	   fread(missionAudioData, 1, m_aSamples[sample].nSize,
	         sampleDataFile) != m_aSamples[sample].nSize)
		return FALSE;
	missionAudioSample = sample;
	return TRUE;
}
#endif

uint8
cSampleManager::IsPedCommentLoaded(uint32 comment)
{
	if(_GetPedCommentSlot(comment) >= 0)
		return LOADING_STATUS_LOADED;
	if(pedCommentLoad.sample == comment)
		return LOADING_STATUS_LOADING;
	return LOADING_STATUS_NOT_LOADED;
}

int32
cSampleManager::_GetPedCommentSlot(uint32 comment)
{
	for(uint32 slot = 0; slot < MAX_PEDSFX; slot++)
		if(pedCommentSamples[slot] == comment)
			return slot;
	return -1;
}

bool8
cSampleManager::LoadPedComment(uint32 comment)
{
	ASSERT(comment < TOTAL_AUDIO_SAMPLES);
	if(_GetPedCommentSlot(comment) >= 0 || pedCommentLoad.sample == comment)
		return TRUE;
	if(pedCommentLoad.sample != InvalidSample || pedCommentData == nil ||
	   !isSampleDataRangeValid(m_aSamples[comment].nOffset,
	                           m_aSamples[comment].nSize) ||
	   m_aSamples[comment].nSize == 0 ||
	   m_aSamples[comment].nSize > PED_BLOCKSIZE)
		return FALSE;

	pedCommentSamples[currentPedSlot] = InvalidSample;
	pedCommentLoad.sample = comment;
	pedCommentLoad.offset = m_aSamples[comment].nOffset;
	pedCommentLoad.size = m_aSamples[comment].nSize;
	pedCommentLoad.loaded = 0;
	pedCommentLoad.slot = currentPedSlot;
	return TRUE;
}

int32
cSampleManager::GetBankContainingSound(uint32 offset)
{
	if(offset >= BankStartOffset[SFX_BANK_PED_COMMENTS])
		return SFX_BANK_PED_COMMENTS;
	if(offset >= BankStartOffset[SFX_BANK_0])
		return SFX_BANK_0;
	return INVALID_SFX_BANK;
}

uint32 cSampleManager::GetSampleBaseFrequency(uint32 sample) { ASSERT(sample < TOTAL_AUDIO_SAMPLES); return m_aSamples[sample].nFrequency; }
uint32 cSampleManager::GetSampleLoopStartOffset(uint32 sample) { ASSERT(sample < TOTAL_AUDIO_SAMPLES); return m_aSamples[sample].nLoopStart; }
int32 cSampleManager::GetSampleLoopEndOffset(uint32 sample) { ASSERT(sample < TOTAL_AUDIO_SAMPLES); return m_aSamples[sample].nLoopEnd; }
uint32 cSampleManager::GetSampleLength(uint32 sample) { ASSERT(sample < TOTAL_AUDIO_SAMPLES); return m_aSamples[sample].nSize / sizeof(uint16); }
bool8 cSampleManager::UpdateReverb(void) { return FALSE; }
void cSampleManager::SetChannelReverbFlag(uint32 channel, bool8) { ASSERT(channel < NUM_CHANNELS); }

bool8
cSampleManager::InitialiseChannel(uint32 channel, uint32 sfx, uint8 bank)
{
	ASSERT(channel < NUM_CHANNELS);
	ASSERT(sfx < TOTAL_AUDIO_SAMPLES);
	StopChannel(channel);
	WiiChannel &state = channels[channel];
	state.configured = FALSE;
	state.data = nil;
	state.fileBacked = FALSE;

	const uint8 *data = nil;
	if(sfx < SAMPLEBANK_MAX){
		if(bank >= MAX_SFX_BANKS || !IsSampleBankLoaded(bank))
			return FALSE;
		uint32 offset = m_aSamples[sfx].nOffset;
		uint32 bankStart = sampleBankDiscStart[bank];
		if(!isSampleDataRangeValid(offset, m_aSamples[sfx].nSize) ||
		   offset < bankStart ||
		   offset - bankStart > sampleBankSize[bank] ||
		   m_aSamples[sfx].nSize >
		   sampleBankSize[bank] - (offset - bankStart))
			return FALSE;
		state.fileOffset = offset;
		state.fileBacked = TRUE;
	}else if(sfx >= PLAYER_COMMENTS_START && sfx <= PLAYER_COMMENTS_END){
		if(missionAudioSample != sfx)
			return FALSE;
		data = missionAudioData;
	}else{
		int32 slot = _GetPedCommentSlot(sfx);
		if(slot < 0)
			return FALSE;
		data = pedCommentData + PED_BLOCKSIZE * slot;
	}

	state.data = data;
	state.sample = sfx;
	state.size = m_aSamples[sfx].nSize;
	state.baseFrequency = m_aSamples[sfx].nFrequency;
	state.frequency = state.baseFrequency;
	state.volume = 0;
	state.pan = 63;
	state.loopStart = 0;
	state.loopEnd = -1;
	state.loopCount = 1;
	state.voice = -1;
	state.configured = TRUE;
	return TRUE;
}

void
cSampleManager::SetChannelVolume(uint32 channel, uint32 volume)
{
	ASSERT(channel < NUM_CHANNELS);
	channels[channel].volume = volume > MAX_VOLUME ? MAX_VOLUME : volume;
	int32 voice = channels[channel].voice;
	if(voice >= FirstSfxVoice){
		int32 left, right;
		getVoiceVolumes(channels[channel], m_nEffectsVolume,
		                m_nEffectsFadeVolume, m_nMonoMode, &left, &right);
		ASND_ChangeVolumeVoice(voice, left, right);
	}
}

void
cSampleManager::SetChannelPan(uint32 channel, uint32 pan)
{
	ASSERT(channel < NUM_CHANNELS);
	channels[channel].pan = pan > MAX_VOLUME ? MAX_VOLUME : pan;
	SetChannelVolume(channel, channels[channel].volume);
}

void
cSampleManager::SetChannelFrequency(uint32 channel, uint32 frequency)
{
	ASSERT(channel < NUM_CHANNELS);
	if(frequency < MIN_PITCH)
		frequency = MIN_PITCH;
	if(frequency > MAX_PITCH)
		frequency = MAX_PITCH;
	channels[channel].frequency = frequency;
	if(channels[channel].voice >= FirstSfxVoice)
		ASND_ChangePitchVoice(channels[channel].voice, frequency);
}

void
cSampleManager::SetChannelLoopPoints(uint32 channel, uint32 start, int32 end)
{
	ASSERT(channel < NUM_CHANNELS);
	channels[channel].loopStart = start;
	channels[channel].loopEnd = end;
}

void
cSampleManager::SetChannelLoopCount(uint32 channel, uint32 count)
{
	ASSERT(channel < NUM_CHANNELS);
	channels[channel].loopCount = count;
}

bool8
cSampleManager::GetChannelUsedFlag(uint32 channel)
{
	ASSERT(channel < NUM_CHANNELS);
	int32 voice = channels[channel].voice;
	if(voice < FirstSfxVoice)
		return FALSE;
	if(ASND_StatusVoice(voice) == SND_UNUSED){
		clearVoiceOwner(voice, FALSE);
		return FALSE;
	}
	return TRUE;
}

void
cSampleManager::StartChannel(uint32 channel)
{
	ASSERT(channel < NUM_CHANNELS);
	WiiChannel &state = channels[channel];
	if(!state.configured || (!state.fileBacked && state.data == nil) ||
	   state.size < 2){
		if(sfxFailureTraceCount++ < SfxTraceLimit)
			wiiLog("[WII][AUDIO][SFX] start rejected channel=%u sample=%u "
			       "configured=%s fileBacked=%s data=%p size=%u\n",
			       channel, state.sample, state.configured ? "yes" : "no",
			       state.fileBacked ? "yes" : "no", (void*)state.data,
			       state.size);
		return;
	}
	StopChannel(channel);
	int32 voice = acquireVoice();
	if(!prepareVoiceBuffer(voice, state)){
		if(sfxFailureTraceCount++ < SfxTraceLimit)
			wiiLog("[WII][AUDIO][SFX] buffer failed channel=%u sample=%u "
			       "voice=%d offset=%u size=%u cached=%s\n", channel,
			       state.sample, voice, state.fileOffset, state.size,
			       state.fileBacked ? "yes" : "no");
		return;
	}

	voiceBuffers[voice].owner = channel;
	state.voice = voice;
	state.startTime = ASND_GetTime();
	int32 left, right;
	getVoiceVolumes(state, m_nEffectsVolume, m_nEffectsFadeVolume,
	                m_nMonoMode, &left, &right);

	WiiVoiceBuffer &buffer = voiceBuffers[voice];
	int32 result;
	if(state.loopCount == 1){
		result = ASND_SetVoice(voice, VOICE_MONO_16BIT_LE,
		                       state.frequency, 0,
		                       (void*)buffer.sampleData,
		                       buffer.sampleSize, left, right, nil);
	}else if(buffer.loopUsesSample){
		result = ASND_SetInfiniteVoice(voice, VOICE_MONO_16BIT_LE,
		                               state.frequency, 0,
		                               (void*)buffer.sampleData,
		                               buffer.sampleSize, left, right);
	}else{
		result = ASND_SetVoice(voice, VOICE_MONO_16BIT_LE,
		                       state.frequency, 0,
		                       (void*)buffer.sampleData,
		                       buffer.sampleSize, left, right,
		                       loopVoiceCallback);
	}
	if(sfxStartTraceCount < SfxTraceLimit)
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][SFX] start channel=%u sample=%u voice=%d "
		       "result=%d status=%d rate=%u bytes=%u volume=%d/%d "
		       "loopCount=%u loopBytes=%u\n", channel, state.sample, voice,
		       result, ASND_StatusVoice(voice), state.frequency,
		       buffer.sampleSize, left, right, state.loopCount,
		       buffer.loopSize);
	else if(sfxStartTraceCount == SfxTraceLimit)
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][SFX] further successful start messages "
		       "suppressed\n");
	sfxStartTraceCount++;
	if(result != SND_OK){
		if(sfxFailureTraceCount++ < SfxTraceLimit)
			wiiLog("[WII][AUDIO][SFX] ASND start failed channel=%u "
			       "sample=%u voice=%d result=%d\n", channel,
			       state.sample, voice, result);
		clearVoiceOwner(voice, TRUE);
	}
}

void
cSampleManager::StopChannel(uint32 channel)
{
	ASSERT(channel < NUM_CHANNELS);
	if(channels[channel].voice >= FirstSfxVoice)
		clearVoiceOwner(channels[channel].voice, TRUE);
}

void
cSampleManager::PreloadStreamedFile(tTrack track, uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	audioStreaming.Preload(track, stream);
}

void
cSampleManager::PauseStream(bool8 pause, uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	audioStreaming.Pause(pause != FALSE, stream);
}

void
cSampleManager::StartPreloadedStreamedFile(uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	audioStreaming.StartPreloaded(stream);
}

bool8
cSampleManager::StartStreamedFile(tTrack track, uint32 position, uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	return audioStreaming.Start(track, position, stream) ? TRUE : FALSE;
}

void
cSampleManager::StopStreamedFile(uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	audioStreaming.Stop(stream);
}

int32
cSampleManager::GetStreamedFilePosition(uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	return audioStreaming.GetPosition(stream);
}

void
cSampleManager::SetStreamedVolumeAndPan(uint8 volume, uint8 pan,
	bool8 effect, uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	audioStreaming.SetVolumeAndPan(volume, pan, effect, stream,
	                               m_nEffectsVolume, m_nEffectsFadeVolume,
	                               m_nMusicVolume, m_nMusicFadeVolume);
}

int32
cSampleManager::GetStreamedFileLength(tTrack track)
{
	ASSERT(track < TOTAL_STREAMED_SOUNDS);
	return audioStreaming.GetLength(track);
}

bool8
cSampleManager::IsStreamPlaying(uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	return audioStreaming.IsPlaying(stream) ? TRUE : FALSE;
}

void
cSampleManager::SetStreamedFileLoopFlag(bool8 loop, uint8 stream)
{
	ASSERT(stream < MAX_STREAMS);
	audioStreaming.SetLoop(loop != FALSE, stream);
}

void
cSampleManager::Service(void)
{
	if(!audioServiceLogged){
		wiiLog("[WII][AUDIO] service active ASND rate=%u\n",
		       (uint32)ASND_GetAudioRate());
		audioServiceLogged = TRUE;
	}
	reclaimFinishedVoices();
	servicePedCommentLoad();
	audioStreaming.Service();
}

bool8
cSampleManager::InitialiseSampleBanks(void)
{
	wiiLog("[WII][AUDIO][BANK] open descriptor=%s data=%s\n",
	       SampleBankDescFilename, SampleBankDataFilename);
	FILE *descriptorFile = fcaseopen(SampleBankDescFilename, "rb");
	if(descriptorFile == nil){
		wiiLog("[WII][AUDIO][BANK] descriptor open failed path=%s\n",
		       SampleBankDescFilename);
		return FALSE;
	}
	sampleDataFile = fcaseopen(SampleBankDataFilename, "rb");
	if(sampleDataFile == nil){
		wiiLog("[WII][AUDIO][BANK] data open failed path=%s\n",
		       SampleBankDataFilename);
		fclose(descriptorFile);
		return FALSE;
	}

	size_t count = fread(m_aSamples, sizeof(tSample), TOTAL_AUDIO_SAMPLES,
	                     descriptorFile);
	fclose(descriptorFile);
	if(count != TOTAL_AUDIO_SAMPLES){
		wiiLog("[WII][AUDIO][BANK] descriptor sample count=%u expected=%u\n",
		       (uint32)count, (uint32)TOTAL_AUDIO_SAMPLES);
		closeSampleDataFile();
		return FALSE;
	}

#ifdef BIGENDIAN
	for(uint32 i = 0; i < TOTAL_AUDIO_SAMPLES; i++){
		memLittle32(&m_aSamples[i].nOffset);
		memLittle32(&m_aSamples[i].nSize);
		memLittle32(&m_aSamples[i].nFrequency);
		memLittle32(&m_aSamples[i].nLoopStart);
		memLittle32(&m_aSamples[i].nLoopEnd);
	}
#endif

	if(fseek(sampleDataFile, 0, SEEK_END) != 0){
		wiiLog("[WII][AUDIO][BANK] data seek-to-end failed\n");
		closeSampleDataFile();
		return FALSE;
	}
	long dataEnd = ftell(sampleDataFile);
	if(dataEnd <= 0){
		wiiLog("[WII][AUDIO][BANK] invalid RAW length=%ld\n", dataEnd);
		closeSampleDataFile();
		return FALSE;
	}
	sampleDataSize = (uint32)dataEnd;
	rewind(sampleDataFile);
	uint32 invalidSamples = 0;
	for(uint32 i = 0; i < TOTAL_AUDIO_SAMPLES; i++){
		if(m_aSamples[i].nOffset > (uint32)dataEnd ||
		   m_aSamples[i].nSize > (uint32)dataEnd - m_aSamples[i].nOffset){
			if(invalidSamples < 4)
				wiiLog("[WII][AUDIO][BANK] sample outside RAW index=%u "
				       "offset=%u size=%u raw=%u\n", i,
				       m_aSamples[i].nOffset, m_aSamples[i].nSize,
				       (uint32)dataEnd);
			invalidSamples++;
		}
	}
	if(invalidSamples != 0)
		wiiLog("[WII][AUDIO][BANK] descriptor has %u/%u samples outside "
		       "RAW; valid entries remain usable\n", invalidSamples,
		       (uint32)TOTAL_AUDIO_SAMPLES);
	if(BankStartOffset[SFX_BANK_0] >= TOTAL_AUDIO_SAMPLES ||
	   BankStartOffset[SFX_BANK_PED_COMMENTS] >= TOTAL_AUDIO_SAMPLES){
		wiiLog("[WII][AUDIO][BANK] invalid bank indices generic=%u ped=%u "
		       "samples=%u\n", BankStartOffset[SFX_BANK_0],
		       BankStartOffset[SFX_BANK_PED_COMMENTS],
		       (uint32)TOTAL_AUDIO_SAMPLES);
		closeSampleDataFile();
		return FALSE;
	}

	sampleBankDiscStart[SFX_BANK_0] =
		m_aSamples[BankStartOffset[SFX_BANK_0]].nOffset;
	sampleBankDiscStart[SFX_BANK_PED_COMMENTS] =
		m_aSamples[BankStartOffset[SFX_BANK_PED_COMMENTS]].nOffset;
	if(sampleBankDiscStart[SFX_BANK_PED_COMMENTS] <
	   sampleBankDiscStart[SFX_BANK_0] ||
	   sampleBankDiscStart[SFX_BANK_PED_COMMENTS] > (uint32)dataEnd ||
	   sampleBankDiscStart[SFX_BANK_0] > (uint32)dataEnd){
		wiiLog("[WII][AUDIO][BANK] invalid bank offsets generic=%u ped=%u "
		       "raw=%u; using whole RAW as generic bank\n",
		       sampleBankDiscStart[SFX_BANK_0],
		       sampleBankDiscStart[SFX_BANK_PED_COMMENTS],
		       (uint32)dataEnd);
		sampleBankDiscStart[SFX_BANK_0] = 0;
		sampleBankDiscStart[SFX_BANK_PED_COMMENTS] = (uint32)dataEnd;
	}
	sampleBankSize[SFX_BANK_0] =
		sampleBankDiscStart[SFX_BANK_PED_COMMENTS] -
		sampleBankDiscStart[SFX_BANK_0];
	sampleBankSize[SFX_BANK_PED_COMMENTS] =
		(uint32)dataEnd - sampleBankDiscStart[SFX_BANK_PED_COMMENTS];
	wiiLog("[WII][AUDIO][BANK] ready data=%uKB bank0=%uKB ped=%uKB "
	       "samples=%u invalid=%u genericIndex=%u pedIndex=%u\n",
	       (uint32)dataEnd / 1024,
	       sampleBankSize[SFX_BANK_0] / 1024,
	       sampleBankSize[SFX_BANK_PED_COMMENTS] / 1024,
	       (uint32)TOTAL_AUDIO_SAMPLES, invalidSamples,
	       BankStartOffset[SFX_BANK_0],
	       BankStartOffset[SFX_BANK_PED_COMMENTS]);
	return TRUE;
}

#endif
