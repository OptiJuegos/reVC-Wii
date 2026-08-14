#include <assert.h>
#include <string.h>

#include <gccore.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "../rwrender.h"

#include "rwgx.h"

namespace rw {
namespace gx {

struct RenderStateCache
{
	bool32 vertexAlpha;
	bool32 textureAlpha;
	uint32 sourceBlend;
	uint32 destinationBlend;
	bool32 zTest;
	bool32 zWrite;
	bool32 fogEnable;
	uint32 fogColor;
	uint32 cullMode;
	uint32 alphaTestFunction;
	uint32 alphaTestReference;
	bool32 gsAlphaTest;
	uint32 gsAlphaTestReference;
};

static RenderStateCache renderState;

struct PresentationState
{
	void *frameBuffers[2];
	int32 width;
	int32 height;
	int32 activeFrameBuffer;
	bool32 copyDepthAvailable;
};

static PresentationState presentation;
static uint32 reusedCopyDepthClearCount;

struct TextureStageState
{
	Raster *raster;
	int32 filterMode;
	int32 addressU;
	int32 addressV;
};

static TextureStageState textureStage;

static void
updateNativeAlphaState(void)
{
	bool32 enable = renderState.vertexAlpha || renderState.textureAlpha;
	setNativeBlendState(enable, renderState.sourceBlend,
	                    renderState.destinationBlend);
	setNativeAlphaTest(enable ? renderState.alphaTestFunction :
	                   (uint32)ALPHAALWAYS,
	                   renderState.alphaTestReference);
}

// What the last beginUpdate() handed to GX, kept so showRaster() can report the
// geometry the frame was actually drawn with rather than what it was asked for.
struct FrameDiagnostics
{
	int32 viewportX;
	int32 viewportY;
	int32 viewportWidth;
	int32 viewportHeight;
	int32 rasterWidth;
	int32 rasterHeight;
	int32 rasterOffsetX;
	int32 rasterOffsetY;
	float32 nearPlane;
	float32 farPlane;
};

static FrameDiagnostics diagnostics;

static void
getCameraViewport(Camera *camera, int32 *x, int32 *y, int32 *width,
	int32 *height)
{
	Raster *frameBuffer = camera->frameBuffer;
	*x = frameBuffer->offsetX;
	*y = frameBuffer->offsetY;
	*width = frameBuffer->width;
	*height = frameBuffer->height;
	if(*x + *width > presentation.width)
		*width = presentation.width - *x;
	if(*y + *height > presentation.height)
		*height = presentation.height - *y;
	if(*width < 0)
		*width = 0;
	if(*height < 0)
		*height = 0;
}

static void
applyFilter(GxRaster *nativeRaster, int32 filter)
{
	if(nativeRaster->filterMode == filter)
		return;
	nativeRaster->filterMode = filter;
	updateNativeRasterSampler(nativeRaster);
}

static void
applyAddressU(GxRaster *nativeRaster, int32 addressing)
{
	if(nativeRaster->addressU == addressing)
		return;
	nativeRaster->addressU = addressing;
	updateNativeRasterSampler(nativeRaster);
}

static void
applyAddressV(GxRaster *nativeRaster, int32 addressing)
{
	if(nativeRaster->addressV == addressing)
		return;
	nativeRaster->addressV = addressing;
	updateNativeRasterSampler(nativeRaster);
}

static void
setFilterMode(int32 filter)
{
	if(textureStage.filterMode == filter)
		return;
	textureStage.filterMode = filter;
	if(textureStage.raster)
		applyFilter(GETGXRASTEREXT(textureStage.raster), filter);
}

static void
setAddressU(int32 addressing)
{
	if(textureStage.addressU == addressing)
		return;
	textureStage.addressU = addressing;
	if(textureStage.raster)
		applyAddressU(GETGXRASTEREXT(textureStage.raster), addressing);
}

static void
setAddressV(int32 addressing)
{
	if(textureStage.addressV == addressing)
		return;
	textureStage.addressV = addressing;
	if(textureStage.raster)
		applyAddressV(GETGXRASTEREXT(textureStage.raster), addressing);
}

static void
bindRaster(Raster *raster)
{
	textureStage.raster = raster;
	if(raster == nil){
		renderState.textureAlpha = 0;
		updateNativeAlphaState();
		return;
	}
	assert(raster->platform == PLATFORM_GX);
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	renderState.textureAlpha = nativeRaster->hasAlpha;
	updateNativeAlphaState();
	if(nativeRaster->nativeReady)
		nativeRaster->nativeUsed = 1;
}

void
setRasterStage(Raster *raster)
{
	if(raster == textureStage.raster)
		return;
	bindRaster(raster);
	if(raster == nil)
		return;
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	applyFilter(nativeRaster, textureStage.filterMode);
	applyAddressU(nativeRaster, textureStage.addressU);
	applyAddressV(nativeRaster, textureStage.addressV);
}

void
setTexture(Texture *texture)
{
	if(texture == nil || texture->raster == nil){
		setRasterStage(nil);
		return;
	}

	if(texture->raster != textureStage.raster){
		bindRaster(texture->raster);
		// A raster keeps whatever parameters it was last bound with, so adopt
		// them here and let the calls below program only what this texture
		// actually changes.
		GxRaster *nativeRaster = GETGXRASTEREXT(texture->raster);
		textureStage.filterMode = nativeRaster->filterMode;
		textureStage.addressU = nativeRaster->addressU;
		textureStage.addressV = nativeRaster->addressV;
	}
	setFilterMode(texture->getFilter());
	setAddressU(texture->getAddressU());
	setAddressV(texture->getAddressV());
}

Raster*
getRasterStage(void)
{
	return textureStage.raster;
}

void
evictRaster(Raster *raster)
{
	if(textureStage.raster == raster)
		setRasterStage(nil);
}

void
setVertexAlpha(bool32 enable)
{
	renderState.vertexAlpha = enable;
	updateNativeAlphaState();
}

static void
resetRenderState()
{
	renderState.sourceBlend = BLENDSRCALPHA;
	renderState.destinationBlend = BLENDINVSRCALPHA;
	renderState.zTest = 1;
	renderState.zWrite = 1;
	renderState.fogEnable = 0;
	renderState.fogColor = 0;
	renderState.cullMode = CULLNONE;
	renderState.alphaTestFunction = ALPHAGREATEREQUAL;
	renderState.alphaTestReference = 10;
	renderState.gsAlphaTest = 0;
	renderState.gsAlphaTestReference = 128;
	renderState.textureAlpha = 0;
	textureStage.raster = nil;
	// Rasters start out with no mode programmed, so these are what the first
	// texture bound gets if the game binds a raster without asking for a
	// filter or an addressing mode first.
	textureStage.filterMode = Texture::LINEAR;
	textureStage.addressU = Texture::WRAP;
	textureStage.addressV = Texture::WRAP;
	setVertexAlpha(0);
	setNativeDepthState(renderState.zTest, renderState.zWrite);
	setNativeFogEnabled(renderState.fogEnable);
	setNativeFogColor(renderState.fogColor);
	setNativeCullMode(renderState.cullMode);
	invalidateNativeRenderState();
}

static void
beginUpdate(Camera *camera)
{
	// Only the clear issued before a camera update may reuse the Z clear left
	// by the preceding display copy.
	presentation.copyDepthAvailable = 0;
	float32 view[16];
	float32 projection[16];
	Matrix inverse;
	Raster *frameBuffer = camera->frameBuffer;
	int32 viewportX, viewportY, viewportWidth, viewportHeight;
	getCameraViewport(camera, &viewportX, &viewportY,
	                  &viewportWidth, &viewportHeight);
	setNativeViewport(viewportX, viewportY,
	                  viewportWidth, viewportHeight);

	diagnostics.viewportX = viewportX;
	diagnostics.viewportY = viewportY;
	diagnostics.viewportWidth = viewportWidth;
	diagnostics.viewportHeight = viewportHeight;
	diagnostics.rasterWidth = frameBuffer->width;
	diagnostics.rasterHeight = frameBuffer->height;
	diagnostics.rasterOffsetX = frameBuffer->offsetX;
	diagnostics.rasterOffsetY = frameBuffer->offsetY;
	diagnostics.nearPlane = camera->nearPlane;
	diagnostics.farPlane = camera->farPlane;

	Matrix::invert(&inverse, camera->getFrame()->getLTM());

	// RenderWare cameras look down positive Z with a right handed frame, so
	// right x up = at, and the at x up that points at the viewer's right is
	// -right.  librw's GL backends mirror X to put it back on screen right and
	// project with w = +z.
	//
	// GX needs that same X mirror plus one on Z: GX_LoadProjectionMtx() reads
	// six scalars out of the matrix and wires the last row to (0, 0, -1, 0), so
	// w is always -z and a positive Z view space puts the whole scene behind
	// the eye, where the clipper drops it.  The two mirrors are on different
	// axes and neither substitutes for the other; together they reach the same
	// NDC the GL backends produce.
	view[0] = -inverse.right.x;
	view[1] = inverse.right.y;
	view[2] = -inverse.right.z;
	view[3] = 0.0f;
	view[4] = -inverse.up.x;
	view[5] = inverse.up.y;
	view[6] = -inverse.up.z;
	view[7] = 0.0f;
	view[8] = -inverse.at.x;
	view[9] = inverse.at.y;
	view[10] = -inverse.at.z;
	view[11] = 0.0f;
	view[12] = -inverse.pos.x;
	view[13] = inverse.pos.y;
	view[14] = -inverse.pos.z;
	view[15] = 1.0f;

	float32 inverseWidth = 1.0f/camera->viewWindow.x;
	float32 inverseHeight = 1.0f/camera->viewWindow.y;
	float32 inverseDepth = 1.0f/(camera->farPlane-camera->nearPlane);
	projection[0] = inverseWidth;
	projection[1] = 0.0f;
	projection[2] = 0.0f;
	projection[3] = 0.0f;
	projection[4] = 0.0f;
	projection[5] = inverseHeight;
	projection[6] = 0.0f;
	projection[7] = 0.0f;
	// The native matrix loader transposes this GL-shaped matrix and replaces
	// its Z terms with the GX clip-space equivalents. GX_LoadProjectionMtx()
	// ignores projection[12] and projection[13] for perspective cameras.
	projection[8] = -camera->viewOffset.x*inverseWidth;
	projection[9] = -camera->viewOffset.y*inverseHeight;
	projection[12] = projection[8];
	projection[13] = projection[9];
	if(camera->projection == Camera::PERSPECTIVE){
		projection[10] = -(camera->farPlane+camera->nearPlane)*inverseDepth;
		projection[11] = -1.0f;
		projection[14] = -2.0f*camera->nearPlane*camera->farPlane*inverseDepth;
		projection[15] = 0.0f;
	}else{
		projection[10] = -2.0f*inverseDepth;
		projection[11] = 0.0f;
		projection[14] = -(camera->farPlane+camera->nearPlane)*inverseDepth;
		projection[15] = 1.0f;
	}

	memcpy(&camera->devView, view, sizeof(RawMatrix));
	memcpy(&camera->devProj, projection, sizeof(RawMatrix));
	setNativeCameraMatrices(view, projection,
	                        camera->projection == Camera::PERSPECTIVE,
	                        camera->nearPlane, camera->farPlane);
	setNativeFogRange(camera->fogPlane, camera->farPlane,
	                  camera->nearPlane, camera->farPlane,
	                  camera->projection == Camera::PERSPECTIVE);
}

static void
endUpdate(Camera*)
{
}

static void
clearCamera(Camera *camera, RGBA *color, uint32 mode)
{
	bool32 clearColor = (mode & Camera::CLEARIMAGE) != 0;
	bool32 clearDepth = (mode & Camera::CLEARZ) != 0;
	if(clearDepth && presentation.copyDepthAvailable){
		reusedCopyDepthClearCount++;
		clearDepth = 0;
	}
	int32 x, y, width, height;
	getCameraViewport(camera, &x, &y, &width, &height);
	clearNativeCamera(x, y, width, height, *color,
	                  clearColor, clearDepth,
	                  engine->device.zNear, engine->device.zFar);
}

static bool32 frameTrace;

void
setFrameTrace(bool32 enabled)
{
	frameTrace = enabled;
}

static void
showRaster(Raster *raster, uint32 flags)
{
	static uint32 presentedFrames;
	if(raster == nil || raster->type != Raster::CAMERA)
		return;
	if(frameTrace && (presentedFrames < 8 || (presentedFrames + 1) % 300 == 0))
		SYS_Report("WII GX frame: present=%u atomics=%u im2d=%u im3d=%u "
		           "dlmem=%uKB dlbuild=%u dlcall=%u dlfallback=%u "
		           "texmem=%uKB texup=%u texwait=%u zreuse=%u "
		           "viewport=%d,%d %dx%d raster=%dx%d off=%d,%d clip=%.2f/%.1f "
		           "alpha=%u/%u gsalpha=%d/%u passes=%u cull=%u ztest=%d zwrite=%d "
		           "blend=%d texalpha=%d tex=%p\n",
		           presentedFrames + 1, renderedAtomicCount,
		           im2DPrimitiveCount, im3DPrimitiveCount,
		           nativeDisplayListMemory/1024, nativeDisplayListBuildCount,
		           nativeDisplayListCallCount, nativeDisplayListFallbackCount,
		           nativeTextureMemory/1024, nativeTextureUploadCount,
		           nativeTextureWaitCount, reusedCopyDepthClearCount,
		           diagnostics.viewportX, diagnostics.viewportY,
		           diagnostics.viewportWidth, diagnostics.viewportHeight,
		           diagnostics.rasterWidth, diagnostics.rasterHeight,
		           diagnostics.rasterOffsetX, diagnostics.rasterOffsetY,
		           (double)diagnostics.nearPlane,
		           (double)diagnostics.farPlane,
		           renderState.alphaTestFunction, renderState.alphaTestReference,
		           renderState.gsAlphaTest, renderState.gsAlphaTestReference,
		           renderedGsAlphaPassCount,
		           renderState.cullMode, renderState.zTest, renderState.zWrite,
		           renderState.vertexAlpha, renderState.textureAlpha,
		           (void*)textureStage.raster);
	presentedFrames++;
	renderedAtomicCount = 0;
	renderedGsAlphaPassCount = 0;
	im2DPrimitiveCount = 0;
	im3DPrimitiveCount = 0;
	nativeDisplayListBuildCount = 0;
	nativeDisplayListCallCount = 0;
	nativeDisplayListFallbackCount = 0;
	nativeTextureUploadCount = 0;
	nativeTextureWaitCount = 0;
	reusedCopyDepthClearCount = 0;
	presentation.activeFrameBuffer ^= 1;
	void *frameBuffer = presentation.frameBuffers[presentation.activeFrameBuffer];
	prepareNativeDisplayCopy();
	GX_CopyDisp(frameBuffer, GX_TRUE);
	GX_DrawDone();
	// The copy uses a deliberately forced Z state.  Keep the cached RenderWare
	// state authoritative when the next camera or draw begins.
	invalidateNativeRenderState();
	presentation.copyDepthAvailable = 1;
	VIDEO_SetNextFramebuffer(frameBuffer);
	VIDEO_Flush();
	if(flags & Raster::FLIPWAITVSYNCH)
		VIDEO_WaitVSync();
}

static bool32
rasterRenderFast(Raster*, int32, int32)
{
	return 0;
}

static void
setRenderState(int32 state, void *pointer)
{
	uint32 value = (uint32)(uintptr)pointer;
	switch(state){
	case TEXTURERASTER:
		setRasterStage((Raster*)pointer);
		break;
	case TEXTUREADDRESS:
		setAddressU(value);
		setAddressV(value);
		break;
	case TEXTUREADDRESSU:
		setAddressU(value);
		break;
	case TEXTUREADDRESSV:
		setAddressV(value);
		break;
	case TEXTUREFILTER:
		setFilterMode(value);
		break;
	case VERTEXALPHA:
		setVertexAlpha(value);
		break;
	case SRCBLEND:
		renderState.sourceBlend = value;
		updateNativeAlphaState();
		break;
	case DESTBLEND:
		renderState.destinationBlend = value;
		updateNativeAlphaState();
		break;
	case ZTESTENABLE:
		renderState.zTest = value;
		setNativeDepthState(renderState.zTest, renderState.zWrite);
		break;
	case ZWRITEENABLE:
		renderState.zWrite = value;
		setNativeDepthState(renderState.zTest, renderState.zWrite);
		break;
	case FOGENABLE:
		renderState.fogEnable = value;
		setNativeFogEnabled(renderState.fogEnable);
		break;
	case FOGCOLOR:
		{
			renderState.fogColor = value;
			setNativeFogColor(renderState.fogColor);
		}
		break;
	case CULLMODE:
		// Mirroring both X and Z in beginUpdate() leaves screen space winding
		// where the GL backends put it, so RenderWare's cull modes map straight
		// across against the default counter-clockwise front face.
		if(renderState.cullMode != value){
			renderState.cullMode = value;
			setNativeCullMode(renderState.cullMode);
		}
		break;
	case ALPHATESTFUNC:
		if(renderState.alphaTestFunction != value){
			renderState.alphaTestFunction = value;
			updateNativeAlphaState();
		}
		break;
	case ALPHATESTREF:
		if(renderState.alphaTestReference != value){
			renderState.alphaTestReference = value;
			updateNativeAlphaState();
		}
		break;
	case GSALPHATEST:
		renderState.gsAlphaTest = value;
		break;
	case GSALPHATESTREF:
		renderState.gsAlphaTestReference = value;
		break;
	default:
		break;
	}
}

static void*
getRenderState(int32 state)
{
	uint32 value = 0;
	switch(state){
	case TEXTURERASTER: return textureStage.raster;
	case TEXTUREADDRESS:
		value = textureStage.addressU == textureStage.addressV ?
			textureStage.addressU : 0;
		break;
	case TEXTUREADDRESSU: value = textureStage.addressU; break;
	case TEXTUREADDRESSV: value = textureStage.addressV; break;
	case TEXTUREFILTER: value = textureStage.filterMode; break;
	case VERTEXALPHA: value = renderState.vertexAlpha; break;
	case SRCBLEND: value = renderState.sourceBlend; break;
	case DESTBLEND: value = renderState.destinationBlend; break;
	case ZTESTENABLE: value = renderState.zTest; break;
	case ZWRITEENABLE: value = renderState.zWrite; break;
	case FOGENABLE: value = renderState.fogEnable; break;
	case FOGCOLOR: value = renderState.fogColor; break;
	case CULLMODE: value = renderState.cullMode; break;
	case ALPHATESTFUNC: value = renderState.alphaTestFunction; break;
	case ALPHATESTREF: value = renderState.alphaTestReference; break;
	case GSALPHATEST: value = renderState.gsAlphaTest; break;
	case GSALPHATESTREF: value = renderState.gsAlphaTestReference; break;
	default: break;
	}
	return (void*)(uintptr)value;
}

static int
deviceSystem(DeviceReq req, void *argument, int32)
{
	switch(req){
	case DEVICEGETNUMSUBSYSTEMS:
	case DEVICEGETNUMVIDEOMODES:
		return 1;
	case DEVICEGETCURRENTSUBSYSTEM:
	case DEVICEGETCURRENTVIDEOMODE:
		return 0;
	case DEVICEGETVIDEOMODEINFO:
		{
			// Callers keep this on the stack and read it whether or not the
			// query succeeded, so leaving it untouched hands them garbage.
			// CameraSize() does exactly that, and a garbage mode there resizes
			// the camera raster to nonsense.
			VideoMode *mode = (VideoMode*)argument;
			if(mode == nil)
				return 0;
			mode->width = presentation.width;
			mode->height = presentation.height;
			mode->depth = 32;
			// The console owns the screen outright, and saying so is what
			// keeps CameraSize() sizing the camera from this mode.
			mode->flags = VIDEOMODEEXCLUSIVE;
			return 1;
		}
	case DEVICEOPEN:
		{
			EngineOpenParams *parameters = (EngineOpenParams*)argument;
			if(parameters == nil || parameters->frameBuffers[0] == nil ||
			   parameters->frameBuffers[1] == nil)
				return 0;
			presentation.frameBuffers[0] = parameters->frameBuffers[0];
			presentation.frameBuffers[1] = parameters->frameBuffers[1];
			presentation.width = parameters->width;
			presentation.height = parameters->height;
			presentation.activeFrameBuffer = 0;
			presentation.copyDepthAvailable = 0;
			return 1;
		}
	case DEVICECLOSE:
		presentation.frameBuffers[0] = nil;
		presentation.frameBuffers[1] = nil;
		presentation.copyDepthAvailable = 0;
		return 1;
	case DEVICEINIT:
		resetRenderState();
		initNativeTevState();
		return 1;
	case DEVICEFINALIZE:
	case DEVICETERM:
		return 1;
	default:
		return 0;
	}
}

Device renderdevice = {
	// GX, like D3D, exposes the post-projection depth range as 0..1.
	// Advertising the OpenGL -1..1 range poisons RenderWare's zScale/zShift
	// calculations and all screen/immediate-mode depths.
	0.0f, 1.0f,
	gx::beginUpdate,
	gx::endUpdate,
	gx::clearCamera,
	gx::showRaster,
	gx::rasterRenderFast,
	gx::setRenderState,
	gx::getRenderState,
	gx::im2DRenderLine,
	gx::im2DRenderTriangle,
	gx::im2DRenderPrimitive,
	gx::im2DRenderIndexedPrimitive,
	gx::im3DTransform,
	gx::im3DRenderPrimitive,
	gx::im3DRenderIndexedPrimitive,
	gx::im3DEnd,
	gx::deviceSystem
};

}
}
