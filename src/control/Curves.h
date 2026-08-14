#ifndef __GTA_CURVES_H__
#define __GTA_CURVES_H__
class CVector;

class CCurves
{
public:
	static float CalcSpeedScaleFactor(CVector*, CVector*, float, float, float, float);
	static void CalcCurvePoint(CVector*, CVector*, CVector*, CVector*, float, int32, CVector*, CVector*);
};

#endif // __GTA_CURVES_H__
