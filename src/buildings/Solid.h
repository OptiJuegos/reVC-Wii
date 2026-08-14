#ifndef __GTA_SOLID_H__
#define __GTA_SOLID_H__

#include "Entity.h"

class CSolid : public CEntity
{
public:
	CSolid(void) {
		m_type = ENTITY_TYPE_BUILDING;
		bUsesCollision = true;
	}
};

#endif // __GTA_SOLID_H__
