#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rwgx.h"

namespace rw {
namespace gx {

#ifndef RW_GX
// registerSkinPlugin/registerMatFXPlugin enumerate every backend even in host
// tools. Keep harmless GX stubs available without pulling libogc headers into
// a desktop build.
void initSkin(void) { }
void initMatFX(void) { }
#endif

static void*
driverOpen(void *o, int32, int32)
{
#ifdef RW_GX
	engine->driver[PLATFORM_GX]->defaultPipeline = makeDefaultPipeline();
	engine->driver[PLATFORM_GX]->rasterCreate = rasterCreate;
	engine->driver[PLATFORM_GX]->rasterLock = rasterLock;
	engine->driver[PLATFORM_GX]->rasterUnlock = rasterUnlock;
	engine->driver[PLATFORM_GX]->rasterLockPalette = rasterLockPalette;
	engine->driver[PLATFORM_GX]->rasterUnlockPalette = rasterUnlockPalette;
	engine->driver[PLATFORM_GX]->rasterNumLevels = rasterNumLevels;
	engine->driver[PLATFORM_GX]->imageFindRasterFormat = imageFindRasterFormat;
	engine->driver[PLATFORM_GX]->rasterFromImage = rasterFromImage;
	engine->driver[PLATFORM_GX]->rasterToImage = rasterToImage;
#endif
	return o;
}

static void*
driverClose(void *o, int32, int32)
{
#ifdef RW_GX
	ObjPipeline *pipeline = (ObjPipeline*)engine->driver[PLATFORM_GX]->defaultPipeline;
	if(pipeline != engine->dummyDefaultPipeline)
		pipeline->destroy();
	engine->driver[PLATFORM_GX]->defaultPipeline = engine->dummyDefaultPipeline;
#endif
	return o;
}

void
registerPlatformPlugins(void)
{
	Driver::registerPlugin(PLATFORM_GX, 0, PLATFORM_GX,
	                       driverOpen, driverClose);
#ifdef RW_GX
	registerNativeRaster();
#endif
}

}
}
