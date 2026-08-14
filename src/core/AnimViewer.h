#ifndef __GTA_ANIMVIEWER_H__
#define __GTA_ANIMVIEWER_H__

class CAnimViewer {
public:
	static int animTxdSlot;
	static CEntity *pTarget;
	
	static void Initialise();
	static void Render();
	static void Shutdown();
	static void Update();
};

#endif // __GTA_ANIMVIEWER_H__
