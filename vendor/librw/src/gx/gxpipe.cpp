#include <assert.h>

#include <gccore.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rwgx.h"

namespace rw {
namespace gx {

static void
instance(rw::ObjPipeline*, Atomic *atomic)
{
	Geometry *geometry = atomic->geometry;
	// GX-native DFFs were already instanced by the native-data plugin while
	// streaming. Their display lists are prepared once when that plugin reads.
	if(geometry->flags & Geometry::NATIVE)
		return;
	if(geometry->meshHeader == nil)
		geometry->buildMeshes();

	InstanceDataHeader *header = (InstanceDataHeader*)geometry->instData;
	if(header){
		assert(header->platform == PLATFORM_GX);
		if(header->serialNumber != geometry->meshHeader->serialNum)
			freeInstanceData(geometry);
	}
	if(geometry->instData == nil || geometry->lockedSinceInst){
		freeInstanceData(geometry);
		instanceGeometry(geometry);
	}
	geometry->lockedSinceInst = 0;
}

static void
uninstance(rw::ObjPipeline*, Atomic *atomic)
{
	freeInstanceData(atomic->geometry);
}

static void
render(rw::ObjPipeline *pipeline, Atomic *atomic)
{
	pipeline->instance(atomic);
	Geometry *geometry = atomic->geometry;
	assert(geometry->instData != nil);
	assert(geometry->instData->platform == PLATFORM_GX);
	renderAtomic(atomic, (InstanceDataHeader*)geometry->instData);
}

void
ObjPipeline::init(void)
{
	this->rw::ObjPipeline::init(PLATFORM_GX);
	this->impl.instance = gx::instance;
	this->impl.uninstance = gx::uninstance;
	this->impl.render = gx::render;
}

ObjPipeline*
ObjPipeline::create(void)
{
	ObjPipeline *pipeline = rwNewT(ObjPipeline, 1, MEMDUR_GLOBAL);
	pipeline->init();
	return pipeline;
}

ObjPipeline*
makeDefaultPipeline(void)
{
	return ObjPipeline::create();
}

}
}
