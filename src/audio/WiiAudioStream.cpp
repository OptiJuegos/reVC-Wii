#ifdef AUDIO_WII

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "wii-port/WiiLog.h"
#include "WiiAudioDecoder.h"
#include "WiiAudioStream.h"

#ifdef WII_AUDIO_DEBUG
#define WII_AUDIO_TRACE_LOG(...) wiiLog(__VA_ARGS__)
#else
#define WII_AUDIO_TRACE_LOG(...) ((void)0)
#endif

WiiAudioStream::WiiAudioStream()
	 : m_decoder(0), m_voice(-1), m_fillIndex(0), m_queueIndex(0),
	m_leftVolume(200), m_rightVolume(200), m_startPosition(0),
	m_timerOffset(0), m_decodedBuffers(0), m_queuedBuffers(0),
	m_restartCount(0), m_loop(false), m_decoderEnded(false),
	m_prepared(false), m_playing(false), m_paused(false),
	m_voiceStarted(false), m_finishLogged(false)
{
	memset(m_buffers, 0, sizeof(m_buffers));
}

WiiAudioStream::~WiiAudioStream()
{
	Shutdown();
}

bool
WiiAudioStream::Initialise(int32_t voice)
{
	Shutdown();
	m_voice = voice;
	for(uint32_t i = 0; i < BufferCount; i++){
		m_buffers[i].data = (uint8_t*)memalign(32, BufferCapacity);
		if(m_buffers[i].data == 0){
			wiiLog("[WII][AUDIO][STREAM %d] buffer allocation failed "
			       "index=%u capacity=%u\n", m_voice, i, BufferCapacity);
			Shutdown();
			return false;
		}
		memset(m_buffers[i].data, 0, BufferCapacity);
		DCFlushRange(m_buffers[i].data, BufferCapacity);
		m_buffers[i].state = BUFFER_FREE;
	}
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] buffers ready count=%u "
	       "capacity=%u total=%uKB\n", m_voice, BufferCount,
	       BufferCapacity, BufferCount * BufferCapacity / 1024);
	return true;
}

void
WiiAudioStream::Shutdown()
{
	Close();
	for(uint32_t i = 0; i < BufferCount; i++){
		free(m_buffers[i].data);
		m_buffers[i].data = 0;
		m_buffers[i].size = 0;
		m_buffers[i].state = BUFFER_FREE;
	}
	m_voice = -1;
}

bool
WiiAudioStream::Open(const char *path)
{
	Close();
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] open path=%s\n", m_voice,
	       path ? path : "(null)");
	WiiAudioDecoder *decoder = CreateWiiAudioDecoder(path, false);
	if(decoder == 0){
		wiiLog("[WII][AUDIO][STREAM %d] decoder unavailable path=%s\n",
		       m_voice, path ? path : "(null)");
		return false;
	}
	if(!decoder->Open(path)){
		wiiLog("[WII][AUDIO][STREAM %d] decoder open failed path=%s\n",
		       m_voice, path ? path : "(null)");
		delete decoder;
		return false;
	}
	m_decoder = decoder;
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] open ready rate=%u channels=%u "
	       "length=%ums endian=%s\n", m_voice, m_decoder->GetSampleRate(),
	       m_decoder->GetChannels(), m_decoder->GetLength(),
	       m_decoder->GetByteOrder() == WII_PCM_LITTLE_ENDIAN ? "LE" : "BE");
	return true;
}

void
WiiAudioStream::Close()
{
	if(m_decoder != 0 && (m_prepared || m_playing))
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] close playing=%s prepared=%s "
		       "position=%ums decoded=%u queued=%u restarts=%u\n", m_voice,
		       m_playing ? "yes" : "no", m_prepared ? "yes" : "no",
		       GetPosition(), m_decodedBuffers, m_queuedBuffers,
		       m_restartCount);
	if(m_voice >= 0)
		ASND_StopVoice(m_voice);
	delete m_decoder;
	m_decoder = 0;
	ResetBuffers();
	m_startPosition = 0;
	m_timerOffset = 0;
	m_decodedBuffers = 0;
	m_queuedBuffers = 0;
	m_restartCount = 0;
	m_decoderEnded = false;
	m_prepared = false;
	m_playing = false;
	m_paused = false;
	m_voiceStarted = false;
	m_finishLogged = false;
}

