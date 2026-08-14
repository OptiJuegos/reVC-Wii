#ifdef AUDIO_WII

#include <gccore.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "WiiSfxCache.h"

namespace {

static uint32_t
align32(uint32_t size)
{
	return (size + 31u) & ~31u;
}

}

WiiSfxCache::WiiSfxCache()
 : m_file(NULL), m_entries(NULL), m_sampleCount(0), m_budget(0),
	m_residentBytes(0), m_useSerial(0), m_hits(0), m_misses(0),
	m_evictions(0)
{
}

WiiSfxCache::~WiiSfxCache()
{
	Shutdown();
}

bool
WiiSfxCache::Initialise(FILE *file, uint32_t sampleCount, uint32_t budget)
{
	Shutdown();
	if(file == NULL || sampleCount == 0 || budget < 32)
		return false;

	m_entries = (Entry*)calloc(sampleCount, sizeof(Entry));
	if(m_entries == NULL)
		return false;

	m_file = file;
	m_sampleCount = sampleCount;
	m_budget = budget & ~31u;
	return true;
}

void
WiiSfxCache::Shutdown()
{
	if(m_entries != NULL){
		for(uint32_t i = 0; i < m_sampleCount; i++)
			free(m_entries[i].data);
		free(m_entries);
	}
	m_file = NULL;
	m_entries = NULL;
	m_sampleCount = 0;
	m_budget = 0;
	m_residentBytes = 0;
	m_useSerial = 0;
	m_hits = 0;
	m_misses = 0;
	m_evictions = 0;
}

const uint8_t*
WiiSfxCache::Acquire(uint32_t sample, uint32_t offset, uint32_t size)
{
	if(m_entries == NULL || sample >= m_sampleCount || size == 0)
		return NULL;

	Entry &entry = m_entries[sample];
	entry.lastUse = ++m_useSerial;
	if(entry.data != NULL){
		if(entry.dataSize != size)
			return NULL;
		entry.pins++;
		m_hits++;
		return entry.data;
	}

	uint32_t allocationSize = align32(size);
	if(allocationSize > m_budget || !MakeRoom(allocationSize))
		return NULL;

	uint8_t *data = (uint8_t*)memalign(32, allocationSize);
	if(data == NULL)
		return NULL;
	if(fseek(m_file, (long)offset, SEEK_SET) != 0 ||
	   fread(data, 1, size, m_file) != size){
		free(data);
		return NULL;
	}
	if(allocationSize > size)
		memset(data + size, 0, allocationSize - size);
	DCFlushRange(data, allocationSize);

	entry.data = data;
	entry.allocationSize = allocationSize;
	entry.dataSize = size;
	entry.pins = 1;
	m_residentBytes += allocationSize;
	m_misses++;
	return entry.data;
}

void
WiiSfxCache::Release(uint32_t sample)
{
	if(m_entries == NULL || sample >= m_sampleCount)
		return;
	Entry &entry = m_entries[sample];
	if(entry.pins > 0)
		entry.pins--;
}

bool
WiiSfxCache::MakeRoom(uint32_t required)
{
	while(m_residentBytes > m_budget - required)
		if(!EvictLeastRecentlyUsed())
			return false;
	return true;
}

bool
WiiSfxCache::EvictLeastRecentlyUsed()
{
	Entry *victim = NULL;
	uint32_t victimAge = 0;
	for(uint32_t i = 0; i < m_sampleCount; i++){
		Entry &entry = m_entries[i];
		if(entry.data == NULL || entry.pins != 0)
			continue;
		uint32_t age = m_useSerial - entry.lastUse;
		if(victim == NULL || age > victimAge){
			victim = &entry;
			victimAge = age;
		}
	}
	if(victim == NULL)
		return false;
	Evict(*victim);
	return true;
}

void
WiiSfxCache::Evict(Entry &entry)
{
	free(entry.data);
	m_residentBytes -= entry.allocationSize;
	memset(&entry, 0, sizeof(entry));
	m_evictions++;
}

#endif
