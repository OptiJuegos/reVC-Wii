#include <math.h>
#include <string.h>

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

uint32 renderedAtomicCount;
uint32 renderedGsAlphaPassCount;

static bool32
isOpaqueWhite(const RGBA &color)
{
	return color.red == 255 && color.green == 255 && color.blue == 255 &&
	       color.alpha == 255;
}

static uint8
colorToByte(float32 value)
{
	if(value <= 0.0f)
		return 0;
	if(value >= 1.0f)
		return 255;
	return (uint8)(value*255.0f + 0.5f);
}

static void
setupNativeVertexArrays(InstanceDataHeader *header)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_INDEX16);
	GX_SetVtxDesc(GX_VA_CLR0, GX_INDEX16);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetArray(GX_VA_POS, header->positions, sizeof(V3d));
	GX_SetArray(GX_VA_CLR0, header->colors, sizeof(RGBA));
	if(header->hasNormals){
		GX_SetVtxDesc(GX_VA_NRM, GX_INDEX16);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_F32, 0);
		GX_SetArray(GX_VA_NRM, header->normals, sizeof(V3d));
	}
	if(header->hasTexCoords){
		GX_SetVtxDesc(GX_VA_TEX0, GX_INDEX16);
		GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0,
		                 GX_TEX_ST, GX_F32, 0);
		GX_SetArray(GX_VA_TEX0, header->texCoords, sizeof(TexCoords));
	}
	if(header->vertexCacheDirty){
		GX_InvVtxCache();
		header->vertexCacheDirty = 0;
	}
}

void
initNativeTevState(void)
{
	// These are backend-wide invariants. No GX path in librw enables indirect
	// TEV, Z-texturing or a non-default swap table, so programming them once
	// avoids repeating the same BP writes for every mesh.
	GX_SetNumIndStages(0);
	GX_SetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
	GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN,
	                       GX_CH_BLUE, GX_CH_ALPHA);

	// The native atomic/immediate pipelines currently use at most four TEV
	// stages. Keep all of them direct with the default swap mapping up front.
	for(uint8 stage = GX_TEVSTAGE0; stage <= GX_TEVSTAGE3; stage++){
		GX_SetTevDirect(stage);
		GX_SetTevSwapMode(stage, GX_TEV_SWAP0, GX_TEV_SWAP0);
	}
}

static void
setupTevCopyRaster(uint8 stage, uint8 channel)
{
	GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, channel);
	GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
	GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
	GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

static void
setupTevRasterTextureModulate(uint8 stage)
{
	// TEX * RASC in one stage. This replaces the old stage0 RASC copy followed
	// by a texture stage in the dominant unlit/prelit world path.
	GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC,
	                 GX_CC_RASC, GX_CC_ZERO);
	GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA,
	                 GX_CA_RASA, GX_CA_ZERO);
	GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

static void
setupTevPrevTextureModulate(uint8 stage)
{
	GX_SetTevOrder(stage, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
	GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_TEXC,
	                 GX_CC_CPREV, GX_CC_ZERO);
	GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_TEXA,
	                 GX_CA_APREV, GX_CA_ZERO);
	GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

static void
setupTevMaterialModulate(uint8 stage, const RGBA &materialColor,
	bool32 hasPrevious)
{
	GXColor konst = {
		materialColor.red, materialColor.green,
		materialColor.blue, materialColor.alpha
	};
	GX_SetTevKColor(GX_KCOLOR0, konst);
	GX_SetTevKColorSel(stage, GX_TEV_KCSEL_K0);
	GX_SetTevKAlphaSel(stage, GX_TEV_KASEL_K0_A);
	GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE,
	               hasPrevious ? GX_COLORNULL : GX_COLOR0A0);
	GX_SetTevColorIn(stage, GX_CC_ZERO,
	                 hasPrevious ? GX_CC_CPREV : GX_CC_RASC,
	                 GX_CC_KONST, GX_CC_ZERO);
	GX_SetTevAlphaIn(stage, GX_CA_ZERO,
	                 hasPrevious ? GX_CA_APREV : GX_CA_RASA,
	                 GX_CA_KONST, GX_CA_ZERO);
	GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

