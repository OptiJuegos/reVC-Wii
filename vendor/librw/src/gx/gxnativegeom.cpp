#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#define PLUGIN_ID ID_NATIVEDATA

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rwgx.h"

namespace rw {
namespace gx {

// Stream format is intentionally simple and endian-neutral through Stream's
// read16/read32 helpers. Display lists are NOT serialized: they are tiny,
// libogc/GX-version dependent command buffers and are rebuilt once at load.
static const uint32 GX_NATIVE_MAGIC = 0x314E5847; // "GXN1"
static const uint32 GX_NATIVE_VERSION = 1;

enum NativeFlags {
	NATIVE_TRISTRIP   = 1 << 0,
	NATIVE_VERTEXALPHA= 1 << 1,
	NATIVE_PRELIT     = 1 << 2,
	NATIVE_NORMALS    = 1 << 3,
	NATIVE_TEXCOORDS  = 1 << 4
};

static uintptr
alignArray(uintptr address)
{
	return (address + 31) & ~uintptr(31);
}

static void
allocateVertexStreams(InstanceDataHeader *header)
{
	size_t positionSize = header->numVertices*sizeof(V3d);
	size_t normalSize = header->hasNormals ?
		header->numVertices*sizeof(V3d) : 0;
	size_t colorSize = header->numVertices*sizeof(RGBA);
	size_t texCoordSize = header->hasTexCoords ?
		header->numVertices*sizeof(TexCoords) : 0;
	size_t numStreams = 2 + (header->hasNormals ? 1 : 0) +
		(header->hasTexCoords ? 1 : 0);
	size_t allocationSize = positionSize + normalSize + colorSize +
		texCoordSize + (numStreams + 1)*31;

	header->vertexData = allocationSize ?
		rwNewT(uint8, allocationSize, MEMDUR_EVENT | ID_GEOMETRY) : nil;
	if(allocationSize && header->vertexData == nil)
		return;

	uintptr cursor = alignArray((uintptr)header->vertexData);
	header->positions = header->numVertices ? (V3d*)cursor : nil;
	cursor = alignArray(cursor + positionSize);
	header->normals = header->hasNormals && header->numVertices ? (V3d*)cursor : nil;
	if(header->hasNormals)
		cursor = alignArray(cursor + normalSize);
	header->colors = header->numVertices ? (RGBA*)cursor : nil;
	cursor = alignArray(cursor + colorSize);
	header->texCoords = header->hasTexCoords && header->numVertices ?
		(TexCoords*)cursor : nil;
}

static InstanceDataHeader*
allocateHeader(uint32 numMeshes, uint32 numVertices, uint32 numIndices,
	bool32 triangleStrip, bool32 vertexAlpha, bool32 hasPrelight,
	bool32 hasNormals, bool32 hasTexCoords)
{
	InstanceDataHeader *header = rwNewT(InstanceDataHeader, 1,
		MEMDUR_EVENT | ID_GEOMETRY);
	if(header == nil)
		return nil;
	memset(header, 0, sizeof(*header));
	header->platform = PLATFORM_GX;
	header->numMeshes = numMeshes;
	header->numVertices = numVertices;
	header->numIndices = numIndices;
	header->triangleStrip = triangleStrip;
	header->vertexAlpha = vertexAlpha;
	header->hasPrelight = hasPrelight;
	header->hasNormals = hasNormals;
	header->hasTexCoords = hasTexCoords;

	allocateVertexStreams(header);
	if(numVertices && header->vertexData == nil){
		rwFree(header);
		return nil;
	}

	if(numIndices){
		header->indices = rwNewT(uint16, numIndices, MEMDUR_EVENT | ID_GEOMETRY);
		if(header->indices == nil){
			rwFree(header->vertexData);
			rwFree(header);
			return nil;
		}
	}
	if(numMeshes){
		header->meshes = rwNewT(InstanceData, numMeshes,
			MEMDUR_EVENT | ID_GEOMETRY);
		if(header->meshes == nil){
			rwFree(header->indices);
			rwFree(header->vertexData);
			rwFree(header);
			return nil;
		}
		memset(header->meshes, 0, numMeshes*sizeof(InstanceData));
	}
	return header;
}

void
freeInstanceData(Geometry *geometry)
{
	if(geometry->instData == nil ||
	   geometry->instData->platform != PLATFORM_GX)
		return;

	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	geometry->instData = nil;
#ifdef RW_GX
	freeNativeDisplayLists(header);
#endif
	rwFree(header->meshes);
	rwFree(header->indices);
	rwFree(header->vertexData);
	rwFree(header);
}

void*
destroyNativeData(void *object, int32, int32)
{
	freeInstanceData((Geometry*)object);
	return object;
}

static bool32
meshHasVertexAlpha(const InstanceDataHeader *header,
	const InstanceData *instance)
{
	if(!header->hasPrelight || instance->numIndices == 0)
		return 0;
	const uint16 *indices = header->indices + instance->indexOffset;
	for(uint32 i = 0; i < instance->numIndices; i++){
		uint32 index = indices[i];
		if(index < header->numVertices && header->colors[index].alpha != 255)
			return 1;
	}
	return 0;
}

static bool32
setupMeshesFromGeometry(Geometry *geometry, InstanceDataHeader *header,
	bool32 copyIndices)
{
	MeshHeader *meshHeader = geometry->meshHeader;
	if(meshHeader == nil || meshHeader->numMeshes != header->numMeshes ||
	   meshHeader->totalIndices != header->numIndices)
		return 0;

	header->serialNumber = meshHeader->serialNum;
	Mesh *mesh = meshHeader->getMeshes();
	uint32 indexOffset = 0;
	for(uint32 i = 0; i < header->numMeshes; i++){
		InstanceData *instance = &header->meshes[i];
		instance->numIndices = mesh[i].numIndices;
		instance->indexOffset = indexOffset;
		instance->material = mesh[i].material;
		instance->displayListData = nil;
		instance->displayList = nil;
		instance->displayListSize = 0;
		instance->displayListCapacity = 0;
		if(copyIndices && mesh[i].numIndices){
			if(mesh[i].indices == nil)
				return 0;
			memcpy(header->indices + indexOffset, mesh[i].indices,
			       mesh[i].numIndices*sizeof(uint16));
		}
		instance->vertexAlpha = meshHasVertexAlpha(header, instance);
		indexOffset += mesh[i].numIndices;
	}
	return indexOffset == header->numIndices;
}

InstanceDataHeader*
instanceGeometry(Geometry *geometry)
{
	if(geometry == nil)
		return nil;
	if(geometry->meshHeader == nil)
		geometry->buildMeshes();
	MeshHeader *meshHeader = geometry->meshHeader;
	if(meshHeader == nil || geometry->numMorphTargets < 1 ||
	   geometry->morphTargets[0].vertices == nil)
		return nil;

	bool32 hasPrelight = (geometry->flags & Geometry::PRELIT) != 0 &&
		geometry->colors != nil;
	bool32 hasNormals = (geometry->flags & Geometry::NORMALS) != 0 &&
		geometry->morphTargets[0].normals != nil;
	bool32 hasTexCoords = geometry->numTexCoordSets > 0 &&
		geometry->texCoords[0] != nil;

	InstanceDataHeader *header = allocateHeader(meshHeader->numMeshes,
		geometry->numVertices, meshHeader->totalIndices,
		meshHeader->flags == MeshHeader::TRISTRIP, 0,
		hasPrelight, hasNormals, hasTexCoords);
	if(header == nil)
		return nil;

	V3d *positions = geometry->morphTargets[0].vertices;
	V3d *normals = geometry->morphTargets[0].normals;
	for(uint32 i = 0; i < header->numVertices; i++){
		header->positions[i] = positions[i];
		if(header->hasNormals)
			header->normals[i] = normals[i];
		if(header->hasTexCoords)
			header->texCoords[i] = geometry->texCoords[0][i];
		if(header->hasPrelight)
			header->colors[i] = geometry->colors[i];
		else{
			header->colors[i].red = 0;
			header->colors[i].green = 0;
			header->colors[i].blue = 0;
			header->colors[i].alpha = 255;
		}
		if(header->hasPrelight && header->colors[i].alpha != 255)
			header->vertexAlpha = 1;
	}

	if(!setupMeshesFromGeometry(geometry, header, 1)){
		Geometry temp = *geometry;
		temp.instData = header;
		freeInstanceData(&temp);
		return nil;
	}

	geometry->instData = header;
#ifdef RW_GX
	prepareNativeGeometryForRender(header);
#endif
	return header;
}

bool32
makeNativeGeometry(Geometry *geometry)
{
	if(geometry == nil)
		return 0;
	if(geometry->flags & Geometry::NATIVE)
		return geometry->instData != nil &&
		       geometry->instData->platform == PLATFORM_GX;
	// Native GX stores one static bind-pose stream. Skins and morphing are
	// intentionally filtered by the converter before reaching here; reject
	// multi-morph geometry as a second line of defence.
	if(geometry->numMorphTargets != 1)
		return 0;
	if(instanceGeometry(geometry) == nil)
		return 0;
	geometry->flags |= Geometry::NATIVE;
	geometry->lockedSinceInst = 0;
	return 1;
}

static uint32
nativeFlags(const InstanceDataHeader *header)
{
	uint32 flags = 0;
	if(header->triangleStrip) flags |= NATIVE_TRISTRIP;
	if(header->vertexAlpha) flags |= NATIVE_VERTEXALPHA;
	if(header->hasPrelight) flags |= NATIVE_PRELIT;
	if(header->hasNormals) flags |= NATIVE_NORMALS;
	if(header->hasTexCoords) flags |= NATIVE_TEXCOORDS;
	return flags;
}

Stream*
readNativeData(Stream *stream, int32, void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	if(!findChunk(stream, ID_STRUCT, nil, nil)){
		RWERROR((ERR_CHUNK, "STRUCT"));
		return nil;
	}

	uint32 platform = stream->readU32();
	uint32 magic = stream->readU32();
	uint32 formatVersion = stream->readU32();
	uint32 numVertices = stream->readU32();
	uint32 numIndices = stream->readU32();
	uint32 numMeshes = stream->readU32();
	uint32 flags = stream->readU32();
	if(platform != PLATFORM_GX){
		RWERROR((ERR_PLATFORM, platform));
		return nil;
	}
	if(magic != GX_NATIVE_MAGIC || formatVersion != GX_NATIVE_VERSION){
		RWERROR((ERR_PLATFORM, platform));
		return nil;
	}
	if(geometry->meshHeader == nil ||
	   geometry->meshHeader->numMeshes != numMeshes ||
	   geometry->meshHeader->totalIndices != numIndices ||
	   (uint32)geometry->numVertices != numVertices){
		RWERROR((ERR_PLATFORM, platform));
		return nil;
	}

	InstanceDataHeader *header = allocateHeader(numMeshes, numVertices,
		numIndices, (flags & NATIVE_TRISTRIP) != 0,
		(flags & NATIVE_VERTEXALPHA) != 0,
		(flags & NATIVE_PRELIT) != 0,
		(flags & NATIVE_NORMALS) != 0,
		(flags & NATIVE_TEXCOORDS) != 0);
	if(header == nil)
		return nil;

	if(numVertices){
		stream->read32(header->positions, numVertices*sizeof(V3d));
		if(header->hasNormals)
			stream->read32(header->normals, numVertices*sizeof(V3d));
		stream->read8(header->colors, numVertices*sizeof(RGBA));
		if(header->hasTexCoords)
			stream->read32(header->texCoords,
			               numVertices*sizeof(TexCoords));
	}
	if(numIndices)
		stream->read16(header->indices, numIndices*sizeof(uint16));
	if(!setupMeshesFromGeometry(geometry, header, 0)){
		Geometry temp = *geometry;
		temp.instData = header;
		freeInstanceData(&temp);
		return nil;
	}

	geometry->instData = header;
#ifdef RW_GX
	prepareNativeGeometryForRender(header);
#endif
	return stream;
}

Stream*
writeNativeData(Stream *stream, int32 len, void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	if(geometry->instData == nil ||
	   geometry->instData->platform != PLATFORM_GX)
		return stream;
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	writeChunkHeader(stream, ID_STRUCT, len-12);
	stream->writeU32(PLATFORM_GX);
	stream->writeU32(GX_NATIVE_MAGIC);
	stream->writeU32(GX_NATIVE_VERSION);
	stream->writeU32(header->numVertices);
	stream->writeU32(header->numIndices);
	stream->writeU32(header->numMeshes);
	stream->writeU32(nativeFlags(header));

	if(header->numVertices){
		stream->write32(header->positions,
		                header->numVertices*sizeof(V3d));
		if(header->hasNormals)
			stream->write32(header->normals,
			                header->numVertices*sizeof(V3d));
		stream->write8(header->colors,
		               header->numVertices*sizeof(RGBA));
		if(header->hasTexCoords)
			stream->write32(header->texCoords,
			                header->numVertices*sizeof(TexCoords));
	}
	if(header->numIndices)
		stream->write16(header->indices,
		                header->numIndices*sizeof(uint16));
	return stream;
}

int32
getSizeNativeData(void *object, int32, int32)
{
	Geometry *geometry = (Geometry*)object;
	if(geometry->instData == nil ||
	   geometry->instData->platform != PLATFORM_GX)
		return 0;
	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	// Native plugin data includes the nested 12-byte STRUCT chunk header.
	uint32 size = 12 + 7*4;
	size += header->numVertices*sizeof(V3d);
	if(header->hasNormals)
		size += header->numVertices*sizeof(V3d);
	size += header->numVertices*sizeof(RGBA);
	if(header->hasTexCoords)
		size += header->numVertices*sizeof(TexCoords);
	size += header->numIndices*sizeof(uint16);
	return (int32)size;
}

}
}
