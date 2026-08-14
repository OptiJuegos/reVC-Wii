#ifdef AUDIO_WII

#include <malloc.h>
#include <mpg123.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

#include "common.h"
#include "crossplatform.h"
#include "wii-port/WiiLog.h"
#include "WiiAudioDecoder.h"

namespace {

const uint32_t InputBufferSize = 64 * 1024;

static uint16_t
readLittle16(const uint8_t *data)
{
	return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t
readLittle32(const uint8_t *data)
{
	return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
	       (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static bool
hasExtension(const char *path, const char *extension)
{
	size_t pathLength = strlen(path);
	size_t extensionLength = strlen(extension);
	return pathLength >= extensionLength &&
	       strcasecmp(path + pathLength - extensionLength, extension) == 0;
}

static uint32_t
samplesToMilliseconds(int64_t samples, uint32_t sampleRate)
{
	if(samples <= 0 || sampleRate == 0)
		return 0;
	return (uint32_t)((uint64_t)samples * 1000u / sampleRate);
}

struct MpegInput
{
	FILE *file;
	bool obfuscated;
};

static mpg123_ssize_t
readMpegInput(void *handle, void *destination, size_t size)
{
	MpegInput *input = (MpegInput*)handle;
	if(input == NULL || input->file == NULL)
		return -1;
	size_t bytesRead = fread(destination, 1, size, input->file);
	if(input->obfuscated){
		uint8_t *bytes = (uint8_t*)destination;
		for(size_t i = 0; i < bytesRead; i++)
			bytes[i] ^= 0x22;
	}
	return (mpg123_ssize_t)bytesRead;
}

static off_t
seekMpegInput(void *handle, off_t offset, int origin)
{
	MpegInput *input = (MpegInput*)handle;
	long fileOffset = (long)offset;
	if(input == NULL || input->file == NULL ||
	   (off_t)fileOffset != offset ||
	   fseek(input->file, fileOffset, origin) != 0)
		return (off_t)-1;
	return (off_t)ftell(input->file);
}

static void
closeMpegInput(void *handle)
{
	MpegInput *input = (MpegInput*)handle;
	if(input != NULL && input->file != NULL){
		fclose(input->file);
		input->file = NULL;
	}
}

class WiiMpegDecoder : public WiiAudioDecoder
{
public:
	WiiMpegDecoder(bool obfuscated, bool trace)
	 : m_handle(NULL), m_ioBuffer(NULL), m_sampleRate(0), m_channels(0),
	   m_open(false), m_trace(trace)
	{
		m_input.file = NULL;
		m_input.obfuscated = obfuscated;
	}

	~WiiMpegDecoder() { Close(); }

	bool Open(const char *path)
	{
		Close();
		if(path == NULL){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] MPEG path is null\n");
			return false;
		}
		if(m_trace)
			wiiLog("[WII][AUDIO][DECODER] MPEG open path=%s adf=%s\n",
			       path, m_input.obfuscated ? "yes" : "no");

		m_handle = mpg123_new(NULL, NULL);
		m_input.file = fcaseopen(path, "rb");
		m_ioBuffer = (uint8_t*)memalign(64, InputBufferSize);
		if(m_handle == NULL || m_input.file == NULL || m_ioBuffer == NULL){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] MPEG resources path=%s "
				       "handle=%p file=%p io=%p\n", path, (void*)m_handle,
				       (void*)m_input.file, (void*)m_ioBuffer);
			Close();
			return false;
		}
		if(setvbuf(m_input.file, (char*)m_ioBuffer, _IOFBF,
		           InputBufferSize) != 0){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] MPEG setvbuf failed path=%s\n",
				       path);
			Close();
			return false;
		}

