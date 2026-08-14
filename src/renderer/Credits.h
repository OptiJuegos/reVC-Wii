#ifndef __GTA_CREDITS_H__
#define __GTA_CREDITS_H__

class CCredits
{
	static bool bCreditsGoing;
	static uint32 CreditsStartTime;
public:
	static void Init(void);
	static void Start(void);
	static void Stop(void);
	static bool AreCreditsDone(void);
	static void Render(void);
	static void PrintCreditSpace(float space, uint32 &line);
	static void PrintCreditText(float scaleX, float scaleY, wchar *text, uint32 &lineoffset, float scrolloffset);
};

#endif // __GTA_CREDITS_H__
