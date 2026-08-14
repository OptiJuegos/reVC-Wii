#ifdef AUDIO_WII

#include <stdio.h>
#include <string.h>

#include "common.h"
#include "crossplatform.h"
#include "wii-port/WiiLog.h"
#include "WiiAudioDecoder.h"
#include "WiiAudioStreaming.h"

#ifdef WII_AUDIO_DEBUG
#define WII_AUDIO_TRACE_LOG(...) wiiLog(__VA_ARGS__)
#else
#define WII_AUDIO_TRACE_LOG(...) ((void)0)
#endif

namespace {

const char *LengthCacheFilename = "audio/wii_sound.cache";
const char LengthCacheMagic[8] = { 'R', 'V', 'W', 'I', 'I', 'A', 'U', 'D' };
const uint32 LengthCacheVersion = 1;
const uint32 InvalidLength = (uint32)-1;

}

WiiAudioStreaming::WiiAudioStreaming()
 : m_resolvedLengths(0), m_decodersInitialised(false),
	m_lengthCacheWritten(false), m_serviceLogged(false)
{
	for(uint32 stream = 0; stream < MAX_STREAMS; stream++){
		m_volumes[stream] = 100;
		m_pans[stream] = 63;
		m_effects[stream] = false;
		m_loops[stream] = false;
	}
	for(uint32 track = 0; track < TOTAL_STREAMED_SOUNDS; track++)
		m_lengths[track] = InvalidLength;
}

WiiAudioStreaming::~WiiAudioStreaming()
{
	Shutdown();
}

bool
WiiAudioStreaming::Initialise(uint32 effectsVolume,
	uint32 effectsFadeVolume, uint32 musicVolume, uint32 musicFadeVolume)
{
	Shutdown();
	for(uint32 stream = 0; stream < MAX_STREAMS; stream++){
		m_volumes[stream] = 100;
		m_pans[stream] = 63;
		m_effects[stream] = false;
		m_loops[stream] = false;
	}
	for(uint32 track = 0; track < TOTAL_STREAMED_SOUNDS; track++)
		m_lengths[track] = InvalidLength;
	m_resolvedLengths = 0;
	m_lengthCacheWritten = false;
	m_serviceLogged = false;
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] initialise effects=%u fade=%u "
	       "music=%u musicFade=%u streams=%u\n", effectsVolume,
	       effectsFadeVolume, musicVolume, musicFadeVolume,
	       (uint32)MAX_STREAMS);

	if(!InitialiseWiiAudioDecoders()){
		wiiLog("[WII][AUDIO][STREAMING] decoder initialisation failed\n");
		return false;
	}
	m_decodersInitialised = true;
	if(LoadLengthCache())
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] loaded %u cached lengths\n",
		       (uint32)TOTAL_STREAMED_SOUNDS);
	else
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] length cache unavailable; "
		       "durations will be scanned\n");

	for(uint32 stream = 0; stream < MAX_STREAMS; stream++){
		if(!m_streams[stream].Initialise(stream)){
			wiiLog("[WII][AUDIO][STREAMING] stream %u initialisation "
			       "failed\n", stream);
			Shutdown();
			return false;
		}
		ApplyVolume(stream, effectsVolume, effectsFadeVolume,
		            musicVolume, musicFadeVolume);
	}
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] ready\n");
	return true;
}

void
WiiAudioStreaming::Shutdown()
{
	if(m_decodersInitialised)
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] shutdown\n");
	for(uint32 stream = 0; stream < MAX_STREAMS; stream++)
		m_streams[stream].Shutdown();
	if(m_decodersInitialised){
		TerminateWiiAudioDecoders();
		m_decodersInitialised = false;
	}
}

void
WiiAudioStreaming::Preload(tTrack track, uint32 stream)
{
	if(stream >= MAX_STREAMS)
		return;
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] preload stream=%u track=%u\n",
	       stream, (uint32)track);
	if(!Open(stream, track) || !m_streams[stream].Prepare(0)){
		wiiLog("[WII][AUDIO][STREAMING] preload failed stream=%u "
		       "track=%u\n", stream, (uint32)track);
		m_streams[stream].Close();
	}
}

void
WiiAudioStreaming::Pause(bool pause, uint32 stream)
{
	if(stream < MAX_STREAMS)
		m_streams[stream].Pause(pause);
}

