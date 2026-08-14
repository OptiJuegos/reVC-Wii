#ifndef __GTA_PLAYERSKIN_H__
#define __GTA_PLAYERSKIN_H__

#define DEFAULT_SKIN_NAME "$$\"\""

class CPlayerSkin
{
	static int m_txdSlot;
public:
	static void Initialise();
	static void Shutdown();
	static RwTexture *GetSkinTexture(const char *texName);
	static void BeginFrontendSkinEdit();
	static void EndFrontendSkinEdit();
	static void RenderFrontendSkinEdit();
};

#endif // __GTA_PLAYERSKIN_H__
