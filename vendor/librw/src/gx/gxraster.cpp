#include <assert.h>
#include <string.h>

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

int32 nativeRasterOffset;
uint32 nativeTextureMemory;
uint32 nativeTextureUploadCount;
uint32 nativeTextureWaitCount;

static void
waitForTexture(void)
{
	GX_DrawDone();
	nativeTextureWaitCount++;
}

static void*
createNativeRaster(void *object, int32 offset, int32)
{
	GxRaster *nativeRaster = PLUGINOFFSET(GxRaster, object, offset);
	nativeRaster->pixels = nil;
	nativeRaster->nativeAllocation = nil;
	nativeRaster->nativePixels = nil;
	nativeRaster->nativeSize = 0;
	nativeRaster->nativeFormat = GX_TF_RGBA8;
	nativeRaster->nativeReady = 0;
	nativeRaster->nativeUsed = 0;
	nativeRaster->hasAlpha = 0;
	memset(&nativeRaster->nativeTexture, 0,
	       sizeof(nativeRaster->nativeTexture));
	nativeRaster->bytesPerPixel = 0;
	// Zero is not a valid RenderWare mode, so the texture unit is always
	// programmed the first time this raster is bound.
	nativeRaster->filterMode = 0;
	nativeRaster->addressU = 0;
	nativeRaster->addressV = 0;
	nativeRaster->numLevels = 1;
	return object;
}

static void*
destroyNativeRaster(void *object, int32 offset, int32)
{
	Raster *raster = (Raster*)object;
	GxRaster *nativeRaster = PLUGINOFFSET(GxRaster, raster, offset);
	if(nativeRaster->nativeUsed)
		waitForTexture();
	if(raster->platform == PLATFORM_GX){
		evictRaster(raster);
	}
	rwFree(nativeRaster->pixels);
	rwFree(nativeRaster->nativeAllocation);
	assert(nativeRaster->nativeSize <= nativeTextureMemory);
	nativeTextureMemory -= nativeRaster->nativeSize;
	nativeRaster->pixels = nil;
	nativeRaster->nativeAllocation = nil;
	nativeRaster->nativePixels = nil;
	nativeRaster->nativeSize = 0;
	nativeRaster->nativeReady = 0;
	nativeRaster->nativeUsed = 0;
	nativeRaster->hasAlpha = 0;
	return object;
}

// Size of one mip level of a base dimension, matching the halving the lock
// below performs on the raster.
static int32
levelSize(int32 size, int32 level)
{
	while(level-- > 0 && size > 1)
		size /= 2;
	return size;
}

static uint8
nativeFormat(int32 format)
{
	switch(format & 0xF00){
	case Raster::C565: return GX_TF_RGB565;
	case Raster::C1555:
	case Raster::C4444: return GX_TF_RGB5A3;
	default: return GX_TF_RGBA8;
	}
}

static uint8
nativeAddress(int32 addressing)
{
	switch(addressing){
	case Texture::CLAMP: return GX_CLAMP;
	case Texture::MIRROR: return GX_MIRROR;
	default: return GX_REPEAT;
	}
}

static uint8
nativeFilter(int32 filter)
{
	switch(filter){
	case Texture::LINEAR:
	case Texture::MIPLINEAR:
	case Texture::LINEARMIPLINEAR:
		return GX_LINEAR;
	default:
		return GX_NEAR;
	}
}

static uint8
nativeMinFilter(int32 filter, int32 numLevels)
{
	if(numLevels <= 1)
		return nativeFilter(filter);
	switch(filter){
	case Texture::MIPNEAREST: return GX_NEAR_MIP_NEAR;
	case Texture::MIPLINEAR: return GX_LIN_MIP_NEAR;
	case Texture::LINEARMIPNEAREST: return GX_NEAR_MIP_LIN;
	case Texture::LINEARMIPLINEAR: return GX_LIN_MIP_LIN;
	default: return nativeFilter(filter);
	}
}

