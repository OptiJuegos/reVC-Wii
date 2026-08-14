#ifndef __GTA_TIMEBARS_H__
#define __GTA_TIMEBARS_H__

#ifdef TIMEBARS
void tbInit();
void tbStartTimer(int32, Const char*);
void tbEndTimer(Const char*);
void tbDisplay();
#else
#define tbInit()
#define tbStartTimer(a, b)
#define tbEndTimer(a)
#define tbDisplay()
#endif

#endif // __GTA_TIMEBARS_H__