// Build the RenderWare colour equation in TEV stages:
//   clamp(prelight + ambient + diffuse) * material * texture
// Alpha deliberately does not receive lighting; it is prelight alpha multiplied
// by material/texture alpha, just like the programmable librw backends.
static void
setupNativeTevPipeline(const RGBA &materialColor, GxRaster *nativeRaster,
	bool32 addLighting)
{
	bool32 textured = nativeRaster != nil && nativeRaster->nativeReady;
	bool32 materialModulate = !isOpaqueWhite(materialColor);
	uint8 stage = GX_TEVSTAGE0;

	if(textured){
		GX_SetNumTexGens(1);
		GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4,
		                  GX_TG_TEX0, GX_IDENTITY);
		GX_LoadTexObj(&nativeRaster->nativeTexture, GX_TEXMAP0);
	}else
		GX_SetNumTexGens(0);

	if(!addLighting){
		// Prelight is already clamped vertex data. Therefore, for the unlit
		// path, texture*RASC is algebraically identical to copying RASC into
		// CPREV first and multiplying it on the following stage.
		if(textured){
			setupTevRasterTextureModulate(stage++);
			if(materialModulate)
				setupTevMaterialModulate(stage++, materialColor, 1);
		}else if(materialModulate)
			setupTevMaterialModulate(stage++, materialColor, 0);
		else
			setupTevCopyRaster(stage++, GX_COLOR0A0);

		GX_SetNumTevStages(stage);
		return;
	}

	// Lit geometry must preserve the PS2/RW clamp point exactly:
	// clamp(prelight + lighting) happens before material/texture modulation.
	// Since a TEV stage can select only one raster colour channel at a time,
	// keep the prelight copy and lighting add as two stages here.
	setupTevCopyRaster(stage++, GX_COLOR0A0);
	GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_DISABLE, GX_COLOR1A1);
	GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_RASC,
	                 GX_CC_ONE, GX_CC_CPREV);
	GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO,
	                 GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO,
	                 GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	stage++;

	if(textured)
		setupTevPrevTextureModulate(stage++);
	if(materialModulate)
		setupTevMaterialModulate(stage++, materialColor, 1);

	GX_SetNumTevStages(stage);
}

// Immediate mode is unlit unless its caller explicitly configures a lit path.
void
setupNativeTev(const RGBA &materialColor, GxRaster *nativeRaster)
{
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
	               GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	setupNativeTevPipeline(materialColor, nativeRaster, 0);
}

static void
getAtomicLights(Atomic *atomic, uint32 geometryFlags, WorldLights *lightData,
	Light **directionals, Light **locals)
{
	memset(lightData, 0, sizeof(*lightData));
	lightData->directionals = directionals;
	lightData->locals = locals;
	if((geometryFlags & Geometry::LIGHT) == 0 || engine->currentWorld == nil)
		return;

	// enumerateLights treats these fields as capacities on input.
	lightData->numDirectionals = 8;
	lightData->numLocals = 8;
	((World*)engine->currentWorld)->enumerateLights(atomic, lightData);
}