		mpg123_param(m_handle, MPG123_ADD_FLAGS,
		             MPG123_GAPLESS | MPG123_FUZZY | MPG123_SKIP_ID3V2,
		             0.0);
		if(mpg123_format_none(m_handle) != MPG123_OK){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] MPEG format reset failed "
				       "path=%s error=%s\n", path,
				       mpg123_strerror(m_handle));
			Close();
			return false;
		}
		const long *rates = NULL;
		size_t rateCount = 0;
		mpg123_rates(&rates, &rateCount);
		for(size_t i = 0; i < rateCount; i++)
			mpg123_format(m_handle, rates[i], MPG123_MONO | MPG123_STEREO,
			              MPG123_ENC_SIGNED_16);

		int result = mpg123_replace_reader_handle(m_handle, readMpegInput,
		                                          seekMpegInput,
		                                          closeMpegInput);
		if(result == MPG123_OK)
			result = mpg123_open_handle(m_handle, &m_input);
		if(result != MPG123_OK){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] MPEG input failed path=%s "
				       "result=%d error=%s\n", path, result,
				       mpg123_strerror(m_handle));
			Close();
			return false;
		}

		long sampleRate = 0;
		int channels = 0;
		int encoding = 0;
		if(mpg123_getformat(m_handle, &sampleRate, &channels, &encoding) !=
		     MPG123_OK ||
		   sampleRate <= 0 || (channels != 1 && channels != 2) ||
		   encoding != MPG123_ENC_SIGNED_16){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] MPEG unsupported format "
				       "path=%s rate=%ld channels=%d encoding=0x%x error=%s\n",
				       path, sampleRate, channels, encoding,
				       mpg123_strerror(m_handle));
			Close();
			return false;
		}

		m_sampleRate = (uint32_t)sampleRate;
		m_channels = (uint32_t)channels;
		m_open = true;
		if(m_trace)
			wiiLog("[WII][AUDIO][DECODER] MPEG ready path=%s rate=%u "
			       "channels=%u length=%ums endian=BE\n", path,
			       m_sampleRate, m_channels, GetLength());
		return true;
	}

	void Close()
	{
		if(m_handle != NULL){
			mpg123_close(m_handle);
			mpg123_delete(m_handle);
			m_handle = NULL;
		}
		if(m_input.file != NULL){
			fclose(m_input.file);
			m_input.file = NULL;
		}
		free(m_ioBuffer);
		m_ioBuffer = NULL;
		m_sampleRate = 0;
		m_channels = 0;
		m_open = false;
	}

	uint32_t Decode(void *buffer, uint32_t capacity)
	{
		if(!m_open || buffer == NULL || capacity == 0)
			return 0;
		size_t bytesDecoded = 0;
		int result = mpg123_read(m_handle, (unsigned char*)buffer, capacity,
		                         &bytesDecoded);
		if(result != MPG123_OK && result != MPG123_DONE)
			return 0;
		return (uint32_t)bytesDecoded;
	}

	bool Seek(uint32_t milliseconds)
	{
		if(!m_open || m_sampleRate == 0)
			return false;
		off_t sample = (off_t)((uint64_t)milliseconds * m_sampleRate / 1000u);
		return mpg123_seek(m_handle, sample, SEEK_SET) >= 0;
	}

	uint32_t Tell() const
	{
		if(!m_open)
			return 0;
		return samplesToMilliseconds(mpg123_tell(m_handle), m_sampleRate);
	}

	uint32_t GetLength() const
	{
		if(!m_open)
			return 0;
		return samplesToMilliseconds(mpg123_length(m_handle), m_sampleRate);
	}

	uint32_t GetSampleRate() const { return m_sampleRate; }
	uint32_t GetChannels() const { return m_channels; }
	WiiPcmByteOrder GetByteOrder() const { return WII_PCM_BIG_ENDIAN; }

private:
	mpg123_handle *m_handle;
	MpegInput m_input;
	uint8_t *m_ioBuffer;
	uint32_t m_sampleRate;
	uint32_t m_channels;
	bool m_open;
	bool m_trace;
};

