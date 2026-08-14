#include <ogc/gx.h>

#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "../rwrender.h"

#include "rwgx.h"

namespace rw {
namespace gx {

enum NativeStateDirty
{
	NATIVE_BLEND = 1 << 0,
	NATIVE_DEPTH = 1 << 1,
	NATIVE_FOG = 1 << 2,
	NATIVE_CULL = 1 << 3,
	NATIVE_ALPHA_TEST = 1 << 4,
	NATIVE_VIEWPORT = 1 << 5,
	NATIVE_STATE_ALL = (1 << 6) - 1
};

struct NativeRenderState
{
	bool32 blendEnable;
	uint32 sourceBlend;
	uint32 destinationBlend;
	bool32 zTest;
	bool32 zWrite;
	bool32 fogEnable;
	uint32 fogColor;
	float32 fogStart;
	float32 fogEnd;
	float32 nearPlane;
	float32 farPlane;
	bool32 perspective;
	uint32 cullMode;
	uint32 alphaTestFunction;
	uint32 alphaTestReference;
	int32 viewportX;
	int32 viewportY;
	int32 viewportWidth;
	int32 viewportHeight;
	uint32 dirty;
};

static NativeRenderState nativeState;

static uint8
nativeBlendFunction(uint32 function)
{
	switch(function){
	case BLENDZERO: return GX_BL_ZERO;
	case BLENDONE: return GX_BL_ONE;
	case BLENDSRCCOLOR: return GX_BL_SRCCLR;
	case BLENDINVSRCCOLOR: return GX_BL_INVSRCCLR;
	case BLENDSRCALPHA: return GX_BL_SRCALPHA;
	case BLENDINVSRCALPHA: return GX_BL_INVSRCALPHA;
	case BLENDDESTALPHA: return GX_BL_DSTALPHA;
	case BLENDINVDESTALPHA: return GX_BL_INVDSTALPHA;
	case BLENDDESTCOLOR: return GX_BL_DSTCLR;
	case BLENDINVDESTCOLOR: return GX_BL_INVDSTCLR;
	case BLENDSRCALPHASAT: return GX_BL_SRCALPHA;
	default: return GX_BL_ONE;
	}
}

static uint8 nativeAlphaFunction(uint32 function)
{
	switch(function){
	case ALPHAGREATEREQUAL: return GX_GEQUAL;
	case ALPHALESS: return GX_LESS;
	default: return GX_ALWAYS;
	}
}

static uint8 nativeCullMode(uint32 mode)
{
	switch(mode){
		
	case CULLBACK: 
		return GX_CULL_FRONT;
	case CULLFRONT: 
		return GX_CULL_BACK;
	default: 
		return GX_CULL_NONE;
	}
}

void
setNativeBlendState(bool32 enable, uint32 source, uint32 destination)
{
	if(nativeState.blendEnable == enable &&
	   nativeState.sourceBlend == source &&
	   nativeState.destinationBlend == destination)
		return;
	nativeState.blendEnable = enable;
	nativeState.sourceBlend = source;
	nativeState.destinationBlend = destination;
	nativeState.dirty |= NATIVE_BLEND;
}

void
setNativeDepthState(bool32 test, bool32 write)
{
	if(nativeState.zTest == test && nativeState.zWrite == write)
		return;
	nativeState.zTest = test;
	nativeState.zWrite = write;
	nativeState.dirty |= NATIVE_DEPTH;
}

void
setNativeFogEnabled(bool32 enable)
{
	if(nativeState.fogEnable == enable)
		return;
	nativeState.fogEnable = enable;
	nativeState.dirty |= NATIVE_FOG;
}

void
setNativeFogColor(uint32 color)
{
	if(nativeState.fogColor == color)
		return;
	nativeState.fogColor = color;
	nativeState.dirty |= NATIVE_FOG;
}

void
setNativeFogRange(float32 start, float32 end, float32 nearPlane,
	float32 farPlane, bool32 perspective)
{
	if(nativeState.fogStart == start && nativeState.fogEnd == end &&
	   nativeState.nearPlane == nearPlane &&
	   nativeState.farPlane == farPlane &&
	   nativeState.perspective == perspective)
		return;
	nativeState.fogStart = start;
	nativeState.fogEnd = end;
	nativeState.nearPlane = nearPlane;
	nativeState.farPlane = farPlane;
	nativeState.perspective = perspective;
	nativeState.dirty |= NATIVE_FOG;
}

void
setNativeCullMode(uint32 mode)
{
	if(nativeState.cullMode == mode)
		return;
	nativeState.cullMode = mode;
	nativeState.dirty |= NATIVE_CULL;
}

void
setNativeAlphaTest(uint32 function, uint32 reference)
{
	if(nativeState.alphaTestFunction == function &&
	   nativeState.alphaTestReference == reference)
		return;
	nativeState.alphaTestFunction = function;
	nativeState.alphaTestReference = reference;
	nativeState.dirty |= NATIVE_ALPHA_TEST;
}

void
setNativeViewport(int32 x, int32 y, int32 width, int32 height)
{
	if(nativeState.viewportX == x && nativeState.viewportY == y &&
	   nativeState.viewportWidth == width &&
	   nativeState.viewportHeight == height)
		return;
	nativeState.viewportX = x;
	nativeState.viewportY = y;
	nativeState.viewportWidth = width;
	nativeState.viewportHeight = height;
	nativeState.dirty |= NATIVE_VIEWPORT;
}

void
invalidateNativeRenderState(void)
{
	nativeState.dirty = NATIVE_STATE_ALL;
}

void
prepareNativeDisplayCopy(void)
{
	// GX_CopyDisp() only applies its requested EFB Z clear when depth updates
	// are enabled.  The last pass is normally Im2D and leaves Z writes off, so
	// inheriting draw state here would preserve stale depth into the next frame.
	GX_SetColorUpdate(GX_TRUE);
	GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_ENABLE);
}

void
prepareNativeDraw(void)
{
	if(nativeState.dirty & NATIVE_VIEWPORT){
		GX_SetViewport(nativeState.viewportX, nativeState.viewportY,
		               nativeState.viewportWidth, nativeState.viewportHeight,
		               0.0f, 1.0f);
		GX_SetScissor(nativeState.viewportX, nativeState.viewportY,
		              nativeState.viewportWidth, nativeState.viewportHeight);
	}
	if(nativeState.dirty & NATIVE_DEPTH)
		GX_SetZMode(nativeState.zTest || nativeState.zWrite,
		            nativeState.zTest ? GX_LEQUAL : GX_ALWAYS,
		            nativeState.zWrite);
	if(nativeState.dirty & NATIVE_BLEND){
		GX_SetColorUpdate(GX_TRUE);
		GX_SetBlendMode(nativeState.blendEnable ? GX_BM_BLEND : GX_BM_NONE,
		                nativeBlendFunction(nativeState.sourceBlend),
		                nativeBlendFunction(nativeState.destinationBlend),
		                GX_LO_CLEAR);
	}
	if(nativeState.dirty & NATIVE_ALPHA_TEST){
		uint8 function = nativeAlphaFunction(nativeState.alphaTestFunction);
		GX_SetZCompLoc(function == GX_ALWAYS ? GX_ENABLE : GX_DISABLE);
		GX_SetAlphaCompare(function, nativeState.alphaTestReference,
		                   GX_AOP_AND, GX_ALWAYS, 0);
	}
	if(nativeState.dirty & NATIVE_CULL)
		GX_SetCullMode(nativeCullMode(nativeState.cullMode));
	if(nativeState.dirty & NATIVE_FOG){
		GXColor color = {
			(uint8)(nativeState.fogColor & 0xFF),
			(uint8)((nativeState.fogColor >> 8) & 0xFF),
			(uint8)((nativeState.fogColor >> 16) & 0xFF),
			(uint8)((nativeState.fogColor >> 24) & 0xFF)
		};
		uint8 mode = GX_FOG_NONE;
		if(nativeState.fogEnable)
			mode = nativeState.perspective ?
			       GX_FOG_PERSP_LIN : GX_FOG_ORTHO_LIN;
		GX_SetFog(mode, nativeState.fogStart, nativeState.fogEnd,
		          nativeState.nearPlane, nativeState.farPlane, color);
	}
	nativeState.dirty = 0;
}

}
}
