#ifndef __GTA_INSTANCE_H__
#define __GTA_INSTANCE_H__

#include "Placeable.h"

// unused

class CInstance : public CPlaceable
{
public:
	int m_modelIndex;
public:
	~CInstance() { }
	void Shutdown();
};

#endif // __GTA_INSTANCE_H__
