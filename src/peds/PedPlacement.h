#ifndef __GTA_PEDPLACEMENT_H__
#define __GTA_PEDPLACEMENT_H__

class CPedPlacement {
public:
	static bool FindZCoorForPed(CVector* pos);
	static CEntity* IsPositionClearOfCars(Const CVector*);
	static bool IsPositionClearForPed(const CVector& pos, float radius = -1.0f, int total = -1, CEntity** entities = nil);
};

#endif // __GTA_PEDPLACEMENT_H__
