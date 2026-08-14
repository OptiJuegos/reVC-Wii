// Wii librw GX backend smoke test.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#include <dirent.h>
#include <unistd.h>

#include <gccore.h>
#include <fat.h>
#include <ogc/usbstorage.h>

#if __has_include(<dvm.h>)
#include <dvm.h>
#define WII_BRINGUP_HAVE_DVM 1
#endif

#include <rw.h>

namespace
{

constexpr unsigned int kFifoSize = 256 * 1024;

struct AssetLocation
{
	const char *name;
	const char *txdPath;
	const char *dffPath;
};

constexpr AssetLocation kAssetLocations[] = {
	{ "working-directory", "test.txd", "test.dff" },
	{ "sd-app", "sd:/apps/reVC/test.txd", "sd:/apps/reVC/test.dff" },
	{ "sd-root", "sd:/reVC/test.txd", "sd:/reVC/test.dff" },
	{ "usb-app", "usb:/apps/reVC/test.txd", "usb:/apps/reVC/test.dff" },
	{ "usb-root", "usb:/reVC/test.txd", "usb:/reVC/test.dff" },
	{ "usb2-app", "usb2:/apps/reVC/test.txd", "usb2:/apps/reVC/test.dff" },
	{ "usb3-app", "usb3:/apps/reVC/test.txd", "usb3:/apps/reVC/test.dff" },
	{ "usb4-app", "usb4:/apps/reVC/test.txd", "usb4:/apps/reVC/test.dff" },
	{ "carda-app", "carda:/apps/reVC/test.txd", "carda:/apps/reVC/test.dff" },
	{ "cardb-app", "cardb:/apps/reVC/test.txd", "cardb:/apps/reVC/test.dff" },
};

constexpr const char *kStorageDevices[] = {
	"sd", "usb", "usb2", "usb3", "usb4", "carda", "cardb"
};

bool
fileExists(const char *path)
{
	FILE *file = std::fopen(path, "rb");
	if(file == nullptr)
		return false;
	std::fclose(file);
	return true;
}

bool
directoryExists(const char *path)
{
	DIR *directory = opendir(path);
	if(directory == nullptr)
		return false;
	closedir(directory);
	return true;
}

bool
assetsAreVisible()
{
	for(const AssetLocation &location : kAssetLocations)
		if(fileExists(location.txdPath) || fileExists(location.dffPath))
			return true;
	return false;
}

bool
tryMountUsb()
{
#ifdef WII_BRINGUP_HAVE_DVM
	return dvmProbeMountDiscIface("usb", &__io_usbstorage, 4, 64) > 0;
#else
	return fatMountSimple("usb", &__io_usbstorage);
#endif
}

bool
initializeStorage()
{
	bool mounted = fatInitDefault();
	if(!assetsAreVisible() && !directoryExists("usb:/")){
		for(int attempt = 0; attempt < 4; attempt++){
			if(tryMountUsb()){
				mounted = true;
				break;
			}
			if(attempt < 3)
				usleep(500 * 1000);
		}
	}

	char cwd[256] = "unavailable";
	getcwd(cwd, sizeof(cwd));
	SYS_Report("GX storage: mounted=%s cwd=%s devices=", mounted ? "yes" : "no", cwd);
	for(const char *device : kStorageDevices){
		char root[16];
		std::snprintf(root, sizeof(root), "%s:/", device);
		if(directoryExists(root))
			SYS_Report("%s ", device);
	}
	SYS_Report("\n");
	return mounted;
}

rw::TexDictionary*
loadTextureDictionary(const char *path)
{
	rw::StreamFile stream;
	if(stream.open(path, "rb") == nullptr)
		return nullptr;
	if(!rw::findChunk(&stream, rw::ID_TEXDICTIONARY, nullptr, nullptr)){
		stream.close();
		return nullptr;
	}
	rw::TexDictionary *dictionary = rw::TexDictionary::streamRead(&stream);
	stream.close();
	return dictionary;
}

rw::Clump*
loadClump(const char *path)
{
	rw::StreamFile stream;
	if(stream.open(path, "rb") == nullptr)
		return nullptr;
	if(!rw::findChunk(&stream, rw::ID_CLUMP, nullptr, nullptr)){
		stream.close();
		return nullptr;
	}
	rw::Clump *clump = rw::Clump::streamRead(&stream);
	stream.close();
	if(clump){
		rw::V3d scale = { 0.25f, 0.25f, 0.25f };
		rw::V3d position = { 0.0f, 0.0f, 4.0f };
		clump->getFrame()->scale(&scale, rw::COMBINEREPLACE);
		clump->getFrame()->translate(&position);
	}
	return clump;
}

rw::Camera*
createCamera(int width, int height)
{
	rw::Camera *camera = rw::Camera::create();
	if(camera == nullptr)
		return nullptr;
	rw::Frame *frame = rw::Frame::create();
	if(frame == nullptr){
		camera->destroy();
		return nullptr;
	}
	camera->setFrame(frame);
	rw::V2d viewWindow = { 1.0f, 0.75f };
	camera->setViewWindow(&viewWindow);
	rw::V3d cameraPosition = { 0.0f, 0.0f, -2.0f };
	frame->translate(&cameraPosition);
	camera->frameBuffer = rw::Raster::create(width, height, 0, rw::Raster::CAMERA);
	camera->zBuffer = rw::Raster::create(width, height, 0, rw::Raster::ZBUFFER);
	if(camera->frameBuffer == nullptr || camera->zBuffer == nullptr){
		if(camera->frameBuffer)
			camera->frameBuffer->destroy();
		if(camera->zBuffer)
			camera->zBuffer->destroy();
		camera->setFrame(nullptr);
		frame->destroy();
		camera->destroy();
		return nullptr;
	}
	return camera;
}

rw::Atomic*
createTriangle()
{
	rw::Geometry *geometry = rw::Geometry::create(3, 1,
		rw::Geometry::POSITIONS | rw::Geometry::PRELIT |
		rw::Geometry::TEXTURED);
	if(geometry == nullptr)
		return nullptr;

	rw::Material *material = rw::Material::create();
	if(material == nullptr){
		geometry->destroy();
		return nullptr;
	}
	material->color.alpha = 192;
	rw::Raster *textureRaster = rw::Raster::create(64, 64, 32,
		rw::Raster::C8888 | rw::Raster::TEXTURE);
	if(textureRaster == nullptr){
		material->destroy();
		geometry->destroy();
		return nullptr;
	}
	uint8_t *pixels = textureRaster->lock(0, rw::Raster::LOCKWRITE |
		rw::Raster::LOCKNOFETCH);
	if(pixels == nullptr){
		textureRaster->destroy();
		material->destroy();
		geometry->destroy();
		return nullptr;
	}
	for(int y = 0; y < 64; y++)
		for(int x = 0; x < 64; x++){
			bool light = ((x/8) + (y/8)) % 2 == 0;
			*pixels++ = light ? 255 : 32;
			*pixels++ = light ? 255 : 64;
			*pixels++ = light ? 255 : 192;
			*pixels++ = 255;
		}
	textureRaster->unlock(0);
	rw::Texture *texture = rw::Texture::create(textureRaster);
	if(texture == nullptr){
		textureRaster->destroy();
		material->destroy();
		geometry->destroy();
		return nullptr;
	}
	texture->setFilter(rw::Texture::LINEAR);
	material->setTexture(texture);
	texture->destroy();
	if(geometry->matList.appendMaterial(material) < 0){
		material->destroy();
		geometry->destroy();
		return nullptr;
	}
	material->destroy();

	rw::V3d *vertices = geometry->morphTargets[0].vertices;
	vertices[0].set(0.0f, 0.5f, 0.0f);
	vertices[1].set(-0.5f, -0.5f, 0.0f);
	vertices[2].set(0.5f, -0.5f, 0.0f);
	geometry->texCoords[0][0].u = 0.5f;
	geometry->texCoords[0][0].v = 0.0f;
	geometry->texCoords[0][1].u = 0.0f;
	geometry->texCoords[0][1].v = 1.0f;
	geometry->texCoords[0][2].u = 1.0f;
	geometry->texCoords[0][2].v = 1.0f;

	geometry->colors[0] = { 255, 0, 0, 255 };
	geometry->colors[1] = { 0, 255, 0, 255 };
	geometry->colors[2] = { 0, 0, 255, 255 };
	geometry->triangles[0].v[0] = 0;
	geometry->triangles[0].v[1] = 1;
	geometry->triangles[0].v[2] = 2;
	geometry->triangles[0].matId = 0;
	geometry->calculateBoundingSphere();
	geometry->unlock();

	rw::Atomic *atomic = rw::Atomic::create();
	if(atomic == nullptr){
		geometry->destroy();
		return nullptr;
	}
	atomic->setGeometry(geometry, 0);
	geometry->destroy();

	rw::Frame *frame = rw::Frame::create();
	if(frame == nullptr){
		atomic->destroy();
		return nullptr;
	}
	atomic->setFrame(frame);
	return atomic;
}

} // namespace

