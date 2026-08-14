#ifndef __GTA_BUILDING_H__
#define __GTA_BUILDING_H__

#include "Entity.h"

class CBuilding : public CEntity
{
public:
	CBuilding(void) {
		m_type = ENTITY_TYPE_BUILDING;
		bUsesCollision = true;
	}
	static void *operator new(size_t) throw();
	static void operator delete(void*, size_t) throw();

	void ReplaceWithNewModel(int32 id);

	virtual bool GetIsATreadable(void) { return false; }
};

bool IsBuildingPointerValid(CBuilding*);

#endif // __GTA_BUILDING_H__
