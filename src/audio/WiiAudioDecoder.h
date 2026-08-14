#pragma once

#ifdef AUDIO_WII

#include <stdint.h>

enum WiiPcmByteOrder
{
	WII_PCM_BIG_ENDIAN,
	WII_PCM_LITTLE_ENDIAN
};

class WiiAudioDecoder
{
public:
	virtual ~WiiAudioDecoder() {}

	virtual bool Open(const char *path) = 0;
	virtual void Close() = 0;
	virtual uint32_t Decode(void *buffer, uint32_t capacity) = 0;
	virtual bool Seek(uint32_t milliseconds) = 0;
	virtual uint32_t Tell() const = 0;
	virtual uint32_t GetLength() const = 0;
	virtual uint32_t GetSampleRate() const = 0;
	virtual uint32_t GetChannels() const = 0;
	virtual WiiPcmByteOrder GetByteOrder() const = 0;
};

bool InitialiseWiiAudioDecoders();
void TerminateWiiAudioDecoders();
WiiAudioDecoder *CreateWiiAudioDecoder(const char *path, bool trace = false);

#endif