const uint16_t ImaStepTable[89] = {
	7, 8, 9, 10, 11, 12, 13, 14,
	16, 17, 19, 21, 23, 25, 28, 31,
	34, 37, 41, 45, 50, 55, 60, 66,
	73, 80, 88, 97, 107, 118, 130, 143,
	157, 173, 190, 209, 230, 253, 279, 307,
	337, 371, 408, 449, 494, 544, 598, 658,
	724, 796, 876, 963, 1060, 1166, 1282, 1411,
	1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
	3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
	7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
	15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767
};

struct ImaState
{
	int32_t sample;
	int32_t stepIndex;
};

static int16_t
decodeImaSample(ImaState &state, uint8_t adpcm)
{
	uint16_t step = ImaStepTable[state.stepIndex];
	if((adpcm & 4) != 0)
		state.stepIndex += ((adpcm & 3) + 1) * 2;
	else
		state.stepIndex--;
	if(state.stepIndex < 0)
		state.stepIndex = 0;
	if(state.stepIndex > 88)
		state.stepIndex = 88;

	int32_t delta = step >> 3;
	if((adpcm & 1) != 0)
		delta += step >> 2;
	if((adpcm & 2) != 0)
		delta += step >> 1;
	if((adpcm & 4) != 0)
		delta += step;
	if((adpcm & 8) != 0)
		delta = -delta;
	state.sample += delta;
	if(state.sample < -32768)
		state.sample = -32768;
	if(state.sample > 32767)
		state.sample = 32767;
	return (int16_t)state.sample;
}

class WiiWavDecoder : public WiiAudioDecoder
{
public:
	explicit WiiWavDecoder(bool trace)
	 : m_file(NULL), m_ioBuffer(NULL), m_dataOffset(0), m_dataSize(0),
	   m_sampleRate(0), m_channels(0), m_blockSize(0), m_encoding(0),
	   m_samplesPerBlock(0), m_adpcmBuffer(NULL), m_trace(trace)
	{
	}

	~WiiWavDecoder() { Close(); }