void
updateNativeRasterSampler(GxRaster *nativeRaster)
{
	if(!nativeRaster->nativeReady)
		return;
	GX_InitTexObjWrapMode(&nativeRaster->nativeTexture,
	                     nativeAddress(nativeRaster->addressU),
	                     nativeAddress(nativeRaster->addressV));
	GX_InitTexObjLOD(&nativeRaster->nativeTexture,
	                 nativeMinFilter(nativeRaster->filterMode,
	                                 nativeRaster->numLevels),
	                 nativeFilter(nativeRaster->filterMode),
	                 0.0f, nativeRaster->numLevels - 1, 0.0f,
	                 GX_ENABLE, GX_ENABLE, GX_ANISO_1);
}

static uint8*
alignTexture(uint8 *pixels)
{
	return (uint8*)(((uintptr)pixels + 31) & ~uintptr(31));
}

static bool32
pixelsHaveAlpha(const uint8 *pixels, int32 width, int32 height, int32 stride)
{
	for(int32 y = 0; y < height; y++){
		const uint8 *row = pixels + y*stride;
		for(int32 x = 0; x < width; x++)
			if(row[x*4 + 3] != 255)
				return 1;
	}
	return 0;
}

static void
copyImageToRgba(const Image *image, uint8 *pixels)
{
	for(int32 y = 0; y < image->height; y++){
		const uint8 *source = image->pixels + y*image->stride;
		uint8 *destination = pixels + y*image->width*4;
		for(int32 x = 0; x < image->width; x++){
			destination[0] = source[0];
			destination[1] = source[1];
			destination[2] = source[2];
			destination[3] = image->bpp == 4 ? source[3] : 255;
			destination += 4;
			source += image->bpp;
		}
	}
}

static void
downsampleRgbaInPlace(uint8 *pixels, int32 width, int32 height,
	bool32 hasAlpha)
{
	int32 newWidth = width > 1 ? width/2 : 1;
	int32 newHeight = height > 1 ? height/2 : 1;
	int32 samplesX = width > 1 ? 2 : 1;
	int32 samplesY = height > 1 ? 2 : 1;
	int32 sourceStride = width*4;
	int32 destinationStride = newWidth*4;

	if(!hasAlpha){
		for(int32 y = 0; y < newHeight; y++){
			uint8 *destination = pixels + y*destinationStride;
			for(int32 x = 0; x < newWidth; x++){
				uint32 red = 0;
				uint32 green = 0;
				uint32 blue = 0;
				uint32 count = 0;
				for(int32 sampleY = 0; sampleY < samplesY; sampleY++){
					const uint8 *source = pixels +
						(y*2 + sampleY)*sourceStride + x*2*4;
					for(int32 sampleX = 0; sampleX < samplesX; sampleX++){
						red += source[0];
						green += source[1];
						blue += source[2];
						count++;
						source += 4;
					}
				}
				destination[0] = red/count;
				destination[1] = green/count;
				destination[2] = blue/count;
				destination[3] = 255;
				destination += 4;
			}
		}
		return;
	}

	for(int32 y = 0; y < newHeight; y++){
		uint8 *destination = pixels + y*destinationStride;
		for(int32 x = 0; x < newWidth; x++){
			uint32 alpha = 0;
			uint32 weightedRed = 0;
			uint32 weightedGreen = 0;
			uint32 weightedBlue = 0;
			uint32 count = 0;
			for(int32 sampleY = 0; sampleY < samplesY; sampleY++){
				const uint8 *source = pixels +
					(y*2 + sampleY)*sourceStride + x*2*4;
				for(int32 sampleX = 0; sampleX < samplesX; sampleX++){
					uint32 sourceAlpha = source[3];
					alpha += sourceAlpha;
					weightedRed += source[0]*sourceAlpha;
					weightedGreen += source[1]*sourceAlpha;
					weightedBlue += source[2]*sourceAlpha;
					count++;
					source += 4;
				}
			}

			if(alpha != 0){
				destination[0] = weightedRed/alpha;
				destination[1] = weightedGreen/alpha;
				destination[2] = weightedBlue/alpha;
			}else
				destination[0] = destination[1] = destination[2] = 0;
			destination[3] = alpha/count;
			destination += 4;
		}
	}
}

struct TextureUpload
{
	uint8 *allocation;
	uint8 *pixels;
	uint32 requiredSize;
	uint32 offset;
	uint32 levelSize;
	bool32 grew;
	bool32 wholeChain;
};

