#include <ogc/gu.h>
#include <ogc/gx.h>

#include "../rwbase.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rwgx.h"

namespace rw {
namespace gx {

static Mtx cameraView;
static Mtx44 cameraProjection;
static uint8 cameraProjectionType;
static bool32 cameraProjectionActive;

static void
convertMatrix(Mtx destination, const float32 *source)
{
	for(int32 i = 0; i < 12; i++)
		destination[i%3][i/3] = source[(i/3)*4 + i%3];
}

static void
convertMatrix44(Mtx44 destination, const float32 *source)
{
	for(int32 i = 0; i < 16; i++)
		destination[i%4][i/4] = source[i];
}

static void
makeNativeMatrix(Mtx destination, const Matrix *source)
{
	destination[0][0] = source->right.x;
	destination[0][1] = source->up.x;
	destination[0][2] = source->at.x;
	destination[0][3] = source->pos.x;
	destination[1][0] = source->right.y;
	destination[1][1] = source->up.y;
	destination[1][2] = source->at.y;
	destination[1][3] = source->pos.y;
	destination[2][0] = source->right.z;
	destination[2][1] = source->up.z;
	destination[2][2] = source->at.z;
	destination[2][3] = source->pos.z;
}

static void
activateCameraProjection(void)
{
	if(cameraProjectionActive)
		return;
	GX_LoadProjectionMtx(cameraProjection, cameraProjectionType);
	cameraProjectionActive = 1;
}

void
setNativeCameraMatrices(const float32 *view, const float32 *projection,
	bool32 perspective, float32 nearPlane, float32 farPlane)
{
	convertMatrix(cameraView, view);
	convertMatrix44(cameraProjection, projection);
	float32 inverseDepth = 1.0f/(farPlane-nearPlane);
	if(perspective){
		cameraProjection[2][2] = -nearPlane*inverseDepth;
		cameraProjection[2][3] = -nearPlane*farPlane*inverseDepth;
	}else{
		cameraProjection[2][2] = -inverseDepth;
		cameraProjection[2][3] = -farPlane*inverseDepth;
	}
	cameraProjectionType = perspective ? GX_PERSPECTIVE : GX_ORTHOGRAPHIC;
	GX_LoadProjectionMtx(cameraProjection, cameraProjectionType);
	GX_LoadPosMtxImm(cameraView, GX_PNMTX0);
	Mtx normalMatrix;
	if(guMtxInvXpose(cameraView, normalMatrix))
		GX_LoadNrmMtxImm(normalMatrix, GX_PNMTX0);
	else
		GX_LoadNrmMtxImm(cameraView, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	cameraProjectionActive = 1;
}

void
loadNativeWorldMatrix(const Matrix *world)
{
	activateCameraProjection();
	Mtx modelView;
	if(world == nil)
		guMtxCopy(cameraView, modelView);
	else{
		Mtx worldMatrix;
		makeNativeMatrix(worldMatrix, world);
		guMtxConcat(cameraView, worldMatrix, modelView);
	}

	GX_LoadPosMtxImm(modelView, GX_PNMTX0);
	// GX lighting consumes normals after the normal matrix.  RenderWare can
	// legally use scaled object matrices, so use inverse-transpose rather than
	// assuming the model-view 3x3 is orthonormal.
	Mtx normalMatrix;
	if(guMtxInvXpose(modelView, normalMatrix))
		GX_LoadNrmMtxImm(normalMatrix, GX_PNMTX0);
	else
		GX_LoadNrmMtxImm(modelView, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
}

void
transformNativeWorldPointToView(const V3d &source, V3d *destination)
{
	destination->x = cameraView[0][0]*source.x + cameraView[0][1]*source.y +
	                 cameraView[0][2]*source.z + cameraView[0][3];
	destination->y = cameraView[1][0]*source.x + cameraView[1][1]*source.y +
	                 cameraView[1][2]*source.z + cameraView[1][3];
	destination->z = cameraView[2][0]*source.x + cameraView[2][1]*source.y +
	                 cameraView[2][2]*source.z + cameraView[2][3];
}

void
transformNativeWorldVectorToView(const V3d &source, V3d *destination)
{
	destination->x = cameraView[0][0]*source.x + cameraView[0][1]*source.y +
	                 cameraView[0][2]*source.z;
	destination->y = cameraView[1][0]*source.x + cameraView[1][1]*source.y +
	                 cameraView[1][2]*source.z;
	destination->z = cameraView[2][0]*source.x + cameraView[2][1]*source.y +
	                 cameraView[2][2]*source.z;
}

void
loadNativeIm2DMatrices(int32 width, int32 height, float32 nearPlane,
	float32 farPlane)
{
	Mtx identity;
	Mtx44 projection;
	guMtxIdentity(identity);
	// Integer Im2D coordinates address pixel centres, matching the half-pixel
	// correction the former compatibility layer applied for GX.
	//
	// Im2DVertex::z is already in RenderWare *device* depth coordinates.
	// RenderWare's GX device range is 0..1 (same convention as D3D), while a
	// normal GX orthographic projection represents near..far as -1..0 before
	// the viewport transform.  Build the X/Y portion with guOrtho, then replace
	// the Z row so deviceNear maps to -1 and deviceFar maps to 0.
	guOrtho(projection, -0.5f, height - 0.5f,
	        -0.5f, width - 0.5f, 1.0f, 0.0f);
	if(farPlane != nearPlane){
		float32 invDepth = 1.0f/(farPlane - nearPlane);
		projection[2][2] = invDepth;
		projection[2][3] = -1.0f - nearPlane*invDepth;
	}else{
		// Degenerate ranges should never happen, but keep the matrix finite.
		projection[2][2] = 0.0f;
		projection[2][3] = -1.0f;
	}
	GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
	GX_LoadPosMtxImm(identity, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	cameraProjectionActive = 0;
}

}
}