	bool Open(const char *path)
	{
		Close();
		if(m_trace)
			wiiLog("[WII][AUDIO][DECODER] WAV open path=%s\n",
			       path ? path : "(null)");
		if(path == NULL)
			return false;
		m_file = fcaseopen(path, "rb");
		m_ioBuffer = (uint8_t*)memalign(64, InputBufferSize);
		if(m_file == NULL || m_ioBuffer == NULL ||
		   setvbuf(m_file, (char*)m_ioBuffer, _IOFBF, InputBufferSize) != 0){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] WAV resources failed path=%s "
				       "file=%p io=%p\n", path, (void*)m_file,
				       (void*)m_ioBuffer);
			Close();
			return false;
		}

		uint8_t header[12];
		if(fread(header, 1, sizeof(header), m_file) != sizeof(header) ||
		   memcmp(header, "RIFF", 4) != 0 ||
		   memcmp(header + 8, "WAVE", 4) != 0){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] WAV invalid RIFF path=%s\n",
				       path);
			Close();
			return false;
		}

		bool foundFormat = false;
		bool foundData = false;
		while(!foundData){
			uint8_t chunkHeader[8];
			if(fread(chunkHeader, 1, sizeof(chunkHeader), m_file) !=
			   sizeof(chunkHeader))
				break;
			uint32_t chunkSize = readLittle32(chunkHeader + 4);
			if(memcmp(chunkHeader, "fmt ", 4) == 0){
				uint8_t format[16];
				if(chunkSize < sizeof(format) ||
				   fread(format, 1, sizeof(format), m_file) != sizeof(format))
					break;
				m_encoding = readLittle16(format);
				m_channels = readLittle16(format + 2);
				m_sampleRate = readLittle32(format + 4);
				uint16_t blockSize = readLittle16(format + 12);
				uint16_t bits = readLittle16(format + 14);
				if((m_channels != 1 && m_channels != 2) ||
				   m_sampleRate == 0 || blockSize == 0)
					break;
				m_blockSize = blockSize;
				uint32_t bytesPerChannel = m_blockSize / m_channels;
				if(m_encoding == 1 && bits == 16 &&
				   m_blockSize == m_channels * sizeof(uint16_t))
					m_samplesPerBlock = 1;
				else if((m_encoding == 0x11 || m_encoding == 0x69) &&
				        bits == 4 &&
				        m_blockSize % m_channels == 0 &&
				        bytesPerChannel > 4 &&
				        (bytesPerChannel - 4) % 4 == 0){
					m_samplesPerBlock =
						(bytesPerChannel - 4) * 2 + 1;
					m_adpcmBuffer = (uint8_t*)malloc(m_blockSize);
					if(m_adpcmBuffer == NULL)
						break;
				}else
					break;
				uint32_t remaining = chunkSize - sizeof(format);
				if(remaining != 0 && fseek(m_file, remaining, SEEK_CUR) != 0)
					break;
				foundFormat = true;
			}else if(memcmp(chunkHeader, "data", 4) == 0){
				if(!foundFormat)
					break;
				m_dataOffset = ftell(m_file);
				m_dataSize = chunkSize - chunkSize % m_blockSize;
				foundData = m_dataOffset >= 0;
				break;
			}else if(fseek(m_file, chunkSize, SEEK_CUR) != 0){
				break;
			}
			if((chunkSize & 1u) != 0 && fseek(m_file, 1, SEEK_CUR) != 0)
				break;
		}

		if(!foundFormat || !foundData || m_dataSize == 0){
			if(m_trace)
				wiiLog("[WII][AUDIO][DECODER] WAV parse failed path=%s "
				       "format=%s data=%s encoding=0x%x rate=%u channels=%u "
				       "block=%u bytes=%u\n", path,
				       foundFormat ? "yes" : "no",
				       foundData ? "yes" : "no", m_encoding, m_sampleRate,
				       m_channels, m_blockSize, m_dataSize);
			Close();
			return false;
		}
		if(m_trace)
			wiiLog("[WII][AUDIO][DECODER] WAV ready path=%s encoding=0x%x "
			       "rate=%u channels=%u block=%u length=%ums endian=%s\n",
			       path, m_encoding, m_sampleRate, m_channels, m_blockSize,
			       GetLength(), GetByteOrder() == WII_PCM_LITTLE_ENDIAN ?
			       "LE" : "BE");
		return true;
	}

	void Close()
	{
		if(m_file != NULL)
			fclose(m_file);
		m_file = NULL;
		free(m_ioBuffer);
		m_ioBuffer = NULL;
		m_dataOffset = 0;
		m_dataSize = 0;
		m_sampleRate = 0;
		m_channels = 0;
		m_blockSize = 0;
		m_encoding = 0;
		m_samplesPerBlock = 0;
		free(m_adpcmBuffer);
		m_adpcmBuffer = NULL;
	}

	uint32_t Decode(void *buffer, uint32_t capacity)
	{
		if(m_file == NULL || buffer == NULL || m_blockSize == 0)
			return 0;
		long position = ftell(m_file);
		if(position < m_dataOffset)
			return 0;
		uint32_t consumed = (uint32_t)(position - m_dataOffset);
		if(consumed >= m_dataSize)
			return 0;
		if(m_encoding == 1){
			uint32_t size = capacity - capacity % m_blockSize;
			if(size > m_dataSize - consumed)
				size = m_dataSize - consumed;
			uint32_t bytesRead = (uint32_t)fread(buffer, 1, size, m_file);
			return bytesRead - bytesRead % m_blockSize;
		}

		uint32_t frameSize = m_channels * sizeof(int16_t);
		uint32_t blocks = capacity / (m_samplesPerBlock * frameSize);
		uint32_t blocksLeft = (m_dataSize - consumed) / m_blockSize;
		if(blocks > blocksLeft)
			blocks = blocksLeft;
		int16_t *output = (int16_t*)buffer;
		uint32_t framesDecoded = 0;
		for(uint32_t block = 0; block < blocks; block++){
			if(fread(m_adpcmBuffer, 1, m_blockSize, m_file) != m_blockSize)
				break;
			ImaState states[2];
			uint8_t *input = m_adpcmBuffer;
			for(uint32_t channel = 0; channel < m_channels; channel++){
				states[channel].sample = (int16_t)readLittle16(input);
				states[channel].stepIndex = input[2] > 88 ? 88 : input[2];
				output[(framesDecoded * m_channels) + channel] =
					(int16_t)states[channel].sample;
				input += 4;
			}

			for(uint32_t frame = 1; frame < m_samplesPerBlock; frame += 8){
				uint32_t groupFrames = m_samplesPerBlock - frame;
				if(groupFrames > 8)
					groupFrames = 8;
				for(uint32_t channel = 0; channel < m_channels; channel++){
					for(uint32_t sample = 0; sample < groupFrames; sample++){
						uint8_t packed = input[sample / 2];
						uint8_t nibble = (sample & 1) == 0 ?
						                 packed & 0x0F : packed >> 4;
						output[((framesDecoded + frame + sample) *
						        m_channels) + channel] =
							decodeImaSample(states[channel], nibble);
					}
					input += 4;
				}
			}
			framesDecoded += m_samplesPerBlock;
		}
		return framesDecoded * frameSize;
	}

	bool Seek(uint32_t milliseconds)
	{
		if(m_file == NULL || m_blockSize == 0)
			return false;
		uint64_t frames = (uint64_t)milliseconds * m_sampleRate / 1000u;
		uint64_t offset = frames / m_samplesPerBlock * m_blockSize;
		if(offset > m_dataSize)
			offset = m_dataSize;
		return fseek(m_file, m_dataOffset + (long)offset, SEEK_SET) == 0;
	}

	uint32_t Tell() const
	{
		if(m_file == NULL || m_sampleRate == 0 || m_blockSize == 0)
			return 0;
		long position = ftell(m_file);
		if(position <= m_dataOffset)
			return 0;
		uint32_t frames = (uint32_t)(position - m_dataOffset) /
		                  m_blockSize * m_samplesPerBlock;
		return (uint32_t)((uint64_t)frames * 1000u / m_sampleRate);
	}

	uint32_t GetLength() const
	{
		if(m_sampleRate == 0 || m_blockSize == 0)
			return 0;
		uint32_t frames = m_dataSize / m_blockSize * m_samplesPerBlock;
		return (uint32_t)((uint64_t)frames * 1000u / m_sampleRate);
	}

	uint32_t GetSampleRate() const { return m_sampleRate; }
	uint32_t GetChannels() const { return m_channels; }
	WiiPcmByteOrder GetByteOrder() const
	{
		return m_encoding == 1 ? WII_PCM_LITTLE_ENDIAN :
		       WII_PCM_BIG_ENDIAN;
	}

