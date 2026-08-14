#pragma once

#include <ogc/gx.h>

namespace rw {
namespace gx {

static inline uint8
nativePrimitive(PrimitiveType primitiveType)
{
	switch(primitiveType){
	case PRIMTYPELINELIST: return GX_LINES;
	case PRIMTYPEPOLYLINE: return GX_LINESTRIP;
	case PRIMTYPETRILIST: return GX_TRIANGLES;
	case PRIMTYPETRISTRIP: return GX_TRIANGLESTRIP;
	case PRIMTYPETRIFAN: return GX_TRIANGLEFAN;
	default: return GX_POINTS;
	}
}

template<typename Submit>
static inline void
drawNativeRange(PrimitiveType primitiveType, const uint16 *indices,
	uint32 first, uint32 count, Submit submit)
{
	GX_Begin(nativePrimitive(primitiveType), GX_VTXFMT0, (uint16)count);
	for(uint32 i = 0; i < count; i++){
		uint32 source = first + i;
		submit(indices ? indices[source] : source);
	}
	GX_End();
}

template<typename Submit>
static inline void
drawNativeBatches(PrimitiveType primitiveType, const uint16 *indices,
	uint32 count, Submit submit)
{
	if(count == 0)
		return;

	if(primitiveType == PRIMTYPETRIFAN){
		uint32 firstCount = count < 0xFFFF ? count : 0xFFFF;
		drawNativeRange(primitiveType, indices, 0, firstCount, submit);
		for(uint32 next = firstCount; next < count;){
			uint32 newCount = count - next;
			if(newCount > 0xFFFD)
				newCount = 0xFFFD;
			GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0,
			         (uint16)(newCount + 2));
			submit(indices ? indices[0] : 0);
			submit(indices ? indices[next - 1] : next - 1);
			for(uint32 i = 0; i < newCount; i++){
				uint32 source = next + i;
				submit(indices ? indices[source] : source);
			}
			GX_End();
			next += newCount;
		}
		return;
	}

	uint32 batchLimit = 0xFFFF;
	uint32 overlap = 0;
	if(primitiveType == PRIMTYPELINELIST)
		batchLimit = 0xFFFE;
	else if(primitiveType == PRIMTYPEPOLYLINE)
		overlap = 1;
	else if(primitiveType == PRIMTYPETRISTRIP){
		batchLimit = 0xFFFE;
		overlap = 2;
	}

	for(uint32 first = 0; first < count;){
		uint32 batchCount = count - first;
		if(batchCount > batchLimit)
			batchCount = batchLimit;
		if(first + batchCount < count &&
		   primitiveType == PRIMTYPETRILIST)
			batchCount -= batchCount%3;
		drawNativeRange(primitiveType, indices, first, batchCount, submit);
		if(first + batchCount >= count)
			break;
		first += batchCount - overlap;
	}
}

}
}