void
WiiAudioStreaming::StartPreloaded(uint32 stream)
{
	if(stream >= MAX_STREAMS)
		return;
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] start preloaded stream=%u loop=%s\n",
	       stream, m_loops[stream] ? "yes" : "no");
	m_streams[stream].SetLoop(m_loops[stream]);
	if(!m_streams[stream].StartPrepared())
		wiiLog("[WII][AUDIO][STREAMING] start preloaded failed "
		       "stream=%u\n", stream);
}

bool
WiiAudioStreaming::Start(tTrack track, uint32 position, uint32 stream)
{
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] start stream=%u track=%u "
	       "position=%ums volume=%u pan=%u effect=%s loop=%s\n", stream,
	       (uint32)track, position,
	       stream < MAX_STREAMS ? (uint32)m_volumes[stream] : 0u,
	       stream < MAX_STREAMS ? (uint32)m_pans[stream] : 0u,
	       stream < MAX_STREAMS && m_effects[stream] ? "yes" : "no",
	       stream < MAX_STREAMS && m_loops[stream] ? "yes" : "no");
	if(stream >= MAX_STREAMS || (uint32)track >= TOTAL_STREAMED_SOUNDS ||
	   track == STREAMED_SOUND_RADIO_MP3_PLAYER){
		wiiLog("[WII][AUDIO][STREAMING] start rejected stream=%u "
		       "track=%u\n", stream, (uint32)track);
		return false;
	}
	if(!Open(stream, track) || !m_streams[stream].Prepare(position) ||
	   !m_streams[stream].StartPrepared()){
		wiiLog("[WII][AUDIO][STREAMING] start failed stream=%u track=%u\n",
		       stream, (uint32)track);
		m_streams[stream].Close();
		return false;
	}
	return true;
}

void
WiiAudioStreaming::Stop(uint32 stream)
{
	if(stream < MAX_STREAMS){
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] stop stream=%u position=%ums\n",
		       stream, m_streams[stream].GetPosition());
		m_streams[stream].Close();
	}
}

uint32
WiiAudioStreaming::GetPosition(uint32 stream) const
{
	return stream < MAX_STREAMS ? m_streams[stream].GetPosition() : 0;
}

uint32
WiiAudioStreaming::GetLength(tTrack track)
{
	if((uint32)track >= TOTAL_STREAMED_SOUNDS || !m_decodersInitialised)
		return 0;
	uint32 &length = m_lengths[track];
	if(length != InvalidLength)
		return length;

	WiiAudioDecoder *decoder = CreateWiiAudioDecoder(StreamedNameTable[track]);
	if(decoder == nil || !decoder->Open(StreamedNameTable[track])){
		delete decoder;
		length = 0;
	}else{
		length = decoder->GetLength();
		delete decoder;
	}
	m_resolvedLengths++;
	WriteLengthCache();
	return length;
}

bool
WiiAudioStreaming::IsPlaying(uint32 stream) const
{
	return stream < MAX_STREAMS && m_streams[stream].IsPlaying();
}

void
WiiAudioStreaming::SetLoop(bool loop, uint32 stream)
{
	if(stream >= MAX_STREAMS)
		return;
	if(m_loops[stream] != loop)
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] loop stream=%u enabled=%s\n",
		       stream, loop ? "yes" : "no");
	m_loops[stream] = loop;
	m_streams[stream].SetLoop(loop);
}

void
WiiAudioStreaming::SetVolumeAndPan(uint32 volume, uint32 pan, bool effect,
	uint32 stream, uint32 effectsVolume, uint32 effectsFadeVolume,
	uint32 musicVolume, uint32 musicFadeVolume)
{
	if(stream >= MAX_STREAMS)
		return;
	m_volumes[stream] = volume > MAX_VOLUME ? MAX_VOLUME : volume;
	m_pans[stream] = pan > MAX_VOLUME ? MAX_VOLUME : pan;
	m_effects[stream] = effect;
	ApplyVolume(stream, effectsVolume, effectsFadeVolume,
	            musicVolume, musicFadeVolume);
}

void
WiiAudioStreaming::UpdateEffectsVolume(uint32 effectsVolume,
	uint32 effectsFadeVolume, uint32 musicVolume, uint32 musicFadeVolume)
{
	for(uint32 stream = 0; stream < MAX_STREAMS; stream++)
		if(m_effects[stream])
			ApplyVolume(stream, effectsVolume, effectsFadeVolume,
			            musicVolume, musicFadeVolume);
}

