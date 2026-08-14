#ifdef RW_GX
#include <ogc/gx.h>
#endif

namespace rw {

#ifdef RW_GX
struct EngineOpenParams
{
	void *frameBuffers[2];
	int32 width;
	int32 height;
};
#endif

namespace gx {

// GameCube/Wii backend. Geometry, textures, matrices and render state are
// submitted directly to GX.

void registerPlatformPlugins(void);
void *destroyNativeData(void *object, int32 offset, int32 size);

extern Device renderdevice;

struct Im2DVertex
{
	float32 x, y, z, w;
	uint8 r, g, b, a;
	float32 u, v;

	void setScreenX(float32 x) { this->x = x; }
	void setScreenY(float32 y) { this->y = y; }
	void setScreenZ(float32 z) { this->z = z; }
	void setCameraZ(float32 z) { this->w = z; }
	void setRecipCameraZ(float32 recipz) { this->w = 1.0f/recipz; }
	void setColor(uint8 r, uint8 g, uint8 b, uint8 a) {
		this->r = r; this->g = g; this->b = b; this->a = a; }
	void setU(float32 u, float32) { this->u = u; }
	void setV(float32 v, float32) { this->v = v; }

	float32 getScreenX(void) { return this->x; }
	float32 getScreenY(void) { return this->y; }
	float32 getScreenZ(void) { return this->z; }
	float32 getCameraZ(void) { return this->w; }
	float32 getRecipCameraZ(void) { return 1.0f/this->w; }
	RGBA getColor(void) { return makeRGBA(this->r, this->g, this->b, this->a); }
	float32 getU(void) { return this->u; }
	float32 getV(void) { return this->v; }
};

struct Im3DVertex
{
	V3d position;
	V3d normal;
	uint8 r, g, b, a;
	float32 u, v;

	void setX(float32 x) { this->position.x = x; }
	void setY(float32 y) { this->position.y = y; }
	void setZ(float32 z) { this->position.z = z; }
	void setNormalX(float32 x) { this->normal.x = x; }
	void setNormalY(float32 y) { this->normal.y = y; }
	void setNormalZ(float32 z) { this->normal.z = z; }
	void setColor(uint8 r, uint8 g, uint8 b, uint8 a) {
		this->r = r; this->g = g; this->b = b; this->a = a; }
	void setU(float32 u) { this->u = u; }
	void setV(float32 v) { this->v = v; }