static uint8
loadDirectionalLights(const WorldLights *lightData,
	const SurfaceProperties &surfaceProps, bool32 hasNormals)
{
	if(!hasNormals || surfaceProps.diffuse <= 0.0f)
		return GX_LIGHTNULL;

	static const uint8 lightIds[8] = {
		GX_LIGHT0, GX_LIGHT1, GX_LIGHT2, GX_LIGHT3,
		GX_LIGHT4, GX_LIGHT5, GX_LIGHT6, GX_LIGHT7
	};
	uint8 lightMask = GX_LIGHTNULL;
	int32 count = lightData->numDirectionals;
	if(count > 8)
		count = 8;

	for(int32 i = 0; i < count; i++){
		Light *light = lightData->directionals[i];
		if(light == nil || light->getFrame() == nil)
			continue;

		V3d direction = light->getFrame()->getLTM()->at;
		V3d viewDirection;
		transformNativeWorldVectorToView(direction, &viewDirection);
		float32 len2 = viewDirection.x*viewDirection.x +
		               viewDirection.y*viewDirection.y +
		               viewDirection.z*viewDirection.z;
		if(len2 <= 1.0e-12f)
			continue;
		float32 invLen = 1.0f/sqrtf(len2);
		viewDirection.x *= invLen;
		viewDirection.y *= invLen;
		viewDirection.z *= invLen;

		GXLightObj lightObject;
		memset(&lightObject, 0, sizeof(lightObject));
		GX_InitLightAttn(&lightObject, 1.0f, 0.0f, 0.0f,
		                 1.0f, 0.0f, 0.0f);
		GXColor color = {
			colorToByte(light->color.red*surfaceProps.diffuse),
			colorToByte(light->color.green*surfaceProps.diffuse),
			colorToByte(light->color.blue*surfaceProps.diffuse),
			255
		};
		GX_InitLightColor(&lightObject, color);

		// GX diffuse lighting is local/positional.  A very remote point in the
		// opposite direction reproduces RenderWare's parallel directional light.
		const float32 directionalDistance = 10000000.0f;
		GX_InitLightPos(&lightObject,
		                -viewDirection.x*directionalDistance,
		                -viewDirection.y*directionalDistance,
		                -viewDirection.z*directionalDistance);
		GX_LoadLightObj(&lightObject, lightIds[i]);
		lightMask |= lightIds[i];
	}
	return lightMask;
}

static void
setupNativeLighting(const WorldLights *lightData,
	const SurfaceProperties &surfaceProps, bool32 hasNormals)
{
	// Channel 0 carries PRELIT unchanged.  Channel 1 is reserved for the
	// RenderWare ambient + diffuse contribution and is added by TEV.
	GX_SetNumChans(2);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
	               GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);

	GXColor white = { 255, 255, 255, 255 };
	GXColor ambient = {
		colorToByte(lightData->ambient.red*surfaceProps.ambient),
		colorToByte(lightData->ambient.green*surfaceProps.ambient),
		colorToByte(lightData->ambient.blue*surfaceProps.ambient),
		255
	};
	GX_SetChanMatColor(GX_COLOR1A1, white);
	GX_SetChanAmbColor(GX_COLOR1A1, ambient);

	uint8 lightMask = loadDirectionalLights(lightData, surfaceProps,
	                                        hasNormals);
	GX_SetChanCtrl(GX_COLOR1, GX_ENABLE, GX_SRC_REG, GX_SRC_REG,
	               lightMask,
	               lightMask ? GX_DF_CLAMP : GX_DF_NONE,
	               GX_AF_NONE);
	// Lighting only changes RGB in librw. Keep the second channel's alpha out
	// of the equation entirely; TEV preserves channel 0 alpha in the add stage.
	GX_SetChanCtrl(GX_ALPHA1, GX_DISABLE, GX_SRC_REG, GX_SRC_REG,
	               GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
}

static void
drawNativeMeshSimple(InstanceDataHeader *header, const InstanceData *mesh)
{
	prepareNativeDraw();
	if(mesh->displayList != nil && mesh->displayListSize != 0){
		GX_CallDispList(mesh->displayList, mesh->displayListSize);
		nativeDisplayListCallCount++;
		return;
	}

	// Allocation/recording failure is non-fatal: preserve the old path so a
	// bad or unusually large mesh can never make the whole model disappear.
	nativeDisplayListFallbackCount++;
	PrimitiveType primitive = header->triangleStrip ?
	                              PRIMTYPETRISTRIP : PRIMTYPETRILIST;
	drawNativeBatches(primitive, header->indices + mesh->indexOffset,
	                  mesh->numIndices, [header](uint32 index) {
		GX_Position1x16(index);
		if(header->hasNormals)
			GX_Normal1x16(index);
		GX_Color1x16(index);
		if(header->hasTexCoords)
			GX_TexCoord1x16(index);
	});
}

