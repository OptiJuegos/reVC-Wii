#include <string.h>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"

#include "rwgx.h"

#ifdef RW_GX
#include <gccore.h>
#endif

namespace rw {
namespace gx {

static bool32 nativeTextureTrace;

void
setNativeTextureTrace(bool32 enabled)
{
	nativeTextureTrace = enabled;
}

bool32
nativeTextureTraceEnabled(void)
{
	return nativeTextureTrace;
}

#ifdef RW_GX
#define GX_NATIVE_TRACE(...) do { if(nativeTextureTrace) SYS_Report(__VA_ARGS__); } while(0)
#else
#define GX_NATIVE_TRACE(...) ((void)0)
#endif

static uint16
readLittle16(const uint8 *source)
{
	return uint16(source[0] | source[1] << 8);
}

static void
decodeColor(RGBA *color, const uint8 *source, int32 format)
{
	switch(format & 0xF00){
	case Raster::C8888:
		color->red = source[2];
		color->green = source[1];
		color->blue = source[0];
		color->alpha = source[3];
		break;
	case Raster::C888:
		color->red = source[2];
		color->green = source[1];
		color->blue = source[0];
		color->alpha = 255;
		break;
	case Raster::C565:
		{
			uint16 value = readLittle16(source);
			color->red = ((value >> 11) & 0x1F)*255/31;
			color->green = ((value >> 5) & 0x3F)*255/63;
			color->blue = (value & 0x1F)*255/31;
			color->alpha = 255;
		}
		break;
	case Raster::C1555:
		{
			uint16 value = readLittle16(source);
			color->red = ((value >> 10) & 0x1F)*255/31;
			color->green = ((value >> 5) & 0x1F)*255/31;
			color->blue = (value & 0x1F)*255/31;
			color->alpha = value & 0x8000 ? 255 : 0;
		}
		break;
	case Raster::C4444:
		{
			uint16 value = readLittle16(source);
			color->red = ((value >> 8) & 0xF)*17;
			color->green = ((value >> 4) & 0xF)*17;
			color->blue = (value & 0xF)*17;
			color->alpha = ((value >> 12) & 0xF)*17;
		}
		break;
	default:
		color->red = color->green = color->blue = 255;
		color->alpha = 255;
		break;
	}
}

static bool32
decodePixels(Image *image, const uint8 *data, uint32 dataSize,
	const uint8 *palette, int32 format, int32 compression)
{
	if(compression == 1 || compression == 3 || compression == 5){
		uint32 blockSize = compression == 1 ? 8 : 16;
		uint32 required = ((image->width+3)/4)*((image->height+3)/4)*blockSize;
		if(dataSize < required)
			return 0;
		// The decompressors in image.cpp write whole 4x4 blocks without
		// clamping them to the image, so an image narrower or shorter than one
		// block runs off the end of the pixel buffer.  Only the last mip
		// levels of a compressed texture are that small; refusing them ends
		// the chain there, which the caller handles.
		if(image->width < 4 || image->height < 4)
			return 0;
		image->setPixelsDXT(compression, (uint8*)data);
		return 1;
	}

	RGBA *destination = (RGBA*)image->pixels;
	int32 pixelCount = image->width*image->height;
	if(format & (Raster::PAL4 | Raster::PAL8)){
		bool32 packed = (format & Raster::PAL4) && dataSize < (uint32)pixelCount;
		uint32 required = packed ? (pixelCount+1)/2 : pixelCount;
		if(dataSize < required)
			return 0;
		for(int32 i = 0; i < pixelCount; i++){
			uint8 index = packed ?
				(data[i/2] >> ((i & 1)*4)) & 0xF : data[i];
			// Palette entries are already in the destination's channel order,
			// unlike the direct formats decodeColor() has to unswap.
			memcpy(&destination[i], palette + index*4, 4);
		}
		return 1;
	}

	int32 bytesPerPixel = ((format & 0xF00) == Raster::C8888) ? 4 :
		((format & 0xF00) == Raster::C888 ? 3 : 2);
	if(dataSize < (uint32)(pixelCount*bytesPerPixel))
		return 0;
	for(int32 i = 0; i < pixelCount; i++)
		decodeColor(&destination[i], data + i*bytesPerPixel, format);
	return 1;
}

// GX stores a 32 bit texel in twice the memory of a 16 bit one, and Vice
// City's texture set does not fit twice.  RGB5A3 covers opaque and blended
// texels alike, and RGB565 halves it again for the images that never blend,
// which is most of the world.
static int32
chooseRasterFormat(const Image *image)
{
	for(int32 y = 0; y < image->height; y++){
		const uint8 *row = image->pixels + y*image->stride;
		for(int32 x = 0; x < image->width; x++)
			if(row[x*4 + 3] != 255)
				return Raster::C1555;
	}
	return Raster::C565;
}

// Always consumes the level's bytes so a level this build cannot decode leaves
// the stream on the next one instead of derailing the rest of the dictionary.
static uint8*
readLevelData(Stream *stream, uint32 *dataSize)
{
	*dataSize = stream->readU32();
	if(*dataSize == 0)
		return nil;
	uint8 *data = rwNewT(uint8, *dataSize, MEMDUR_FUNCTION | ID_IMAGE);
	if(data == nil){
		GX_NATIVE_TRACE("WII GX native: level allocation failed bytes=%u\n",
		                *dataSize);
		stream->seek(*dataSize);
		return nil;
	}
	if(stream->read8(data, *dataSize) != *dataSize){
		GX_NATIVE_TRACE("WII GX native: truncated level bytes=%u\n", *dataSize);
		rwFree(data);
		return nil;
	}
	return data;
}

static Image*
readLevel(Stream *stream, int32 width, int32 height, const uint8 *palette,
	int32 format, int32 compression)
{
	uint32 dataSize;
	uint8 *data = readLevelData(stream, &dataSize);
	if(data == nil)
		return nil;

	Image *image = Image::create(width, height, 32);
	if(image == nil){
		rwFree(data);
		return nil;
	}
	image->allocate();
	if(image->pixels == nil){
		rwFree(data);
		image->destroy();
		return nil;
	}

	bool32 decoded = decodePixels(image, data, dataSize, palette, format,
		compression);
	rwFree(data);
	if(!decoded){
		GX_NATIVE_TRACE("WII GX native: decode failed size=%dx%d\n", width, height);
		image->destroy();
		return nil;
	}
	return image;
}

static bool32
shouldGenerateNativeMipmaps(int32 width, int32 height, int32 numLevels,
	int32 compression)
{
	if(numLevels != 1 || compression == 1)
		return 0;
	return width >= 64 || height >= 64;
}

static uint32
enableNativeMipmapFilter(uint32 filterAddressing)
{
	uint32 filter = filterAddressing & 0xFF;
	if(filter == Texture::NEAREST)
		filter = Texture::MIPNEAREST;
	else if(filter == Texture::LINEAR)
		filter = Texture::MIPLINEAR;
	else
		return filterAddressing;
	return (filterAddressing & ~0xFF) | filter;
}

static Texture*
makeNativeTexture(Raster *raster, uint32 filterAddressing,
	const char *name, const char *mask)
{
	Texture *texture = Texture::create(raster);
	GX_NATIVE_TRACE("WII GX native: texture create result=%p\n", texture);
	if(texture == nil){
		raster->destroy();
		return nil;
	}
	texture->filterAddressing = filterAddressing;
	memcpy(texture->name, name, sizeof(texture->name));
	memcpy(texture->mask, mask, sizeof(texture->mask));
	return texture;
}

static Texture*
readDxt1Texture(Stream *stream, uint32 filterAddressing,
	const char *name, const char *mask, int32 width, int32 height,
	int32 numLevels, int32 type)
{
	uint32 dataSize;
	uint8 *data = readLevelData(stream, &dataSize);
	int32 gxFormat = data && dxt1TextureHasAlpha(data, dataSize, width, height) ?
		Raster::C1555 : Raster::C565;
	Raster *raster = nil;
	if(data){
		raster = Raster::create(width, height, 32,
			gxFormat | (type == Raster::NORMAL ? Raster::TEXTURE : type));
	}
	bool32 uploaded = raster != nil &&
		rasterSetLevelFromDxt1(raster, data, dataSize, 0);
	GX_NATIVE_TRACE("WII GX native: DXT1 base format=0x%x upload=%d\n",
	                gxFormat, uploaded);
	rwFree(data);

	bool32 mipChainValid = uploaded;
	for(int32 level = 1; level < numLevels; level++){
		int32 levelWidth = width >> level;
		int32 levelHeight = height >> level;
		if(levelWidth < 1)
			levelWidth = 1;
		if(levelHeight < 1)
			levelHeight = 1;
		data = readLevelData(stream, &dataSize);
		if(data == nil){
			mipChainValid = 0;
			continue;
		}
		if(mipChainValid)
			mipChainValid = rasterSetLevelFromDxt1(raster, data,
				dataSize, level);
		GX_NATIVE_TRACE("WII GX native: DXT1 mip level=%d size=%dx%d chain=%d\n",
		                level, levelWidth, levelHeight, mipChainValid);
		rwFree(data);
	}

	if(!uploaded){
		if(raster)
			raster->destroy();
		return nil;
	}
	return makeNativeTexture(raster, filterAddressing, name, mask);
}

Texture*
readNativeTextureD3D8(Stream *stream)
{
	GX_NATIVE_TRACE("WII GX native: find struct begin\n");
	if(!findChunk(stream, ID_STRUCT, nil, nil))
		return nil;
	uint32 platform = stream->readU32();
	GX_NATIVE_TRACE("WII GX native: struct found platform=%u\n", platform);
	if(platform != PLATFORM_D3D8)
		return nil;

	uint32 filterAddressing = stream->readU32();
	char name[32], mask[32];
	stream->read8(name, sizeof(name));
	stream->read8(mask, sizeof(mask));
	uint32 format = stream->readU32();
	stream->readI32();
	int32 width = stream->readU16();
	int32 height = stream->readU16();
	stream->readU8();
	int32 numLevels = stream->readU8();
	int32 type = stream->readU8();
	int32 compression = stream->readU8();
	GX_NATIVE_TRACE("WII GX native: header name=%.32s size=%dx%d format=0x%x levels=%d type=%d compression=%d\n",
	                name, width, height, format, numLevels, type, compression);

	uint8 palette[256*4];
	memset(palette, 0xFF, sizeof(palette));
	if(format & Raster::PAL4)
		stream->read8(palette, 32*4);
	else if(format & Raster::PAL8)
		stream->read8(palette, 256*4);
	GX_NATIVE_TRACE("WII GX native: palette complete\n");
	if(compression == 1)
		return readDxt1Texture(stream, filterAddressing, name, mask,
		                       width, height, numLevels, type);

	GX_NATIVE_TRACE("WII GX native: base level read begin\n");
	Image *image = readLevel(stream, width, height, palette, format, compression);
	if(image == nil){
		GX_NATIVE_TRACE("WII GX native: base level failed\n");
		return nil;
	}

	GX_NATIVE_TRACE("WII GX native: raster create begin\n");
	int32 gxFormat = chooseRasterFormat(image);
	Raster *raster = Raster::create(width, height, 32,
		gxFormat | (type == Raster::NORMAL ? Raster::TEXTURE : type));
	GX_NATIVE_TRACE("WII GX native: raster create result=%p format=0x%x upload begin\n",
	                raster, gxFormat);
	bool32 autoMipmaps = shouldGenerateNativeMipmaps(width, height,
	                                                numLevels, compression);
	bool32 uploaded = raster != nil &&
		(autoMipmaps ? rasterSetFromImageWithMipmaps(raster, image) :
		 raster->setFromImage(image) != nil);
	if(autoMipmaps && uploaded){
		filterAddressing = enableNativeMipmapFilter(filterAddressing);
		GX_NATIVE_TRACE("WII GX native: generated mip chain levels=%d filter=%u\n",
		                raster->getNumLevels(), filterAddressing & 0xFF);
	}
	GX_NATIVE_TRACE("WII GX native: upload result=%d\n", uploaded);
	image->destroy();
	if(!uploaded){
		if(raster)
			raster->destroy();
		return nil;
	}

	// A gap in the chain would leave GX sampling an unwritten level, so the
	// first level that does not make it ends the chain.  The remaining levels
	// are still read, because the next texture in the dictionary starts where
	// this one stops.
	bool32 mipChainValid = 1;
	for(int32 level = 1; level < numLevels; level++){
		int32 levelWidth = width >> level;
		int32 levelHeight = height >> level;
		if(levelWidth < 1)
			levelWidth = 1;
		if(levelHeight < 1)
			levelHeight = 1;
		Image *levelImage = readLevel(stream, levelWidth, levelHeight, palette,
			format, compression);
		if(levelImage == nil){
			mipChainValid = 0;
			continue;
		}
		if(mipChainValid)
			mipChainValid = rasterSetLevelFromImage(raster, levelImage, level);
		GX_NATIVE_TRACE("WII GX native: mip level=%d size=%dx%d chain=%d\n",
		                level, levelWidth, levelHeight, mipChainValid);
		levelImage->destroy();
	}

	return makeNativeTexture(raster, filterAddressing, name, mask);
}

}
}
