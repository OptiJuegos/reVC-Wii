#ifndef __GTA_DUMMYOBJECT_H__
#define __GTA_DUMMYOBJECT_H__

#include "Dummy.h"

class CObject;

class CDummyObject : public CDummy
{
public:
	CDummyObject(void) {}
	CDummyObject(CObject *obj);
};

#endif // __GTA_DUMMYOBJECT_H__
