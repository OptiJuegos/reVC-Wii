#ifndef __GTA_MLOMODELINFO_H__
#define __GTA_MLOMODELINFO_H__

#include "ClumpModelInfo.h"

class CMloModelInfo : public CClumpModelInfo
{
public:
	float drawDist;
	int firstInstance;
	int lastInstance;
public:
	CMloModelInfo(void) : CClumpModelInfo(MITYPE_MLO) {}
	void ConstructClump();
};

#endif // __GTA_MLOMODELINFO_H__
