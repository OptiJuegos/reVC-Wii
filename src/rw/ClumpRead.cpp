#include "common.h"

#ifdef NINTENDO_WII
#include "wii-port/WiiLog.h"
#endif

struct rpGeometryList
{
	RpGeometry **geometries;
	int32 numGeoms;
};

struct rpAtomicBinary
{
	RwInt32 frameIndex;
	RwInt32 geomIndex;
	RwInt32 flags;
	RwInt32 unused;
};

static int32 numberGeometrys;
static int32 streamPosition;
static rpGeometryList gGeomList;
static rwFrameList gFrameList;
static RpClumpChunkInfo gClumpInfo;

rpGeometryList*
GeometryListStreamRead1(RwStream *stream, rpGeometryList *geomlist)
{
	int i;
	RwUInt32 size, version;
	RwInt32 numGeoms;

	numberGeometrys = 0;
	if(!RwStreamFindChunk(stream, rwID_STRUCT, &size, &version))
		return nil;
	assert(size == 4);
	if(RwStreamRead(stream, &numGeoms, 4) != 4)
		return nil;
	numGeoms = (RwInt32)BSWAP_I32(numGeoms);
#ifdef NINTENDO_WII
	wiiLog("WII clump: geometry list count=%d\n", numGeoms);
	if(numGeoms < 0 || numGeoms > 256){
		wiiLog("WII clump: invalid geometry count=%d\n", numGeoms);
		return nil;
	}
#endif

	numberGeometrys = numGeoms/2;
	geomlist->numGeoms = numGeoms;
	if(geomlist->numGeoms > 0){
		geomlist->geometries = (RpGeometry**)RwMalloc(geomlist->numGeoms * sizeof(RpGeometry*));
		if(geomlist->geometries == nil)
			return nil;
		memset(geomlist->geometries, 0, geomlist->numGeoms * sizeof(RpGeometry*));
	}else
		geomlist->geometries = nil;

	for(i = 0; i < numberGeometrys; i++){
		if(!RwStreamFindChunk(stream, rwID_GEOMETRY, nil, &version))
			return nil;
		geomlist->geometries[i] = RpGeometryStreamRead(stream);
		if(geomlist->geometries[i] == nil)
			return nil;
	}

	return geomlist;
}

rpGeometryList*
GeometryListStreamRead2(RwStream *stream, rpGeometryList *geomlist)
{
	int i;
	RwUInt32 version;

	for(i = numberGeometrys; i < geomlist->numGeoms; i++){
		if(!RwStreamFindChunk(stream, rwID_GEOMETRY, nil, &version))
			return nil;
		geomlist->geometries[i] = RpGeometryStreamRead(stream);
		if(geomlist->geometries[i] == nil)
			return nil;
	}

	return geomlist;
}

void
GeometryListDeinitialize(rpGeometryList *geomlist)
{
	int i;

	for(i = 0; i < geomlist->numGeoms; i++)
		if(geomlist->geometries[i])
			RpGeometryDestroy(geomlist->geometries[i]);

	if(geomlist->numGeoms){
		RwFree(geomlist->geometries);
		geomlist->numGeoms = 0;
	}
}

RpAtomic*
ClumpAtomicStreamRead(RwStream *stream, rwFrameList *frmList, rpGeometryList *geomList)
{
	RwUInt32 size, version;
	rpAtomicBinary a;
	RpAtomic *atomic;

	numberGeometrys = 0;
	if(!RwStreamFindChunk(stream, rwID_STRUCT, &size, &version))
		return nil;
	assert(size <= sizeof(rpAtomicBinary));
	if(RwStreamRead(stream, &a, size) != size)
		return nil;
	a.frameIndex = (RwInt32)BSWAP_I32(a.frameIndex);
	a.geomIndex = (RwInt32)BSWAP_I32(a.geomIndex);
	a.flags = (RwInt32)BSWAP_I32(a.flags);
	a.unused = (RwInt32)BSWAP_I32(a.unused);

	atomic = RpAtomicCreate();
	if(atomic == nil)
		return nil;

	RpAtomicSetFlags(atomic, a.flags);

	if(frmList->numFrames){
#ifdef NINTENDO_WII
		if(a.frameIndex < 0 || a.frameIndex >= frmList->numFrames){
			wiiLog("WII clump: invalid atomic frame index=%d frames=%d\n",
			       a.frameIndex, frmList->numFrames);
			RpAtomicDestroy(atomic);
			return nil;
		}
#endif
		assert(a.frameIndex < frmList->numFrames);
		RpAtomicSetFrame(atomic, frmList->frames[a.frameIndex]);
	}

	if(geomList->numGeoms){
#ifdef NINTENDO_WII
		if(a.geomIndex < 0 || a.geomIndex >= geomList->numGeoms){
			wiiLog("WII clump: invalid atomic geometry index=%d geometries=%d\n",
			       a.geomIndex, geomList->numGeoms);
			RpAtomicDestroy(atomic);
			return nil;
		}
#endif
		assert(a.geomIndex < geomList->numGeoms);
		RpAtomicSetGeometry(atomic, geomList->geometries[a.geomIndex], 0);
	}else{
		RpGeometry *geom;
		if(!RwStreamFindChunk(stream, rwID_GEOMETRY, nil, &version)){
			RpAtomicDestroy(atomic);
			return nil;
		}
		geom = RpGeometryStreamRead(stream);
		if(geom == nil){
			RpAtomicDestroy(atomic);
			return nil;
		}
		RpAtomicSetGeometry(atomic, geom, 0);
		RpGeometryDestroy(geom);
	}

	return atomic;
}

