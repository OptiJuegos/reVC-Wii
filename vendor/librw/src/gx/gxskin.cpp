#include <assert.h>
#include <string.h>
#include <math.h>

#include <gccore.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwrender.h"
#include "../rwengine.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwanim.h"
#include "../rwplugins.h"

#include "rwgx.h"

namespace rw {
namespace gx {

#ifdef RW_GX

// GX skins with an indexed matrix palette the fixed function GL layer cannot
// reach, so the blend runs on the CPU straight into the instanced vertices.
// The bind pose stays in the geometry's morph target, which is what every
// frame reads from, so this costs no extra memory.

#define MAX_SKIN_BONES 64

static Matrix skinMatrices[MAX_SKIN_BONES];

static uint32
alignArraySize(uint32 size)
{
	return (size + 31) & ~uint32(31);
}

// Returns how many matrices were actually written.  A hierarchy with fewer
// nodes than the skin has bones would otherwise leave the tail holding another
// atomic's matrices, and a vertex weighted to one of those tears across the
// screen.
static int32
buildSkinMatrices(Atomic *atomic, Skin *skin)
{
	HAnimHierarchy *hierarchy = Skin::getHierarchy(atomic);
	int32 numBones = skin->numBones;
	int32 i;

	if(numBones > MAX_SKIN_BONES)
		numBones = MAX_SKIN_BONES;
	if(hierarchy == nil){
		for(i = 0; i < numBones; i++)
			skinMatrices[i].setIdentity();
		return numBones;
	}

	if(numBones > hierarchy->numNodes)
		numBones = hierarchy->numNodes;

	Matrix *inverseMatrices = (Matrix*)skin->inverseMatrices;
	if(hierarchy->flags & HAnimHierarchy::LOCALSPACEMATRICES){
		for(i = 0; i < numBones; i++){
			inverseMatrices[i].flags = 0;
			Matrix::mult(&skinMatrices[i], &inverseMatrices[i],
			             &hierarchy->matrices[i]);
		}
	}else{
		Matrix inverseAtomic;
		Matrix relative;
		Matrix::invert(&inverseAtomic, atomic->getFrame()->getLTM());
		for(i = 0; i < numBones; i++){
			inverseMatrices[i].flags = 0;
			Matrix::mult(&relative, &hierarchy->matrices[i], &inverseAtomic);
			Matrix::mult(&skinMatrices[i], &inverseMatrices[i], &relative);
		}
	}
	return numBones;
}

static void
blendVertices(InstanceDataHeader *header, Geometry *geometry, Skin *skin,
	int32 numBones)
{
	const V3d *bindPose = geometry->morphTargets[0].vertices;
	const V3d *bindNormals = geometry->morphTargets[0].normals;
	const float32 *weights = skin->weights;
	const uint8 *indices = skin->indices;

	for(uint32 i = 0; i < header->numVertices; i++){
		const V3d &source = bindPose[i];
		V3d blended = { 0.0f, 0.0f, 0.0f };
		V3d blendedNormal = { 0.0f, 0.0f, 0.0f };
		float32 blendedWeight = 0.0f;
		for(int32 j = 0; j < 4; j++){
			float32 weight = weights[i*4 + j];
			if(weight == 0.0f)
				continue;
			int32 bone = indices[i*4 + j];
			if(bone >= numBones)
				continue;
			const Matrix &matrix = skinMatrices[bone];
			blended.x += weight*(matrix.right.x*source.x +
			                     matrix.up.x*source.y +
			                     matrix.at.x*source.z + matrix.pos.x);
			blended.y += weight*(matrix.right.y*source.x +
			                     matrix.up.y*source.y +
			                     matrix.at.y*source.z + matrix.pos.y);
			blended.z += weight*(matrix.right.z*source.x +
			                     matrix.up.z*source.y +
			                     matrix.at.z*source.z + matrix.pos.z);

			if(header->hasNormals && bindNormals){
				const V3d &normal = bindNormals[i];
				blendedNormal.x += weight*(matrix.right.x*normal.x +
				                         matrix.up.x*normal.y +
				                         matrix.at.x*normal.z);
				blendedNormal.y += weight*(matrix.right.y*normal.x +
				                         matrix.up.y*normal.y +
				                         matrix.at.y*normal.z);
				blendedNormal.z += weight*(matrix.right.z*normal.x +
				                         matrix.up.z*normal.y +
				                         matrix.at.z*normal.z);
			}
			blendedWeight += weight;
		}
		// An unweighted vertex keeps its bind pose rather than collapsing onto
		// the atomic's origin.
		header->positions[i] = blendedWeight > 0.0f ? blended : source;

		if(header->hasNormals && bindNormals){
			V3d normal = blendedWeight > 0.0f ? blendedNormal : bindNormals[i];
			float32 lengthSquared = normal.x*normal.x + normal.y*normal.y +
			                        normal.z*normal.z;
			if(lengthSquared > 1.0e-12f){
				float32 inverseLength = 1.0f/sqrtf(lengthSquared);
				normal.x *= inverseLength;
				normal.y *= inverseLength;
				normal.z *= inverseLength;
			}else
				normal = bindNormals[i];
			header->normals[i] = normal;
		}
	}
	DCStoreRange(header->positions,
	             alignArraySize(header->numVertices*sizeof(V3d)));
	if(header->hasNormals)
		DCStoreRange(header->normals,
		             alignArraySize(header->numVertices*sizeof(V3d)));
	header->vertexCacheDirty = 1;
}

void
skinAtomic(Atomic *atomic, InstanceDataHeader *header)
{
	Geometry *geometry = atomic->geometry;
	Skin *skin = Skin::get(geometry);
	if(skin == nil || skin->numBones <= 0)
		return;
	if(geometry->morphTargets[0].vertices == nil ||
	   skin->weights == nil || skin->indices == nil)
		return;

	blendVertices(header, geometry, skin, buildSkinMatrices(atomic, skin));
}

static void
render(rw::ObjPipeline *pipeline, Atomic *atomic)
{
	pipeline->instance(atomic);
	Geometry *geometry = atomic->geometry;
	assert(geometry->instData != nil);
	assert(geometry->instData->platform == PLATFORM_GX);
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	skinAtomic(atomic, header);
	renderAtomic(atomic, header);
}

ObjPipeline*
makeSkinPipeline(void)
{
	// Instancing is the default one: skinning overwrites the positions it
	// produced, every frame, from the bind pose.
	ObjPipeline *pipeline = ObjPipeline::create();
	pipeline->impl.render = gx::render;
	pipeline->pluginID = ID_SKIN;
	pipeline->pluginData = 1;
	return pipeline;
}

static void*
skinOpen(void *o, int32, int32)
{
	skinGlobals.pipelines[PLATFORM_GX] = makeSkinPipeline();
	return o;
}

static void*
skinClose(void *o, int32, int32)
{
	((ObjPipeline*)skinGlobals.pipelines[PLATFORM_GX])->destroy();
	skinGlobals.pipelines[PLATFORM_GX] = nil;
	return o;
}

void
initSkin(void)
{
	Driver::registerPlugin(PLATFORM_GX, 0, ID_SKIN, skinOpen, skinClose);
}

#else

void initSkin(void) { }

#endif

}
}
