#ifndef __GTA_TEXLIST_H__
#define __GTA_TEXLIST_H__

class CTexList
{
	enum { MAX_TEXUSED = 400, };
	static bool ms_nTexUsed[MAX_TEXUSED];
public:
	static void Initialise();
	static void Shutdown();
	static RwTexture *SetTexture(int32 slot, char *name);
	static int32 GetFirstFreeTexture();
	static RwTexture *LoadFileNameTexture(char *name);
	static void LoadGlobalTextureList();
};

#endif // __GTA_TEXLIST_H__
