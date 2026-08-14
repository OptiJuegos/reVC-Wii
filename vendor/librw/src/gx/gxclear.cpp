#include <ogc/gx.h>

#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rwgx.h"

namespace rw {
namespace gx {

static void
submitClearVertex(float32 x, float32 y, float32 z, const RGBA &color)
{
	GX_Position3f32(x, y, z);
	GX_Color4u8(color.red, color.green, color.blue, color.alpha);
}

void
clearNativeCamera(int32 x, int32 y, int32 width, int32 height,
	const RGBA &color, bool32 clearColor, bool32 clearDepth,
	float32 nearPlane, float32 farPlane)
{
	if(width <= 0 || height <= 0 || (!clearColor && !clearDepth))
		return;

	GX_SetViewport(x, y, width, height, 0.0f, 1.0f);
	GX_SetScissor(x, y, width, height);
	GX_SetZMode(clearDepth, GX_ALWAYS, clearDepth);
	GX_SetZCompLoc(GX_ENABLE);
	GX_SetColorUpdate(clearColor);
	GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GXColor fogColor = { 0, 0, 0, 0 };
	GX_SetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, fogColor);

	loadNativeIm2DMatrices(width, height, nearPlane, farPlane);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	RGBA white = { 255, 255, 255, 255 };
	setupNativeTev(white, nil);

	float32 right = width - 0.5f;
	float32 bottom = height - 0.5f;
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	submitClearVertex(-0.5f, -0.5f, farPlane, color);
	submitClearVertex(-0.5f, bottom, farPlane, color);
	submitClearVertex(right, bottom, farPlane, color);
	submitClearVertex(right, -0.5f, farPlane, color);
	GX_End();
	invalidateNativeRenderState();
}

}
}
