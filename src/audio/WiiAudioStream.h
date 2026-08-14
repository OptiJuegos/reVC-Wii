#pragma once

#ifdef AUDIO_WII

#include <stdint.h>

class WiiAudioDecoder;

class WiiAudioStream
{
public:
	WiiAudioStream();
	~WiiAudioStream();

	bool Initialise(int32_t voice);
	void Shutdown();
	bool Open(const char *path);
	void Close();
	bool Prepare(uint32_t milliseconds);
	bool StartPrepared();
	void Pause(bool pause);
	void Service();
	void SetLoop(bool loop);
	void SetVolume(uint32_t left, uint32_t right);

	bool IsOpen() const { return m_decoder != 0; }
	bool IsPlaying() const { return m_playing; }
	uint32_t GetPosition() const;
	uint32_t GetLength() const;

private:
	enum BufferState
	{
		BUFFER_FREE,
		BUFFER_READY,
		BUFFER_QUEUED
	};

	struct Buffer
	{
		uint8_t *data;
		uint32_t size;
		BufferState state;
	};

	static const uint32_t BufferCount = 6;
	static const uint32_t BufferCapacity = 32 * 1024;

	void ResetBuffers();
	bool FillBuffer(Buffer &buffer);
	void FillAvailableBuffers(uint32_t maxBuffers);
	void ReclaimPlayedBuffers();
	bool BeginVoice();
	void QueueNextBuffer();
	int32_t GetAsndFormat() const;

	WiiAudioDecoder *m_decoder;
	Buffer m_buffers[BufferCount];
	int32_t m_voice;
	uint32_t m_fillIndex;
	uint32_t m_queueIndex;
	uint32_t m_leftVolume;
	uint32_t m_rightVolume;
	uint32_t m_startPosition;
	uint32_t m_timerOffset;
	uint32_t m_decodedBuffers;
	uint32_t m_queuedBuffers;
	uint32_t m_restartCount;
	bool m_loop;
	bool m_decoderEnded;
	bool m_prepared;
	bool m_playing;
	bool m_paused;
	bool m_voiceStarted;
	bool m_finishLogged;
};

#endif