static bool32
prepareTextureUpload(Raster *raster, int32 level, TextureUpload *upload)
{
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	int32 numLevels = level + 1;
	if(numLevels < nativeRaster->numLevels)
		numLevels = nativeRaster->numLevels;
	upload->wholeChain = 0;
	upload->requiredSize = gxTextureSize(raster->originalWidth,
		raster->originalHeight, nativeRaster->nativeFormat, numLevels);
	upload->grew = upload->requiredSize > nativeRaster->nativeSize;
	upload->allocation = nativeRaster->nativeAllocation;
	upload->pixels = nativeRaster->nativePixels;
	if(upload->grew){
		upload->allocation = rwNewT(uint8, upload->requiredSize + 31,
			MEMDUR_EVENT | ID_RASTERGX);
		if(upload->allocation == nil)
			return 0;
		upload->pixels = alignTexture(upload->allocation);
		memset(upload->pixels, 0, upload->requiredSize);
		if(nativeRaster->nativePixels)
			memcpy(upload->pixels, nativeRaster->nativePixels,
			       nativeRaster->nativeSize);
	}

	if(!upload->grew && nativeRaster->nativeUsed)
		waitForTexture();
	upload->offset = gxTextureLevelOffset(raster->originalWidth,
		raster->originalHeight, nativeRaster->nativeFormat, level);
	upload->levelSize = gxTextureLevelSize(raster->width, raster->height,
		nativeRaster->nativeFormat);
	return 1;
}

static void
finishTextureUpload(Raster *raster, int32 level, TextureUpload *upload)
{
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	bool32 storeWholeChain = upload->grew || upload->wholeChain;
	uint32 storeSize = storeWholeChain ? upload->requiredSize :
	                 upload->levelSize;
	DCStoreRange(storeWholeChain ? upload->pixels :
	             upload->pixels + upload->offset, storeSize);

	if(upload->grew && nativeRaster->nativeUsed)
		waitForTexture();
	if(upload->grew){
		nativeTextureMemory += upload->requiredSize - nativeRaster->nativeSize;
		rwFree(nativeRaster->nativeAllocation);
		nativeRaster->nativeAllocation = upload->allocation;
		nativeRaster->nativePixels = upload->pixels;
		nativeRaster->nativeSize = upload->requiredSize;
	}
	if(level + 1 > nativeRaster->numLevels)
		nativeRaster->numLevels = level + 1;
	GX_InitTexObj(&nativeRaster->nativeTexture, nativeRaster->nativePixels,
	              raster->originalWidth, raster->originalHeight,
	              nativeRaster->nativeFormat, GX_REPEAT, GX_REPEAT,
	              nativeRaster->numLevels > 1);
	nativeRaster->nativeReady = 1;
	nativeRaster->nativeUsed = 0;
	GX_InvalidateTexAll();
	updateNativeRasterSampler(nativeRaster);
	nativeTextureUploadCount++;
	// The level count changes minification and the upload can replace the
	// object. Force the texture-stage cache to adopt both on its next bind.
	nativeRaster->filterMode = 0;
	setRasterStage(nil);
}

static void
uploadTexture(Raster *raster, int32 level)
{
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	assert(nativeRaster->nativeFormat != GX_TF_CMPR);
	TextureUpload upload;
	if(!prepareTextureUpload(raster, level, &upload))
		return;
	gxConvertTexture(nativeRaster->pixels, raster->stride,
	                 raster->width, raster->height,
	                 upload.pixels + upload.offset,
	                 nativeRaster->nativeFormat);
	finishTextureUpload(raster, level, &upload);
}

