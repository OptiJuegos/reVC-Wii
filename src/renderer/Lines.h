#ifndef __GTA_LINES_H__
#define __GTA_LINES_H__

class CLines
{
public:
	static void RenderLineWithClipping(float x1, float y1, float z1, float x2, float y2, float z2, uint32 c1, uint32 c2);
};

#endif // __GTA_LINES_H__