// Emulate the PS2 GS alpha-test FB_ONLY case. Pixels below the GS reference
// still blend into the frame buffer, but must not update Z.
static void
drawNativeMeshGsAlpha(InstanceDataHeader *header, const InstanceData *mesh,
	bool32 hasAlpha)
{
	if(!hasAlpha){
		drawNativeMeshSimple(header, mesh);
		return;
	}

	uint32 zTest = GetRenderState(ZTESTENABLE);
	uint32 zWrite = GetRenderState(ZWRITEENABLE);
	uint32 alphaFunction = GetRenderState(ALPHATESTFUNC);
	uint32 alphaReference = GetRenderState(ALPHATESTREF);
	if(zWrite){
		uint32 gsAlphaReference = GetRenderState(GSALPHATESTREF);

		setNativeAlphaTest(ALPHAGREATEREQUAL, gsAlphaReference);
		drawNativeMeshSimple(header, mesh);

		setNativeAlphaTest(ALPHALESS, gsAlphaReference);
		setNativeDepthState(zTest, 0);
		drawNativeMeshSimple(header, mesh);
		renderedGsAlphaPassCount++;

		setNativeDepthState(zTest, zWrite);
		setNativeAlphaTest(alphaFunction, alphaReference);
	}else{
		setNativeAlphaTest(ALPHAALWAYS, alphaReference);
		drawNativeMeshSimple(header, mesh);
		setNativeAlphaTest(alphaFunction, alphaReference);
	}
}

static void
drawNativeMesh(InstanceDataHeader *header, const InstanceData *mesh,
	bool32 hasAlpha)
{
	if(GetRenderState(GSALPHATEST))
		drawNativeMeshGsAlpha(header, mesh, hasAlpha);
	else
		drawNativeMeshSimple(header, mesh);
}

static void
renderAtomicNative(Atomic *atomic, InstanceDataHeader *header,
	uint32 geometryFlags)
{
	static const RGBA white = { 255, 255, 255, 255 };
	Light *directionals[8];
	Light *locals[8];
	WorldLights lightData;
	getAtomicLights(atomic, geometryFlags, &lightData, directionals, locals);
	bool32 lightingEnabled = (geometryFlags & Geometry::LIGHT) != 0;

	setupNativeVertexArrays(header);
	for(uint32 i = 0; i < header->numMeshes; i++){
		InstanceData *mesh = &header->meshes[i];
		if(mesh->numIndices == 0)
			continue;
		RGBA sourceMaterialColor = mesh->material->color;
		RGBA materialColor = geometryFlags & Geometry::MODULATE ?
		                         sourceMaterialColor : white;
		setTexture(header->hasTexCoords ? mesh->material->texture : nil);
		setVertexAlpha(mesh->vertexAlpha ||
		               sourceMaterialColor.alpha != 255);
		Raster *raster = getRasterStage();
		GxRaster *nativeRaster = raster ? GETGXRASTEREXT(raster) : nil;

		if(lightingEnabled)
			setupNativeLighting(&lightData, mesh->material->surfaceProps,
			                    header->hasNormals);
		else{
			GX_SetNumChans(1);
			GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX,
			               GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
		}
		setupNativeTevPipeline(materialColor, nativeRaster, lightingEnabled);

		bool32 hasAlpha = mesh->vertexAlpha ||
		                  sourceMaterialColor.alpha != 255 ||
		                  (nativeRaster && nativeRaster->hasAlpha);
		drawNativeMesh(header, mesh, hasAlpha);
	}
}

void
renderAtomic(Atomic *atomic, InstanceDataHeader *header)
{
	if(header->numVertices == 0 || header->numIndices == 0)
		return;
	renderedAtomicCount++;

	loadNativeWorldMatrix(atomic->getFrame()->getLTM());
	renderAtomicNative(atomic, header, atomic->geometry->flags);
}

}
}