bool
WiiAudioStream::Prepare(uint32_t milliseconds)
{
	if(m_decoder == 0 || m_voice < 0){
		wiiLog("[WII][AUDIO][STREAM %d] prepare rejected decoder=%p "
		       "position=%ums\n", m_voice, (void*)m_decoder, milliseconds);
		return false;
	}
	ASND_StopVoice(m_voice);
	ResetBuffers();
	if(!m_decoder->Seek(milliseconds)){
		wiiLog("[WII][AUDIO][STREAM %d] seek failed position=%ums "
		       "length=%ums\n", m_voice, milliseconds,
		       m_decoder->GetLength());
		return false;
	}
	m_startPosition = milliseconds;
	m_timerOffset = 0;
	m_decodedBuffers = 0;
	m_queuedBuffers = 0;
	m_restartCount = 0;
	m_decoderEnded = false;
	m_prepared = false;
	m_playing = false;
	m_paused = false;
	m_voiceStarted = false;
	m_finishLogged = false;
	FillAvailableBuffers(2);
	m_prepared = m_buffers[m_queueIndex].state == BUFFER_READY;
	uint32_t ready = 0;
	uint32_t bytes = 0;
	for(uint32_t i = 0; i < BufferCount; i++){
		if(m_buffers[i].state == BUFFER_READY)
			ready++;
		bytes += m_buffers[i].size;
	}
	WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] prepare position=%ums ready=%u "
	       "bytes=%u decoderEnded=%s result=%s\n", m_voice, milliseconds,
	       ready, bytes, m_decoderEnded ? "yes" : "no",
	       m_prepared ? "ok" : "failed");
	return m_prepared;
}

bool
WiiAudioStream::StartPrepared()
{
	if(!m_prepared || m_decoder == 0){
		wiiLog("[WII][AUDIO][STREAM %d] start rejected prepared=%s "
		       "decoder=%p\n", m_voice, m_prepared ? "yes" : "no",
		       (void*)m_decoder);
		return false;
	}
	m_playing = true;
	m_paused = false;
	m_timerOffset = 0;
	if(!BeginVoice()){
		wiiLog("[WII][AUDIO][STREAM %d] ASND start failed\n", m_voice);
		m_playing = false;
		return false;
	}
	return true;
}

void
WiiAudioStream::Pause(bool pause)
{
	if(!m_playing || m_voice < 0)
		return;
	if(m_paused != pause)
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] pause=%s position=%ums\n",
		       m_voice, pause ? "yes" : "no", GetPosition());
	m_paused = pause;
	if(m_voiceStarted)
		ASND_PauseVoice(m_voice, pause ? 1 : 0);
}

void
WiiAudioStream::Service()
{
	if(!m_playing || m_paused || m_decoder == 0)
		return;
	ReclaimPlayedBuffers();
	FillAvailableBuffers(1);

	if(!m_voiceStarted || ASND_StatusVoice(m_voice) == SND_UNUSED){
		if(m_voiceStarted)
			m_timerOffset += ASND_GetTimerVoice(m_voice);
		m_voiceStarted = false;
		if(m_buffers[m_queueIndex].state == BUFFER_READY){
			m_restartCount++;
			if(m_restartCount <= 4)
				wiiLog("[WII][AUDIO][STREAM %d] ASND underrun restart=%u "
				       "position=%ums\n", m_voice, m_restartCount,
				       GetPosition());
			if(!BeginVoice())
				m_playing = false;
		}else if(m_decoderEnded){
			m_playing = false;
			if(!m_finishLogged){
				WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] playback finished "
				       "position=%ums decoded=%u queued=%u restarts=%u\n",
				       m_voice, GetPosition(), m_decodedBuffers,
				       m_queuedBuffers, m_restartCount);
				m_finishLogged = true;
			}
		}
		return;
	}
	QueueNextBuffer();
}

void
WiiAudioStream::SetLoop(bool loop)
{
	if(loop && !m_loop && m_decoderEnded)
		m_decoderEnded = false;
	m_loop = loop;
}

void
WiiAudioStream::SetVolume(uint32_t left, uint32_t right)
{
	bool wasMuted = m_leftVolume == 0 && m_rightVolume == 0;
	m_leftVolume = left > 255 ? 255 : left;
	m_rightVolume = right > 255 ? 255 : right;
	if(m_voiceStarted){
		bool muted = m_leftVolume == 0 && m_rightVolume == 0;
		if(wasMuted != muted)
			WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] output %s volume=%u/%u\n",
			       m_voice, muted ? "muted" : "unmuted",
			       m_leftVolume, m_rightVolume);
		ASND_ChangeVolumeVoice(m_voice, m_leftVolume, m_rightVolume);
	}
}

uint32_t
WiiAudioStream::GetPosition() const
{
	uint32_t elapsed = m_timerOffset;
	if(m_voiceStarted)
		elapsed += ASND_GetTimerVoice(m_voice);
	uint32_t position = m_startPosition + elapsed;
	uint32_t length = GetLength();
	if(m_loop && length != 0)
		position %= length;
	else if(length != 0 && position > length)
		position = length;
	return position;
}

uint32_t
WiiAudioStream::GetLength() const
{
	return m_decoder == 0 ? 0 : m_decoder->GetLength();
}

void
WiiAudioStream::ResetBuffers()
{
	for(uint32_t i = 0; i < BufferCount; i++){
		m_buffers[i].size = 0;
		m_buffers[i].state = BUFFER_FREE;
	}
	m_fillIndex = 0;
	m_queueIndex = 0;
}

