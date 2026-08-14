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

uint32
gxTextureLevelSize(int32 width, int32 height, uint32 format)
{
	return GX_GetTexBufferSize(width, height, format, GX_FALSE, 0);
}

uint32
gxTextureSize(int32 width, int32 height, uint32 format, int32 numLevels)
{
	uint32 size = 0;
	for(int32 level = 0; level < numLevels; level++){
		size += gxTextureLevelSize(width, height, format);
		if(width > 1)
			width /= 2;
		if(height > 1)
			height /= 2;
	}
	return size;
}

uint32
gxTextureLevelOffset(int32 width, int32 height, uint32 format, int32 level)
{
	return gxTextureSize(width, height, format, level);
}

static uint16
packRgb565(const uint8 *source)
{
	return uint16(((source[0] & 0xF8) << 8) |
	              ((source[1] & 0xFC) << 3) |
	              (source[2] >> 3));
}

static uint16
packRgb5A3(const uint8 *source)
{
	if(source[3] >= 237)
		return uint16(0x8000 |
		              ((source[0] & 0xF8) << 7) |
		              ((source[1] & 0xF8) << 2) |
		              (source[2] >> 3));
	return uint16(((source[3] & 0xE0) << 7) |
	              ((source[0] & 0xF0) << 4) |
	              (source[1] & 0xF0) |
	              (source[2] >> 4));
}

static void
store16(uint8 *destination, uint16 value)
{
	destination[0] = value >> 8;
	destination[1] = value & 0xFF;
}

static void
convert16(const uint8 *source, int32 sourceStride, int32 width,
	int32 height, uint8 *destination, uint32 format)
{
	int32 blocksPerRow = (width + 3)/4;
	for(int32 y = 0; y < height; y++){
		const uint8 *sourceRow = source + y*sourceStride;
		for(int32 x = 0; x < width; x++){
			uint32 block = (y/4)*blocksPerRow + x/4;
			uint32 offset = block*32 + (y & 3)*8 + (x & 3)*2;
			const uint8 *pixel = sourceRow + x*4;
			uint16 value = format == GX_TF_RGB565 ?
				packRgb565(pixel) : packRgb5A3(pixel);
			store16(destination + offset, value);
		}
	}
}

static void
convertRgba8(const uint8 *source, int32 sourceStride, int32 width,
	int32 height, uint8 *destination)
{
	int32 blocksPerRow = (width + 3)/4;
	for(int32 y = 0; y < height; y++){
		const uint8 *sourceRow = source + y*sourceStride;
		for(int32 x = 0; x < width; x++){
			uint32 block = (y/4)*blocksPerRow + x/4;
			uint32 offset = block*64 + (y & 3)*8 + (x & 3)*2;
			const uint8 *pixel = sourceRow + x*4;
			destination[offset] = pixel[3];
			destination[offset + 1] = pixel[0];
			destination[offset + 32] = pixel[1];
			destination[offset + 33] = pixel[2];
		}
	}
}

void
gxConvertTexture(const uint8 *source, int32 sourceStride, int32 width,
	int32 height, uint8 *destination, uint32 format)
{
	assert(format == GX_TF_RGB565 || format == GX_TF_RGB5A3 ||
	       format == GX_TF_RGBA8);
	if(format == GX_TF_RGBA8)
		convertRgba8(source, sourceStride, width, height, destination);
	else
		convert16(source, sourceStride, width, height, destination, format);
}

uint32
dxt1TextureLevelSize(int32 width, int32 height)
{
	if(width <= 0 || height <= 0)
		return 0;
	return uint32((width + 3)/4)*uint32((height + 3)/4)*8;
}

static uint16
readLittle16(const uint8 *source)
{
	return uint16(source[0] | source[1] << 8);
}

bool32
dxt1TextureHasAlpha(const uint8 *source, uint32 sourceSize,
	int32 width, int32 height)
{
	uint32 requiredSize = dxt1TextureLevelSize(width, height);
	if(source == nil || sourceSize < requiredSize)
		return 0;
	for(uint32 offset = 0; offset < requiredSize; offset += 8){
		const uint8 *block = source + offset;
		if(readLittle16(block) > readLittle16(block + 2))
			continue;
		for(int32 row = 0; row < 4; row++){
			uint8 selectors = block[4 + row];
			if((selectors & (selectors >> 1) & 0x55) != 0)
				return 1;
		}
	}
	return 0;
}

static uint8
reverseDxtSelectors(uint8 selectors)
{
	return uint8(((selectors & 0x03) << 6) |
	             ((selectors & 0x0C) << 2) |
	             ((selectors & 0x30) >> 2) |
	             ((selectors & 0xC0) >> 6));
}

static void
convertDxt1Block(const uint8 *source, uint8 *destination)
{
	// D3D stores the 565 endpoints little-endian and each row's leftmost
	// selector in the low bits. GX uses big-endian endpoints and high bits.
	destination[0] = source[1];
	destination[1] = source[0];
	destination[2] = source[3];
	destination[3] = source[2];
	for(int32 row = 0; row < 4; row++)
		destination[4 + row] = reverseDxtSelectors(source[4 + row]);
}

bool32
gxConvertDxt1ToCmpr(const uint8 *source, uint32 sourceSize,
	int32 width, int32 height, uint8 *destination)
{
	uint32 requiredSize = dxt1TextureLevelSize(width, height);
	if(source == nil || destination == nil || sourceSize < requiredSize)
		return 0;

	int32 sourceBlocksX = (width + 3)/4;
	int32 sourceBlocksY = (height + 3)/4;
	int32 macroBlocksX = (width + 7)/8;
	int32 macroBlocksY = (height + 7)/8;
	for(int32 macroY = 0; macroY < macroBlocksY; macroY++){
		for(int32 macroX = 0; macroX < macroBlocksX; macroX++){
			uint8 *macroDestination = destination +
				(macroY*macroBlocksX + macroX)*32;
			for(int32 subY = 0; subY < 2; subY++){
				int32 sourceY = macroY*2 + subY;
				if(sourceY >= sourceBlocksY)
					sourceY = sourceBlocksY - 1;
				for(int32 subX = 0; subX < 2; subX++){
					int32 sourceX = macroX*2 + subX;
					if(sourceX >= sourceBlocksX)
						sourceX = sourceBlocksX - 1;
					const uint8 *sourceBlock = source +
						(sourceY*sourceBlocksX + sourceX)*8;
					uint8 *destinationBlock = macroDestination +
						(subY*2 + subX)*8;
					convertDxt1Block(sourceBlock, destinationBlock);
				}
			}
		}
	}
	return 1;
}

}
}
