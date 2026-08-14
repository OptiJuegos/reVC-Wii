#include <cstdio>
#include <cstring>
#include <vector>

#include <rw.h>

using namespace rw;

// GTA's node-name extension is required by vehicles/peds and is not a core
// librw plugin. Re-register the tiny streamed extension here so converting a
// DFF does not strip frame names.
static int32 nodeNameOffset = -1;
static const uint32 ID_NODENAME = MAKEPLUGINID(0x0253F2, 0xFE);

static char *
nodeName(void *object)
{
	return PLUGINOFFSET(char, object, nodeNameOffset);
}

static void *
nodeNameCtor(void *object, int32, int32)
{
	if(nodeNameOffset >= 0)
		nodeName(object)[0] = '\0';
	return object;
}

static void *
nodeNameDtor(void *object, int32, int32)
{
	return object;
}

static void *
nodeNameCopy(void *dst, void *src, int32, int32)
{
	strncpy(nodeName(dst), nodeName(src), 23);
	nodeName(dst)[23] = '\0';
	return dst;
}

static Stream *
nodeNameRead(Stream *stream, int32 len, void *object, int32, int32)
{
	int32 n = len < 23 ? len : 23;
	if(n > 0)
		stream->read8(nodeName(object), n);
	nodeName(object)[n] = '\0';
	if(len > n)
		stream->seek(len - n);
	return stream;
}

static Stream *
nodeNameWrite(Stream *stream, int32 len, void *object, int32, int32)
{
	if(len > 0)
		stream->write8(nodeName(object), len);
	return stream;
}

static int32
nodeNameSize(void *object, int32, int32)
{
	return (int32)strlen(nodeName(object));
}

static bool
attachPlugins(void)
{
	registerMeshPlugin();
	registerNativeDataPlugin();
	registerAtomicRightsPlugin();
	registerMaterialRightsPlugin();
	registerSkinPlugin();
	registerHAnimPlugin();
	registerMatFXPlugin();
	registerAnisotropyPlugin();
	registerUserDataPlugin();
	registerUVAnimPlugin();

	nodeNameOffset = Frame::registerPlugin(24, ID_NODENAME,
		nodeNameCtor, nodeNameDtor, nodeNameCopy);
	if(nodeNameOffset < 0)
		return false;
	Frame::registerPluginStream(ID_NODENAME,
		nodeNameRead, nodeNameWrite, nodeNameSize);
	return true;
}

static bool
alreadySeen(const std::vector<Geometry*> &geometries, Geometry *geometry)
{
	for(size_t i = 0; i < geometries.size(); i++)
		if(geometries[i] == geometry)
			return true;
	return false;
}

int
main(int argc, char **argv)
{
	if(argc != 3){
		fprintf(stderr, "usage: %s input.dff output.dff\n", argv[0]);
		fprintf(stderr, "Converts static geometry to Wii/GX native streams.\n");
		return 1;
	}

	if(!Engine::init()){
		fprintf(stderr, "gxnative: Engine::init failed\n");
		return 1;
	}
	if(!attachPlugins()){
		fprintf(stderr, "gxnative: plugin attach failed\n");
		return 1;
	}
	if(!Engine::open(nil) || !Engine::start()){
		fprintf(stderr, "gxnative: engine start failed\n");
		return 1;
	}

	StreamFile input;
	if(!input.open(argv[1], "rb")){
		fprintf(stderr, "gxnative: couldn't open %s\n", argv[1]);
		return 1;
	}
	ChunkHeaderInfo top;
	if(!readChunkHeaderInfo(&input, &top) || top.type != ID_CLUMP){
		fprintf(stderr, "gxnative: %s is not a DFF/clump\n", argv[1]);
		return 1;
	}
	version = top.version;
	build = top.build;
	input.seek(0, 0);
	if(!findChunk(&input, ID_CLUMP, nil, nil)){
		fprintf(stderr, "gxnative: couldn't find clump chunk\n");
		return 1;
	}
	Clump *clump = Clump::streamRead(&input);
	input.close();
	if(clump == nil){
		fprintf(stderr, "gxnative: couldn't decode clump\n");
		return 1;
	}

	std::vector<Geometry*> seen;
	uint32 converted = 0;
	uint32 skippedSkin = 0;
	uint32 skippedMorph = 0;
	uint32 skippedNative = 0;
	uint32 failed = 0;
	FORLIST(lnk, clump->atomics){
		Atomic *atomic = Atomic::fromClump(lnk);
		Geometry *geometry = atomic->geometry;
		if(geometry == nil || alreadySeen(seen, geometry))
			continue;
		seen.push_back(geometry);

		if(geometry->flags & Geometry::NATIVE){
			skippedNative++;
			continue;
		}
		if(Skin::get(geometry) != nil){
			// gxskin.cpp needs the original bind-pose vertices/weights.
			skippedSkin++;
			continue;
		}
		if(geometry->numMorphTargets != 1){
			skippedMorph++;
			continue;
		}
		if(gx::makeNativeGeometry(geometry))
			converted++;
		else
			failed++;
	}

	StreamFile output;
	if(!output.open(argv[2], "wb")){
		fprintf(stderr, "gxnative: couldn't create %s\n", argv[2]);
		clump->destroy();
		return 1;
	}
	bool ok = clump->streamWrite(&output);
	output.close();
	clump->destroy();

	fprintf(stderr,
		"gxnative: converted=%u skin=%u morph=%u already-native=%u failed=%u\n",
		converted, skippedSkin, skippedMorph, skippedNative, failed);
	if(!ok){
		fprintf(stderr, "gxnative: write failed\n");
		return 1;
	}

	Engine::stop();
	Engine::close();
	Engine::term();
	return failed ? 2 : 0;
}
