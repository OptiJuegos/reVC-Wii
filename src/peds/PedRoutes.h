#ifndef __GTA_PEDROUTES_H__
#define __GTA_PEDROUTES_H__

class CRouteNode
{
public:
	int16 m_route;
	CVector m_pos;

	static int16 GetRouteThisPointIsOn(int16);
	static CVector GetPointPosition(int16);
	static int16 GetRouteStart(int16);
	static void AddRoutePoint(int16, CVector);
	static void RemoveRoute(int16);
	static void Initialise(void);
};

#endif // __GTA_PEDROUTES_H__