bool32
rasterSetFromImageWithMipmaps(Raster *raster, Image *image)
{
	if(image->bpp != 3 && image->bpp != 4)
		return 0;
	if(image->width != raster->originalWidth ||
	   image->height != raster->originalHeight)
		return 0;

	int32 numLevels = Raster::calculateNumLevels(image->width, image->height);
	if(numLevels <= 1)
		return rasterSetLevelFromImage(raster, image, 0);

	uint32 rgbaSize = uint32(image->width)*uint32(image->height)*4;
	uint8 *rgba = rwNewT(uint8, rgbaSize, MEMDUR_FUNCTION | ID_RASTERGX);
	if(rgba == nil)
		return 0;
	copyImageToRgba(image, rgba);

	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	if(nativeRaster->nativeFormat == GX_TF_CMPR){
		rwFree(rgba);
		return 0;
	}
	TextureUpload upload;
	if(!prepareTextureUpload(raster, numLevels - 1, &upload)){
		rwFree(rgba);
		return 0;
	}
	upload.wholeChain = 1;

	bool32 hasAlpha = nativeRaster->nativeFormat != GX_TF_RGB565 &&
		pixelsHaveAlpha(rgba, image->width, image->height, image->width*4);
	int32 width = image->width;
	int32 height = image->height;
	for(int32 level = 0; level < numLevels; level++){
		uint32 offset = gxTextureLevelOffset(raster->originalWidth,
			raster->originalHeight, nativeRaster->nativeFormat, level);
		gxConvertTexture(rgba, width*4, width, height,
		                 upload.pixels + offset, nativeRaster->nativeFormat);
		if(level + 1 < numLevels){
			downsampleRgbaInPlace(rgba, width, height, hasAlpha);
			if(width > 1)
				width /= 2;
			if(height > 1)
				height /= 2;
		}
	}

	nativeRaster->hasAlpha = hasAlpha;
	finishTextureUpload(raster, numLevels - 1, &upload);
	rwFree(rgba);
	return 1;
}

Raster*
rasterCreate(Raster *raster)
{
	if(raster->width == 0 || raster->height == 0){
		raster->flags |= Raster::DONTALLOCATE;
		return raster;
	}
	switch(raster->type){
	case Raster::CAMERA:
		raster->format = Raster::C8888;
		raster->depth = 32;
		break;
	case Raster::ZBUFFER:
		raster->format = Raster::D24;
		raster->depth = 24;
		break;
	case Raster::NORMAL:
	case Raster::TEXTURE:
		{
			GxRaster *nativeRaster = GETGXRASTEREXT(raster);
			if(raster->format != Raster::C565 && raster->format != Raster::C1555 &&
			   raster->format != Raster::C4444 && raster->format != Raster::C8888)
				raster->format = Raster::C8888;
			raster->depth = 32;
			raster->stride = raster->width*4;
			nativeRaster->bytesPerPixel = 4;
			nativeRaster->nativeFormat = nativeFormat(raster->format);
			nativeRaster->hasAlpha = Raster::formatHasAlpha(raster->format);
			raster->originalWidth = raster->width;
			raster->originalHeight = raster->height;
			raster->originalStride = raster->stride;
			return raster;
		}
	default:
		return nil;
	}

	raster->flags |= Raster::DONTALLOCATE;
	raster->stride = 0;
	raster->pixels = nil;
	raster->palette = nil;
	raster->originalWidth = raster->width;
	raster->originalHeight = raster->height;
	raster->originalStride = 0;
	raster->originalPixels = nil;
	return raster;
}

uint8*
rasterLock(Raster *raster, int32 level, int32)
{
	if(raster->type != Raster::NORMAL && raster->type != Raster::TEXTURE)
		return nil;
	int32 maxLevels = Raster::calculateNumLevels(raster->originalWidth,
	                                            raster->originalHeight);
	if(level < 0 || level >= maxLevels)
		return nil;
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	if(nativeRaster->pixels != nil)
		return nil;
	if(nativeRaster->nativeFormat == GX_TF_CMPR)
		return nil;

	// Like the other backends, describe the locked level through the raster
	// itself and restore the base size on unlock.
	raster->width = levelSize(raster->originalWidth, level);
	raster->height = levelSize(raster->originalHeight, level);
	raster->stride = raster->width*nativeRaster->bytesPerPixel;

	nativeRaster->pixels = rwNewT(uint8, raster->stride*raster->height,
		MEMDUR_FUNCTION | ID_RASTERGX);
	if(nativeRaster->pixels == nil){
		raster->width = raster->originalWidth;
		raster->height = raster->originalHeight;
		raster->stride = raster->originalStride;
		return nil;
	}
	raster->pixels = nativeRaster->pixels;
	return raster->pixels;
}

void
rasterUnlock(Raster *raster, int32 level)
{
	if(raster->pixels == nil)
		return;
	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	bool32 hasAlpha = nativeRaster->nativeFormat != GX_TF_RGB565 &&
		pixelsHaveAlpha(nativeRaster->pixels, raster->width,
		                raster->height, raster->stride);
	if(level == 0)
		nativeRaster->hasAlpha = hasAlpha;
	else
		nativeRaster->hasAlpha |= hasAlpha;
	uploadTexture(raster, level);
	raster->pixels = nil;
	rwFree(nativeRaster->pixels);
	nativeRaster->pixels = nil;
	raster->width = raster->originalWidth;
	raster->height = raster->originalHeight;
	raster->stride = raster->originalStride;
}