extern "C" void wiiLog(const char *format, ...)
{
	char message[512];
	va_list args;
	va_start(args, format);
	std::vsnprintf(message, sizeof(message), format, args);
	va_end(args);
	SYS_Report("%s", message);
}

int main()
{
	VIDEO_Init();

	GXRModeObj *rmode = VIDEO_GetPreferredMode(nullptr);
	void *xfb[2];
	xfb[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	xfb[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();

	const bool storageMounted = initializeStorage();

	void *fifo = memalign(32, kFifoSize);
	std::memset(fifo, 0, kFifoSize);
	GX_Init(fifo, kFifoSize);

	GXColor background = { 0, 0, 64, 255 };
	GX_SetCopyClear(background, GX_MAX_Z24);
	GX_SetViewport(0.0f, 0.0f, rmode->fbWidth, rmode->efbHeight, 0.0f, 1.0f);
	GX_SetDispCopyYScale((f32)rmode->xfbHeight/(f32)rmode->efbHeight);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, rmode->xfbHeight);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering,
	                rmode->viHeight == 2*rmode->xfbHeight ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetDispCopyGamma(GX_GM_1_0);
	GX_CopyDisp(xfb[1], GX_TRUE);

	rw::EngineOpenParams openParameters = {
		{ xfb[0], xfb[1] },
		rmode->fbWidth,
		rmode->efbHeight
	};
	if(!rw::Engine::init() || !rw::Engine::open(&openParameters) ||
	   !rw::Engine::start())
		return 1;
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	rw::SetRenderState(rw::ZTESTENABLE, 1);
	rw::SetRenderState(rw::ZWRITEENABLE, 1);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);
	rw::Camera *camera = createCamera(rmode->fbWidth, rmode->efbHeight);
	rw::Atomic *triangle = createTriangle();
	if(camera == nullptr || triangle == nullptr)
		return 1;

	rw::TexDictionary *testDictionary = nullptr;
	rw::Clump *testClump = nullptr;
	const char *assetLocation = "none";
	bool txdFileFound = false;
	bool dffFileFound = false;
	if(storageMounted){
		for(const AssetLocation &location : kAssetLocations){
			const bool locationHasTxd = fileExists(location.txdPath);
			const bool locationHasDff = fileExists(location.dffPath);
			if(!locationHasTxd && !locationHasDff)
				continue;

			SYS_Report("GX assets: location=%s TXD-file=%s DFF-file=%s\n",
			           location.name, locationHasTxd ? "yes" : "no",
			           locationHasDff ? "yes" : "no");
			txdFileFound |= locationHasTxd;
			dffFileFound |= locationHasDff;
			assetLocation = location.name;
			if(!locationHasTxd || !locationHasDff)
				continue;

			testDictionary = loadTextureDictionary(location.txdPath);
			if(testDictionary == nullptr){
				SYS_Report("GX assets: TXD stream invalid at %s\n", location.txdPath);
				continue;
			}

			rw::TexDictionary::setCurrent(testDictionary);
			testClump = loadClump(location.dffPath);
			if(testClump){
				break;
			}
			SYS_Report("GX assets: DFF stream invalid at %s\n", location.dffPath);

			rw::TexDictionary::setCurrent(nullptr);
			testDictionary->destroy();
			testDictionary = nullptr;
		}
	}
	SYS_Report("GX asset checkpoint: storage=%s location=%s TXD-file=%s DFF-file=%s TXD=%s DFF=%s\n",
	           storageMounted ? "mounted" : "unavailable", assetLocation,
	           txdFileFound ? "found" : "missing",
	           dffFileFound ? "found" : "missing",
	           testDictionary ? "loaded" : "missing",
	           testClump ? "loaded" : "missing");

	rw::V3d rotationAxis = { 0.0f, 0.0f, 1.0f };
	while(1){
		rw::RGBA clearColor = { 0, 0, 64, 255 };
		camera->clear(&clearColor, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
		camera->beginUpdate();
		triangle->render();
		if(testClump)
			testClump->render();
		camera->endUpdate();
		triangle->getFrame()->rotate(&rotationAxis, 0.5f);
		camera->showRaster(rw::Raster::FLIPWAITVSYNCH);
	}

	return 0;
}
