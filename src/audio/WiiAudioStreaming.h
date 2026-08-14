#pragma once

#ifdef AUDIO_WII

#include <stdint.h>

#include "audio_enums.h"
#include "sampman.h"
#include "WiiAudioStream.h"

class WiiAudioStreaming
{
public:
	WiiAudioStreaming();
	~WiiAudioStreaming();

	bool Initialise(uint32_t effectsVolume, uint32_t effectsFadeVolume,
	                uint32_t musicVolume, uint32_t musicFadeVolume);
	void Shutdown();
	void Preload(tTrack track, uint32_t stream);
	void Pause(bool pause, uint32_t stream);
	void StartPreloaded(uint32_t stream);
	bool Start(tTrack track, uint32_t position, uint32_t stream);
	void Stop(uint32_t stream);
	uint32_t GetPosition(uint32_t stream) const;
	uint32_t GetLength(tTrack track);
	bool IsPlaying(uint32_t stream) const;
	void SetLoop(bool loop, uint32_t stream);
	void SetVolumeAndPan(uint32_t volume, uint32_t pan, bool effect,
	                     uint32_t stream, uint32_t effectsVolume,
	                     uint32_t effectsFadeVolume, uint32_t musicVolume,
	                     uint32_t musicFadeVolume);
	void UpdateEffectsVolume(uint32_t effectsVolume,
	                         uint32_t effectsFadeVolume,
	                         uint32_t musicVolume,
	                         uint32_t musicFadeVolume);
	void UpdateMusicVolume(uint32_t effectsVolume,
	                       uint32_t effectsFadeVolume,
	                       uint32_t musicVolume,
	                       uint32_t musicFadeVolume);
	void Service();

private:
	bool Open(uint32_t stream, tTrack track);
	void ApplyVolume(uint32_t stream, uint32_t effectsVolume,
	                 uint32_t effectsFadeVolume, uint32_t musicVolume,
	                 uint32_t musicFadeVolume);
	bool LoadLengthCache();
	void WriteLengthCache();

	WiiAudioStream m_streams[MAX_STREAMS];
	uint32_t m_lengths[TOTAL_STREAMED_SOUNDS];
	uint8_t m_volumes[MAX_STREAMS];
	uint8_t m_pans[MAX_STREAMS];
	bool m_effects[MAX_STREAMS];
	bool m_loops[MAX_STREAMS];
	uint32_t m_resolvedLengths;
	bool m_decodersInitialised;
	bool m_lengthCacheWritten;
	bool m_serviceLogged;
};

#endif