void
WiiAudioStreaming::UpdateMusicVolume(uint32 effectsVolume,
	uint32 effectsFadeVolume, uint32 musicVolume, uint32 musicFadeVolume)
{
	for(uint32 stream = 0; stream < MAX_STREAMS; stream++)
		if(!m_effects[stream])
			ApplyVolume(stream, effectsVolume, effectsFadeVolume,
			            musicVolume, musicFadeVolume);
}

void
WiiAudioStreaming::Service()
{
	if(!m_serviceLogged){
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] service active\n");
		m_serviceLogged = true;
	}
	for(uint32 stream = 0; stream < MAX_STREAMS; stream++)
		m_streams[stream].Service();
}

bool
WiiAudioStreaming::Open(uint32 stream, tTrack track)
{
	if(stream >= MAX_STREAMS || (uint32)track >= TOTAL_STREAMED_SOUNDS ||
	   track == STREAMED_SOUND_RADIO_MP3_PLAYER ||
	   !m_decodersInitialised)
		return false;
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] open stream=%u track=%u path=%s\n",
	       stream, (uint32)track, StreamedNameTable[track]);
	if(!m_streams[stream].Open(StreamedNameTable[track])){
		wiiLog("[WII][AUDIO][STREAMING] open failed stream=%u track=%u "
		       "path=%s\n", stream, (uint32)track,
		       StreamedNameTable[track]);
		return false;
	}
	m_streams[stream].SetLoop(m_loops[stream]);
	return true;
}

void
WiiAudioStreaming::ApplyVolume(uint32 stream, uint32 effectsVolume,
	uint32 effectsFadeVolume, uint32 musicVolume, uint32 musicFadeVolume)
{
	uint32 volume = m_volumes[stream];
	uint32 pan = m_pans[stream];
	if(m_effects[stream]){
		if(stream == 1 || stream == 2)
			volume = 128 * volume * effectsVolume >> 14;
		else
			volume = effectsFadeVolume * volume * effectsVolume >> 14;
	}else
		volume = musicFadeVolume * volume * musicVolume >> 14;
	if(volume > MAX_VOLUME)
		volume = MAX_VOLUME;

	uint32 left = volume;
	uint32 right = volume;
	if(pan < 64)
		right = volume * pan / 63;
	else
		left = volume * (127 - pan) / 63;
	m_streams[stream].SetVolume(left * 2, right * 2);
}

bool
WiiAudioStreaming::LoadLengthCache()
{
	FILE *file = fcaseopen(LengthCacheFilename, "rb");
	if(file == nil)
		return false;
	char magic[sizeof(LengthCacheMagic)];
	uint32 version = 0;
	uint32 count = 0;
	bool loaded =
		fread(magic, 1, sizeof(magic), file) == sizeof(magic) &&
		fread(&version, sizeof(version), 1, file) == 1 &&
		fread(&count, sizeof(count), 1, file) == 1 &&
		memcmp(magic, LengthCacheMagic, sizeof(magic)) == 0 &&
		version == LengthCacheVersion && count == TOTAL_STREAMED_SOUNDS &&
		fread(m_lengths, sizeof(m_lengths[0]), count, file) == count;
	fclose(file);
	if(!loaded)
		return false;
	m_resolvedLengths = TOTAL_STREAMED_SOUNDS;
	m_lengthCacheWritten = true;
	return true;
}

void
WiiAudioStreaming::WriteLengthCache()
{
	if(m_lengthCacheWritten || m_resolvedLengths != TOTAL_STREAMED_SOUNDS)
		return;
	FILE *file = fcaseopen(LengthCacheFilename, "wb");
	if(file == nil){
		wiiLog("[WII][AUDIO][STREAMING] cannot create length cache path=%s\n",
		       LengthCacheFilename);
		return;
	}
	uint32 count = TOTAL_STREAMED_SOUNDS;
	bool written =
		fwrite(LengthCacheMagic, 1, sizeof(LengthCacheMagic), file) ==
			sizeof(LengthCacheMagic) &&
		fwrite(&LengthCacheVersion, sizeof(LengthCacheVersion), 1, file) == 1 &&
		fwrite(&count, sizeof(count), 1, file) == 1 &&
		fwrite(m_lengths, sizeof(m_lengths[0]), count, file) == count;
	fclose(file);
	if(written){
		m_lengthCacheWritten = true;
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAMING] wrote length cache tracks=%u\n",
		       count);
	}else
		wiiLog("[WII][AUDIO][STREAMING] length cache write failed\n");
}

#endif