private:
	FILE *m_file;
	uint8_t *m_ioBuffer;
	long m_dataOffset;
	uint32_t m_dataSize;
	uint32_t m_sampleRate;
	uint32_t m_channels;
	uint32_t m_blockSize;
	uint32_t m_encoding;
	uint32_t m_samplesPerBlock;
	uint8_t *m_adpcmBuffer;
	bool m_trace;
};

}

bool
InitialiseWiiAudioDecoders()
{
	int result = mpg123_init();
	wiiLog("[WII][AUDIO][DECODER] mpg123 init result=%d\n", result);
	return result == MPG123_OK;
}

void
TerminateWiiAudioDecoders()
{
	mpg123_exit();
}

WiiAudioDecoder*
CreateWiiAudioDecoder(const char *path, bool trace)
{
	if(path == NULL)
		return NULL;
	if(hasExtension(path, ".adf"))
		return new (std::nothrow) WiiMpegDecoder(true, trace);
	if(hasExtension(path, ".mp3"))
		return new (std::nothrow) WiiMpegDecoder(false, trace);
	if(hasExtension(path, ".wav"))
		return new (std::nothrow) WiiWavDecoder(trace);
	if(trace)
		wiiLog("[WII][AUDIO][DECODER] unsupported extension path=%s\n",
		       path);
	return NULL;
}

#endif
