#pragma once

#ifdef AUDIO_WII

#include <stdint.h>
#include <stdio.h>

class WiiSfxCache
{
public:
	WiiSfxCache();
	~WiiSfxCache();

	bool Initialise(FILE *file, uint32_t sampleCount, uint32_t budget);
	void Shutdown();

	const uint8_t *Acquire(uint32_t sample, uint32_t offset, uint32_t size);
	void Release(uint32_t sample);

	uint32_t GetBudget() const { return m_budget; }
	uint32_t GetResidentBytes() const { return m_residentBytes; }
	uint32_t GetHits() const { return m_hits; }
	uint32_t GetMisses() const { return m_misses; }
	uint32_t GetEvictions() const { return m_evictions; }

private:
	struct Entry
	{
		uint8_t *data;
		uint32_t allocationSize;
		uint32_t dataSize;
		uint32_t lastUse;
		uint32_t pins;
	};

	bool MakeRoom(uint32_t required);
	bool EvictLeastRecentlyUsed();
	void Evict(Entry &entry);

	FILE *m_file;
	Entry *m_entries;
	uint32_t m_sampleCount;
	uint32_t m_budget;
	uint32_t m_residentBytes;
	uint32_t m_useSerial;
	uint32_t m_hits;
	uint32_t m_misses;
	uint32_t m_evictions;
};

#endif
