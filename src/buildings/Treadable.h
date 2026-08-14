#ifndef __GTA_TREADABLE_H__
#define __GTA_TREADABLE_H__

#include "Building.h"

class CTreadable : public CBuilding
{
public:
	static void *operator new(size_t) throw();
	static void operator delete(void*, size_t) throw();

	bool GetIsATreadable(void) { return true; }
};

#endif // __GTA_TREADABLE_H__