bool
WiiAudioStream::FillBuffer(Buffer &buffer)
{
	uint32_t bytes = m_decoder->Decode(buffer.data, BufferCapacity);
	if(bytes == 0 && m_loop){
		if(!m_decoder->Seek(0))
			return false;
		bytes = m_decoder->Decode(buffer.data, BufferCapacity);
	}
	if(bytes == 0)
		return false;

	uint32_t frameSize = m_decoder->GetChannels() * sizeof(uint16_t);
	bytes -= bytes % frameSize;
	if(bytes == 0)
		return false;
	uint32_t flushSize = (bytes + 31u) & ~31u;
	DCFlushRange(buffer.data, flushSize);
	buffer.size = bytes;
	buffer.state = BUFFER_READY;
	m_decodedBuffers++;
	return true;
}

void
WiiAudioStream::FillAvailableBuffers(uint32_t maxBuffers)
{
	uint32_t filled = 0;
	while(!m_decoderEnded && filled < maxBuffers &&
	      m_buffers[m_fillIndex].state == BUFFER_FREE){
		if(!FillBuffer(m_buffers[m_fillIndex])){
			m_decoderEnded = true;
			WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] decoder ended tell=%ums "
			       "length=%ums decoded=%u loop=%s\n", m_voice,
			       m_decoder->Tell(), m_decoder->GetLength(),
			       m_decodedBuffers, m_loop ? "yes" : "no");
			break;
		}
		m_fillIndex = (m_fillIndex + 1) % BufferCount;
		filled++;
	}
}

void
WiiAudioStream::ReclaimPlayedBuffers()
{
	if(!m_voiceStarted)
		return;
	for(uint32_t i = 0; i < BufferCount; i++){
		Buffer &buffer = m_buffers[i];
		if(buffer.state == BUFFER_QUEUED &&
		   ASND_TestPointer(m_voice, buffer.data) <= 0){
			buffer.size = 0;
			buffer.state = BUFFER_FREE;
		}
	}
}

bool
WiiAudioStream::BeginVoice()
{
	Buffer &buffer = m_buffers[m_queueIndex];
	if(buffer.state != BUFFER_READY)
		return false;
	int32_t result = ASND_SetVoice(m_voice, GetAsndFormat(),
	                               m_decoder->GetSampleRate(), 0,
	                               buffer.data, buffer.size,
	                               m_leftVolume, m_rightVolume, 0);
	if(result != SND_OK){
		wiiLog("[WII][AUDIO][STREAM %d] ASND_SetVoice result=%d "
		       "format=%d rate=%u bytes=%u volume=%u/%u\n", m_voice,
		       result, GetAsndFormat(), m_decoder->GetSampleRate(),
		       buffer.size, m_leftVolume, m_rightVolume);
		return false;
	}
	buffer.state = BUFFER_QUEUED;
	m_queuedBuffers++;
	m_queueIndex = (m_queueIndex + 1) % BufferCount;
	m_voiceStarted = true;
	if(m_queuedBuffers == 1 || m_restartCount <= 4)
		WII_AUDIO_TRACE_LOG("[WII][AUDIO][STREAM %d] ASND voice started format=%d "
		       "rate=%u channels=%u bytes=%u volume=%u/%u loop=%s "
		       "status=%d\n", m_voice, GetAsndFormat(),
		       m_decoder->GetSampleRate(), m_decoder->GetChannels(),
		       buffer.size, m_leftVolume, m_rightVolume,
		       m_loop ? "yes" : "no", ASND_StatusVoice(m_voice));
	QueueNextBuffer();
	return true;
}

void
WiiAudioStream::QueueNextBuffer()
{
	while(ASND_TestVoiceBufferReady(m_voice) == 1){
		Buffer &buffer = m_buffers[m_queueIndex];
		if(buffer.state != BUFFER_READY)
			return;
		int32_t result = ASND_AddVoice(m_voice, buffer.data, buffer.size);
		if(result != SND_OK){
			wiiLog("[WII][AUDIO][STREAM %d] ASND_AddVoice result=%d "
			       "bytes=%u ready=%d\n", m_voice, result, buffer.size,
			       ASND_TestVoiceBufferReady(m_voice));
			return;
		}
		buffer.state = BUFFER_QUEUED;
		m_queuedBuffers++;
		m_queueIndex = (m_queueIndex + 1) % BufferCount;
	}
}

int32_t
WiiAudioStream::GetAsndFormat() const
{
	if(m_decoder->GetChannels() == 1)
		return m_decoder->GetByteOrder() == WII_PCM_LITTLE_ENDIAN ?
		       VOICE_MONO_16BIT_LE : VOICE_MONO_16BIT_BE;
	return m_decoder->GetByteOrder() == WII_PCM_LITTLE_ENDIAN ?
	       VOICE_STEREO_16BIT_LE : VOICE_STEREO_16BIT_BE;
}

#endif
