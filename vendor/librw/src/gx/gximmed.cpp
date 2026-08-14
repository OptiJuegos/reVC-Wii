#include <ogc/gx.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "../rwrender.h"

#include "rwgx.h"
#include "gxbatch.h"

namespace rw {
namespace gx {

uint32 im2DPrimitiveCount;
uint32 im3DPrimitiveCount;

static Im2DVertex primitiveBuffer[3];
static bool32 fogWasEnabled;

static Im3DVertex *im3DVertices;
static int32 im3DNumVertices;
static bool32 im3DTextured;

// Im2D vertices are already in screen space and hold the device Z in their
// position, so the pipeline only has to scale pixels to the viewport and let Z
// through untouched. Im2DVertex::w is not part of the device-space position.
static void
beginIm2DState(void)
{
	Camera *camera = engine->currentCamera;
	loadNativeIm2DMatrices(camera->frameBuffer->width,
	                       camera->frameBuffer->height,
	                       engine->device.zNear, engine->device.zFar);

	// RenderWare fogs Im2D by the camera space Z held in Im2DVertex::w, which
	// cannot reach a fixed function pipeline that fogs by eye space Z.  Screen
	// space geometry fogged by its device Z would be worse than no fog at all.
	fogWasEnabled = GetRenderState(FOGENABLE);
	if(fogWasEnabled)
		SetRenderState(FOGENABLE, 0);
}

static void
endIm2DState(void)
{
	if(fogWasEnabled)
		SetRenderState(FOGENABLE, fogWasEnabled);
}

static GxRaster*
resolveNativeImmediate(bool32 useRaster)
{
	if(!useRaster)
		return nil;
	Raster *raster = getRasterStage();
	if(raster == nil)
		return nil;
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	return nativeRaster->nativeReady ? nativeRaster : nil;
}

static void
setupNativeImmediate(GxRaster *nativeRaster)
{
	bool32 textured = nativeRaster != nil;
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	if(textured){
		GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0,
		                 GX_TEX_ST, GX_F32, 0);
	}

	RGBA white = { 255, 255, 255, 255 };
	setupNativeTev(white, nativeRaster);
}

static void
submitNativeIm2DVertex(const Im2DVertex *vertex, bool32 textured)
{
	GX_Position3f32(vertex->x, vertex->y, vertex->z);
	GX_Color4u8(vertex->r, vertex->g, vertex->b, vertex->a);
	if(textured)
		GX_TexCoord2f32(vertex->u, vertex->v);
}

static void
renderNativeIm2D(PrimitiveType primitiveType, Im2DVertex *vertices,
	const uint16 *indices, int32 count)
{
	GxRaster *nativeRaster = resolveNativeImmediate(1);
	bool32 textured = nativeRaster != nil;

	beginIm2DState();
	prepareNativeDraw();
	setupNativeImmediate(nativeRaster);
	drawNativeBatches(primitiveType, indices, count,
	                  [vertices, textured](uint32 index) {
		submitNativeIm2DVertex(&vertices[index], textured);
	});
	endIm2DState();
}

void
im2DRenderLine(void *vertices, int32, int32 vertex1, int32 vertex2)
{
	Im2DVertex *vertexArray = (Im2DVertex*)vertices;
	primitiveBuffer[0] = vertexArray[vertex1];
	primitiveBuffer[1] = vertexArray[vertex2];
	im2DRenderPrimitive(PRIMTYPELINELIST, primitiveBuffer, 2);
}

void
im2DRenderTriangle(void *vertices, int32, int32 vertex1, int32 vertex2,
	int32 vertex3)
{
	Im2DVertex *vertexArray = (Im2DVertex*)vertices;
	primitiveBuffer[0] = vertexArray[vertex1];
	primitiveBuffer[1] = vertexArray[vertex2];
	primitiveBuffer[2] = vertexArray[vertex3];
	im2DRenderPrimitive(PRIMTYPETRILIST, primitiveBuffer, 3);
}

void
im2DRenderPrimitive(PrimitiveType primitiveType, void *vertices,
	int32 numVertices)
{
	if(numVertices <= 0 || primitiveType <= PRIMTYPENONE ||
	   (uint32)primitiveType > PRIMTYPEPOINTLIST)
		return;

	im2DPrimitiveCount++;
	renderNativeIm2D(primitiveType, (Im2DVertex*)vertices, nil,
	                 numVertices);
}

void
im2DRenderIndexedPrimitive(PrimitiveType primitiveType, void *vertices,
	int32 numVertices, void *indices, int32 numIndices)
{
	if(numVertices <= 0 || numIndices <= 0 ||
	   primitiveType <= PRIMTYPENONE ||
	   (uint32)primitiveType > PRIMTYPEPOINTLIST)
		return;

	im2DPrimitiveCount++;
	renderNativeIm2D(primitiveType, (Im2DVertex*)vertices,
	                 (const uint16*)indices, numIndices);
}

static void
submitNativeIm3DVertex(const Im3DVertex *vertex, bool32 textured)
{
	GX_Position3f32(vertex->position.x, vertex->position.y,
	                 vertex->position.z);
	GX_Color4u8(vertex->r, vertex->g, vertex->b, vertex->a);
	if(textured)
		GX_TexCoord2f32(vertex->u, vertex->v);
}

static void
renderNativeIm3D(PrimitiveType primitiveType, const uint16 *indices,
	int32 count)
{
	GxRaster *nativeRaster = resolveNativeImmediate(im3DTextured);
	bool32 textured = nativeRaster != nil;

	prepareNativeDraw();
	setupNativeImmediate(nativeRaster);
	drawNativeBatches(primitiveType, indices, count,
	                  [textured](uint32 index) {
		submitNativeIm3DVertex(&im3DVertices[index], textured);
	});
}

// Im3D vertices go through the camera matrices like any other geometry. The
// native path writes each draw into the FIFO because callers may reuse their
// buffer as soon as im3DEnd() returns. im3d::LIGHTING remains unsupported, so
// vertex colours supply the diffuse result.
void
im3DTransform(void *vertices, int32 numVertices, Matrix *world, uint32 flags)
{
	loadNativeWorldMatrix(world);
	im3DVertices = (Im3DVertex*)vertices;
	im3DNumVertices = numVertices;
	im3DTextured = (flags & im3d::VERTEXUV) != 0;
	if(!im3DTextured)
		SetRenderStatePtr(TEXTURERASTER, nil);
}

void
im3DRenderPrimitive(PrimitiveType primitiveType)
{
	if(im3DNumVertices <= 0 || primitiveType <= PRIMTYPENONE ||
	   (uint32)primitiveType > PRIMTYPEPOINTLIST)
		return;
	im3DPrimitiveCount++;
	renderNativeIm3D(primitiveType, nil, im3DNumVertices);
}

void
im3DRenderIndexedPrimitive(PrimitiveType primitiveType, void *indices,
	int32 numIndices)
{
	if(im3DNumVertices <= 0 || numIndices <= 0 ||
	   primitiveType <= PRIMTYPENONE ||
	   (uint32)primitiveType > PRIMTYPEPOINTLIST)
		return;
	im3DPrimitiveCount++;
	renderNativeIm3D(primitiveType, (const uint16*)indices, numIndices);
}

void
im3DEnd(void)
{
	im3DVertices = nil;
	im3DNumVertices = 0;
}

}
}
