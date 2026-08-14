#include <assert.h>
#include <stddef.h>

#include <gccore.h>
#include <ogc/gx.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "rwgx.h"
#include "gxbatch.h"

namespace rw {
namespace gx {

uint32 nativeDisplayListMemory;
uint32 nativeDisplayListBuildCount;
uint32 nativeDisplayListCallCount;
uint32 nativeDisplayListFallbackCount;

static uintptr
align32(uintptr p)
{
	return (p + 31) & ~uintptr(31);
}

static uint32
align32Size(uint32 size)
{
	return (size + 31) & ~uint32(31);
}

static uint32
estimateDisplayListSize(const InstanceDataHeader *header,
	const InstanceData *mesh)
{
	// Each submitted vertex contributes one u16 index per enabled indexed
	// attribute. Add generous command/batch padding; if GX still reports an
	// overflow we retry with a doubled buffer below.
	uint32 attributes = 2 + (header->hasNormals ? 1 : 0) +
		(header->hasTexCoords ? 1 : 0); // POS + CLR + optional NRM/TEX
	uint64 bytes = (uint64)mesh->numIndices * attributes * sizeof(uint16);
	uint32 batches = mesh->numIndices/0xFF00 + 1;
	bytes += 256 + (uint64)batches*32;
	if(bytes < 512)
		bytes = 512;
	if(bytes > 0x7FFFFFFFu)
		return 0;
	return align32Size((uint32)bytes);
}

static bool32
recordDisplayList(InstanceDataHeader *header, InstanceData *mesh)
{
	if(mesh->numIndices == 0)
		return 1;

	uint32 capacity = estimateDisplayListSize(header, mesh);
	if(capacity == 0)
		return 0;
	PrimitiveType primitive = header->triangleStrip ?
		PRIMTYPETRISTRIP : PRIMTYPETRILIST;

	for(uint32 attempt = 0; attempt < 4; attempt++){
		uint8 *raw = rwNewT(uint8, capacity + 31,
			MEMDUR_EVENT | ID_GEOMETRY);
		if(raw == nil)
			return 0;
		uint8 *list = (uint8*)align32((uintptr)raw);

		// GX_BeginDispList redirects the CPU write-gather pipe straight into
		// this memory. Invalidate first so an old dirty CPU cache line can
		// never overwrite the freshly recorded command stream afterwards.
		DCInvalidateRange(list, capacity);
		GX_BeginDispList(list, capacity);
		drawNativeBatches(primitive,
			header->indices + mesh->indexOffset, mesh->numIndices,
			[header](uint32 index) {
				GX_Position1x16(index);
				if(header->hasNormals)
					GX_Normal1x16(index);
				GX_Color1x16(index);
				if(header->hasTexCoords)
					GX_TexCoord1x16(index);
			});
		uint32 size = GX_EndDispList();
		if(size != 0 && size <= capacity){
			DCFlushRange(list, align32Size(size));
			mesh->displayListData = raw;
			mesh->displayList = list;
			mesh->displayListSize = size;
			mesh->displayListCapacity = capacity + 31;
			nativeDisplayListMemory += mesh->displayListCapacity;
			nativeDisplayListBuildCount++;
			return 1;
		}

		rwFree(raw);
		if(capacity > 0x3FFFFFFFu)
			break;
		capacity *= 2;
	}
	return 0;
}

void
freeNativeDisplayLists(InstanceDataHeader *header)
{
	if(header == nil || header->meshes == nil)
		return;
	for(uint32 i = 0; i < header->numMeshes; i++){
		InstanceData *mesh = &header->meshes[i];
		if(mesh->displayListData){
			if(mesh->displayListCapacity <= nativeDisplayListMemory)
				nativeDisplayListMemory -= mesh->displayListCapacity;
			else
				nativeDisplayListMemory = 0;
			rwFree(mesh->displayListData);
		}
		mesh->displayListData = nil;
		mesh->displayList = nil;
		mesh->displayListSize = 0;
		mesh->displayListCapacity = 0;
	}
}

void
prepareNativeGeometryForRender(InstanceDataHeader *header)
{
	if(header == nil)
		return;
	// GX reads indexed arrays through DMA. Make all offline/runtime-instanced
	// streams coherent once; skinned positions/normals are flushed separately
	// by gxskin.cpp every frame after CPU skinning.
	if(header->numVertices){
		DCFlushRange(header->positions,
		             header->numVertices*sizeof(V3d));
		if(header->hasNormals)
			DCFlushRange(header->normals,
			             header->numVertices*sizeof(V3d));
		DCFlushRange(header->colors,
		             header->numVertices*sizeof(RGBA));
		if(header->hasTexCoords)
			DCFlushRange(header->texCoords,
			             header->numVertices*sizeof(TexCoords));
	}
	if(header->numIndices)
		DCFlushRange(header->indices,
		             header->numIndices*sizeof(uint16));
	header->vertexCacheDirty = 1;

	for(uint32 i = 0; i < header->numMeshes; i++){
		InstanceData *mesh = &header->meshes[i];
		if(mesh->displayList == nil)
			recordDisplayList(header, mesh);
	}
}

}
}