bool
RpClumpGtaStreamRead1(RwStream *stream)
{
	RwUInt32 size, version;

	if(!RwStreamFindChunk(stream, rwID_STRUCT, &size, &version))
		return false;
	if(version >= 0x33000){
		assert(size == 12);
		if(RwStreamRead(stream, &gClumpInfo, 12) != 12)
			return false;
	}else{
		assert(size == 4);
		if(RwStreamRead(stream, &gClumpInfo, 4) != 4)
			return false;
		gClumpInfo.numLights = 0;
		gClumpInfo.numCameras = 0;
	}
	gClumpInfo.numAtomics = (RwInt32)BSWAP_I32(gClumpInfo.numAtomics);
	gClumpInfo.numLights = (RwInt32)BSWAP_I32(gClumpInfo.numLights);
	gClumpInfo.numCameras = (RwInt32)BSWAP_I32(gClumpInfo.numCameras);
#ifdef NINTENDO_WII
	wiiLog("WII clump: split header atomics=%d lights=%d cameras=%d\n",
	       gClumpInfo.numAtomics, gClumpInfo.numLights, gClumpInfo.numCameras);
#endif

	if(!RwStreamFindChunk(stream, rwID_FRAMELIST, nil, &version))
		return false;
#ifdef NINTENDO_WII
	wiiLog("WII clump: frame list begin\n");
#endif
	if(rwFrameListStreamRead(stream, &gFrameList) == nil)
		return false;
#ifdef NINTENDO_WII
	wiiLog("WII clump: frame list complete frames=%d\n", gFrameList.numFrames);
#endif

	if(!RwStreamFindChunk(stream, rwID_GEOMETRYLIST, nil, &version)){
		rwFrameListDeinitialize(&gFrameList);
		return false;
	}
#ifdef NINTENDO_WII
	wiiLog("WII clump: geometry list begin\n");
#endif
	if(GeometryListStreamRead1(stream, &gGeomList) == nil){
		rwFrameListDeinitialize(&gFrameList);
		return false;
	}
#ifdef NINTENDO_WII
	wiiLog("WII clump: split first phase complete geometries=%d partial=%d\n",
	       gGeomList.numGeoms, numberGeometrys);
#endif
	streamPosition = STREAMPOS(stream);
	return true;
}

RpClump*
RpClumpGtaStreamRead2(RwStream *stream)
{
	int i;
	RwUInt32 version;
	RpAtomic *atomic;
	RpClump *clump;

	clump = RpClumpCreate();
	if(clump == nil)
		return nil;

	RwStreamSkip(stream, streamPosition - STREAMPOS(stream));

	if(GeometryListStreamRead2(stream, &gGeomList) == nil){
		GeometryListDeinitialize(&gGeomList);
		rwFrameListDeinitialize(&gFrameList);
		RpClumpDestroy(clump);
		return nil;
	}
#ifdef NINTENDO_WII
	wiiLog("WII clump: second geometry phase complete geometries=%d\n", gGeomList.numGeoms);
#endif

	RpClumpSetFrame(clump, gFrameList.frames[0]);

	for(i = 0; i < gClumpInfo.numAtomics; i++){
		if(!RwStreamFindChunk(stream, rwID_ATOMIC, nil, &version)){
			GeometryListDeinitialize(&gGeomList);
			rwFrameListDeinitialize(&gFrameList);
			RpClumpDestroy(clump);
			return nil;
		}

		atomic = ClumpAtomicStreamRead(stream, &gFrameList, &gGeomList);
		if(atomic == nil){
			GeometryListDeinitialize(&gGeomList);
			rwFrameListDeinitialize(&gFrameList);
			RpClumpDestroy(clump);
			return nil;
		}

		RpClumpAddAtomic(clump, atomic);
	}

	GeometryListDeinitialize(&gGeomList);
	rwFrameListDeinitialize(&gFrameList);
#ifdef NINTENDO_WII
	wiiLog("WII clump: split clump complete atomics=%d\n", gClumpInfo.numAtomics);
#endif
	return clump;
}

void
RpClumpGtaCancelStream(void)
{
	GeometryListDeinitialize(&gGeomList);
	rwFrameListDeinitialize(&gFrameList);
	gFrameList.numFrames = 0;
}