	float32 getX(void) { return this->position.x; }
	float32 getY(void) { return this->position.y; }
	float32 getZ(void) { return this->position.z; }
	float32 getNormalX(void) { return this->normal.x; }
	float32 getNormalY(void) { return this->normal.y; }
	float32 getNormalZ(void) { return this->normal.z; }
	RGBA getColor(void) { return makeRGBA(this->r, this->g, this->b, this->a); }
	float32 getU(void) { return this->u; }
	float32 getV(void) { return this->v; }
};

struct Vertex
{
	V3d position;
	RGBA color;
	TexCoords texCoord;
};

struct InstanceData
{
	uint32 numIndices;
	uint32 indexOffset;
	Material *material;
	bool32 vertexAlpha;
	// GX display list contains only the indexed vertex command stream.
	// Material/TEV/depth state stays outside so the same list is reusable
	// for alpha-test passes and dynamically skinned vertex arrays.
	uint8 *displayListData;
	uint8 *displayList;
	uint32 displayListSize;
	uint32 displayListCapacity;
};

struct InstanceDataHeader : rw::InstanceDataHeader
{
	uint32 serialNumber;
	uint32 numMeshes;
	uint32 numVertices;
	uint32 numIndices;
	bool32 triangleStrip;
	bool32 vertexAlpha;
	bool32 hasPrelight;
	bool32 hasNormals;
	bool32 hasTexCoords;
	bool32 vertexCacheDirty;
	uint8 *vertexData;
	V3d *positions;
	V3d *normals;
	RGBA *colors;
	TexCoords *texCoords;
	uint16 *indices;
	InstanceData *meshes;
};

class ObjPipeline : public rw::ObjPipeline
{
public:
	void init(void);
	static ObjPipeline *create(void);
};

ObjPipeline *makeDefaultPipeline(void);
ObjPipeline *makeSkinPipeline(void);
ObjPipeline *makeMatFXPipeline(void);
void initSkin(void);
void initMatFX(void);

// Common GX geometry instancing/streaming. These are deliberately host-safe
// so the gxnative conversion tool can produce Wii-native DFFs on a PC.
InstanceDataHeader *instanceGeometry(Geometry *geometry);
void freeInstanceData(Geometry *geometry);
bool32 makeNativeGeometry(Geometry *geometry);
Stream *readNativeData(Stream *stream, int32 len, void *object, int32 offset, int32 size);
Stream *writeNativeData(Stream *stream, int32 len, void *object, int32 offset, int32 size);
int32 getSizeNativeData(void *object, int32 offset, int32 size);

#ifdef RW_GX
void prepareNativeGeometryForRender(InstanceDataHeader *header);
void freeNativeDisplayLists(InstanceDataHeader *header);
#endif

void renderAtomic(Atomic *atomic, InstanceDataHeader *header);
void skinAtomic(Atomic *atomic, InstanceDataHeader *header);
void setVertexAlpha(bool32 enable);
void setNativeCameraMatrices(const float32 *view, const float32 *projection,
	bool32 perspective, float32 nearPlane, float32 farPlane);
void loadNativeWorldMatrix(const Matrix *world);
void transformNativeWorldPointToView(const V3d &source, V3d *destination);
void transformNativeWorldVectorToView(const V3d &source, V3d *destination);
void loadNativeIm2DMatrices(int32 width, int32 height, float32 nearPlane,
	float32 farPlane);
void clearNativeCamera(int32 x, int32 y, int32 width, int32 height,
	const RGBA &color, bool32 clearColor, bool32 clearDepth,
	float32 nearPlane, float32 farPlane);
void setNativeBlendState(bool32 enable, uint32 source, uint32 destination);
void setNativeDepthState(bool32 test, bool32 write);
void setNativeFogEnabled(bool32 enable);
void setNativeFogColor(uint32 color);
void setNativeFogRange(float32 start, float32 end, float32 nearPlane,
	float32 farPlane, bool32 perspective);
void setNativeCullMode(uint32 mode);
void setNativeAlphaTest(uint32 function, uint32 reference);
void setNativeViewport(int32 x, int32 y, int32 width, int32 height);
void prepareNativeDraw(void);
void prepareNativeDisplayCopy(void);
void invalidateNativeRenderState(void);

// Per-frame submission counters, reported by showRaster and reset there.
extern uint32 renderedAtomicCount;
extern uint32 renderedGsAlphaPassCount;
extern uint32 im2DPrimitiveCount;
extern uint32 im3DPrimitiveCount;
extern uint32 nativeTextureMemory;
extern uint32 nativeTextureUploadCount;
extern uint32 nativeTextureWaitCount;
extern uint32 nativeDisplayListMemory;
extern uint32 nativeDisplayListBuildCount;
extern uint32 nativeDisplayListCallCount;
extern uint32 nativeDisplayListFallbackCount;

// Single texture stage shared by the atomic and immediate-mode paths. It
// caches what is bound so they do not reprogram the texture unit per draw.
void setTexture(Texture *texture);
void setRasterStage(Raster *raster);
Raster *getRasterStage(void);
void evictRaster(Raster *raster);

void im2DRenderLine(void *vertices, int32 numVertices, int32 vertex1,
	int32 vertex2);
void im2DRenderTriangle(void *vertices, int32 numVertices, int32 vertex1,
	int32 vertex2, int32 vertex3);
void im2DRenderPrimitive(PrimitiveType primitiveType, void *vertices,
	int32 numVertices);
void im2DRenderIndexedPrimitive(PrimitiveType primitiveType, void *vertices,
	int32 numVertices, void *indices, int32 numIndices);

void im3DTransform(void *vertices, int32 numVertices, Matrix *world,
	uint32 flags);
void im3DRenderPrimitive(PrimitiveType primitiveType);
void im3DRenderIndexedPrimitive(PrimitiveType primitiveType, void *indices,
	int32 numIndices);
void im3DEnd(void);

struct GxRaster
{
	uint8 *pixels;
	uint8 *nativeAllocation;
	uint8 *nativePixels;
	uint32 nativeSize;
	uint8 nativeFormat;
	bool32 nativeReady;
	bool32 nativeUsed;
	bool32 hasAlpha;
#ifdef RW_GX
	GXTexObj nativeTexture;
#endif
	int32 bytesPerPixel;
	int32 filterMode;
	int32 addressU;
	int32 addressV;
	int32 numLevels;
};

void updateNativeRasterSampler(GxRaster *nativeRaster);
void initNativeTevState(void);
void setupNativeTev(const RGBA &materialColor, GxRaster *nativeRaster);

uint32 gxTextureLevelSize(int32 width, int32 height, uint32 format);
uint32 gxTextureSize(int32 width, int32 height, uint32 format,
	int32 numLevels);
uint32 gxTextureLevelOffset(int32 width, int32 height, uint32 format,
	int32 level);
void gxConvertTexture(const uint8 *source, int32 sourceStride,
	int32 width, int32 height, uint8 *destination, uint32 format);
uint32 dxt1TextureLevelSize(int32 width, int32 height);
bool32 dxt1TextureHasAlpha(const uint8 *source, uint32 sourceSize,
	int32 width, int32 height);
bool32 gxConvertDxt1ToCmpr(const uint8 *source, uint32 sourceSize,
	int32 width, int32 height, uint8 *destination);

extern int32 nativeRasterOffset;
#define GETGXRASTEREXT(raster) PLUGINOFFSET(GxRaster, raster, nativeRasterOffset)
void registerNativeRaster(void);

Raster *rasterCreate(Raster *raster);
uint8 *rasterLock(Raster *raster, int32 level, int32 lockMode);
void rasterUnlock(Raster *raster, int32 level);
bool32 rasterSetLevelFromImage(Raster *raster, Image *image, int32 level);
bool32 rasterSetFromImageWithMipmaps(Raster *raster, Image *image);
bool32 rasterSetLevelFromDxt1(Raster *raster, const uint8 *pixels,
	uint32 dataSize, int32 level);
uint8 *rasterLockPalette(Raster *raster, int32 lockMode);
void rasterUnlockPalette(Raster *raster);
int32 rasterNumLevels(Raster *raster);
bool32 imageFindRasterFormat(Image *image, int32 type, int32 *width,
	int32 *height, int32 *depth, int32 *format);
bool32 rasterFromImage(Raster *raster, Image *image);
Image *rasterToImage(Raster *raster);
Texture *readNativeTextureD3D8(Stream *stream);
void setNativeTextureTrace(bool32 enabled);
bool32 nativeTextureTraceEnabled(void);
// Per-frame GX diagnostics. The application controls this because librw is
// built as a separate target and cannot see the Wii port's CREATE_LOG switch.
void setFrameTrace(bool32 enabled);

}
}
