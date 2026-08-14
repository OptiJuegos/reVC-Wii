#include <assert.h>
#include <string.h>

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

// Material effects render as their base pass only.  Environment and bump maps
// want a second TEV stage and a generated texture coordinate that the fixed
// function GL layer does not expose, so vehicles come out with their diffuse
// texture and no reflection.  Without this pipeline registered they would keep
// the dummy one the skin plugin installs, which draws nothing at all.

ObjPipeline*
makeMatFXPipeline(void)
{
	ObjPipeline *pipeline = ObjPipeline::create();
	pipeline->pluginID = ID_MATFX;
	pipeline->pluginData = 0;
	return pipeline;
}

static void*
matfxOpen(void *o, int32, int32)
{
	matFXGlobals.pipelines[PLATFORM_GX] = makeMatFXPipeline();
	return o;
}

static void*
matfxClose(void *o, int32, int32)
{
	matFXGlobals.pipelines[PLATFORM_GX]->destroy();
	matFXGlobals.pipelines[PLATFORM_GX] = nil;
	return o;
}

void
initMatFX(void)
{
	Driver::registerPlugin(PLATFORM_GX, 0, ID_MATFX, matfxOpen, matfxClose);
}

#else

void initMatFX(void) { }

#endif

}
}