uint8*
rasterLockPalette(Raster*, int32)
{
	return nil;
}

void rasterUnlockPalette(Raster*) {}

int32
rasterNumLevels(Raster *raster)
{
	return GETGXRASTEREXT(raster)->numLevels;
}

bool32
imageFindRasterFormat(Image *image, int32 type, int32 *width, int32 *height,
	int32 *depth, int32 *format)
{
	*width = image->width;
	*height = image->height;
	*depth = 32;
	*format = Raster::C8888 | type;
	return 1;
}

bool32
rasterSetLevelFromImage(Raster *raster, Image *image, int32 level)
{
	if(image->bpp != 3 && image->bpp != 4)
		return 0;
	if(image->width != levelSize(raster->originalWidth, level) ||
	   image->height != levelSize(raster->originalHeight, level))
		return 0;

	uint8 *destination = rasterLock(raster, level, Raster::LOCKWRITE |
		Raster::LOCKNOFETCH);
	if(destination == nil)
		return 0;
	for(int32 y = 0; y < raster->height; y++){
		uint8 *source = image->pixels + y*image->stride;
		uint8 *destinationRow = destination + y*raster->stride;
		for(int32 x = 0; x < raster->width; x++){
			destinationRow[0] = source[0];
			destinationRow[1] = source[1];
			destinationRow[2] = source[2];
			destinationRow[3] = image->bpp == 4 ? source[3] : 255;
			destinationRow += 4;
			source += image->bpp;
		}
	}
	rasterUnlock(raster, level);
	return 1;
}

bool32
rasterSetLevelFromDxt1(Raster *raster, const uint8 *pixels,
	uint32 dataSize, int32 level)
{
	if(raster->type != Raster::NORMAL && raster->type != Raster::TEXTURE)
		return 0;
	int32 maxLevels = Raster::calculateNumLevels(raster->originalWidth,
	                                            raster->originalHeight);
	if(level < 0 || level >= maxLevels)
		return 0;
	int32 width = levelSize(raster->originalWidth, level);
	int32 height = levelSize(raster->originalHeight, level);
	if(dataSize < dxt1TextureLevelSize(width, height))
		return 0;

	GxRaster *nativeRaster = GETGXRASTEREXT(raster);
	if(nativeRaster->nativeReady && nativeRaster->nativeFormat != GX_TF_CMPR)
		return 0;
	nativeRaster->nativeFormat = GX_TF_CMPR;
	raster->width = width;
	raster->height = height;
	TextureUpload upload;
	if(!prepareTextureUpload(raster, level, &upload)){
		raster->width = raster->originalWidth;
		raster->height = raster->originalHeight;
		return 0;
	}
	bool32 converted = gxConvertDxt1ToCmpr(pixels, dataSize, width, height,
		upload.pixels + upload.offset);
	if(!converted){
		if(upload.grew)
			rwFree(upload.allocation);
		raster->width = raster->originalWidth;
		raster->height = raster->originalHeight;
		return 0;
	}
	bool32 hasAlpha = dxt1TextureHasAlpha(pixels, dataSize, width, height);
	if(level == 0)
		nativeRaster->hasAlpha = hasAlpha;
	else
		nativeRaster->hasAlpha |= hasAlpha;
	finishTextureUpload(raster, level, &upload);
	raster->width = raster->originalWidth;
	raster->height = raster->originalHeight;
	return converted;
}

bool32
rasterFromImage(Raster *raster, Image *image)
{
	if(Texture::getMipmapping() && Texture::getAutoMipmapping())
		return rasterSetFromImageWithMipmaps(raster, image);
	return rasterSetLevelFromImage(raster, image, 0);
}

Image*
rasterToImage(Raster*)
{
	return nil;
}

void
registerNativeRaster(void)
{
	nativeRasterOffset = Raster::registerPlugin(sizeof(GxRaster), ID_RASTERGX,
		createNativeRaster, destroyNativeRaster, nil);
}

}
}
