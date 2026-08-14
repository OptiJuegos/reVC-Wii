#ifndef __GTA_SAVEBUF_H__
#define __GTA_SAVEBUF_H__

#ifdef VALIDATE_SAVE_SIZE
extern int32 _saveBufCount;
#define INITSAVEBUF _saveBufCount = 0;
#define VALIDATESAVEBUF(b) assert(_saveBufCount == b);
#else
#define INITSAVEBUF
#define VALIDATESAVEBUF(b)
#endif

inline void
SkipSaveBuf(uint8 *&buf, int32 skip)
{
	buf += skip;
#ifdef VALIDATE_SAVE_SIZE
	_saveBufCount += skip;
#endif
}

inline void
SkipSaveBuf(uint8*& buf, uint32 &length, int32 skip)
{
	buf += skip;
	length += skip;
#ifdef VALIDATE_SAVE_SIZE
	_saveBufCount += skip;
#endif
}

// The save buffer is an unpadded byte stream, so a T lands at whatever offset
// the fields before it happen to leave -- and under COMPATIBLE_SAVES those
// include single bytes, so a float routinely starts at an odd address.  Casting
// the cursor to T* and assigning through it is therefore an unaligned access.
//
// x86 does not care.  PowerPC fixes up unaligned INTEGER accesses in hardware
// but traps on floating point ones, which is the alignment exception the Wii
// build takes inside CVehicle::Save.  memcpy carries no alignment requirement at
// all, and compilers fold a known, small size back into a plain load or store
// wherever they can prove one is safe, so the platforms that never had the
// problem pay nothing for this.
template<typename T>
inline void
ReadSaveBuf(T *out, uint8 *&buf)
{
	memcpy(out, buf, sizeof(T));
	SkipSaveBuf(buf, sizeof(T));
}

template<typename T>
inline void
ReadSaveBuf(T *out, uint8 *&buf, uint32 &length)
{
	memcpy(out, buf, sizeof(T));
	SkipSaveBuf(buf, length, sizeof(T));
}

// The returned pointer is only ever used by the non-COMPATIBLE_SAVES paths, and
// only to patch the struct that was just written; it inherits whatever alignment
// the cursor had, exactly as before.
template<typename T>
inline T *
WriteSaveBuf(uint8 *&buf, const T &value)
{
	T *p = (T*)buf;
	memcpy(buf, &value, sizeof(T));
	SkipSaveBuf(buf, sizeof(T));
	return p;
}

template<typename T>
inline T *
WriteSaveBuf(uint8 *&buf, uint32 &length, const T &value)
{
	T *p = (T*)buf;
	memcpy(buf, &value, sizeof(T));
	SkipSaveBuf(buf, length, sizeof(T));
	return p;
}

#ifdef COMPATIBLE_SAVES
inline void
ZeroSaveBuf(uint8 *&buf, uint32 length)
{
	memset(buf, 0, length);
	SkipSaveBuf(buf, length);
}
#endif

#define SAVE_HEADER_SIZE (4*sizeof(char)+sizeof(uint32))

#define WriteSaveHeader(buf,a,b,c,d,size) \
	WriteSaveBuf(buf, a);\
	WriteSaveBuf(buf, b);\
	WriteSaveBuf(buf, c);\
	WriteSaveBuf(buf, d);\
	WriteSaveBuf<uint32>(buf, size);

#define WriteSaveHeaderWithLength(buf,len,a,b,c,d,size) \
	WriteSaveBuf(buf, len, a);\
	WriteSaveBuf(buf, len, b);\
	WriteSaveBuf(buf, len, c);\
	WriteSaveBuf(buf, len, d);\
	WriteSaveBuf(buf, len, (uint32)(size));

#ifdef VALIDATE_SAVE_SIZE
#define CheckSaveHeader(buf, a, b, c, d, size) do { \
	char _c; uint32 _size;\
	ReadSaveBuf(&_c, buf);\
	assert(_c == a);\
	ReadSaveBuf(&_c, buf);\
	assert(_c == b);\
	ReadSaveBuf(&_c, buf);\
	assert(_c == c);\
	ReadSaveBuf(&_c, buf);\
	assert(_c == d);\
	ReadSaveBuf(&_size, buf);\
	assert(_size == size);\
	} while(0)

#define CheckSaveHeaderWithLength(buf,len,a,b,c,d,size) do { \
	char _c; uint32 _size;\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == a);\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == b);\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == c);\
	ReadSaveBuf(&_c, buf, len);\
	assert(_c == d);\
	ReadSaveBuf(&_size, buf, len);\
	assert(_size == size);\
	} while(0)
#else
#define CheckSaveHeader(buf, a, b, c, d, size) SkipSaveBuf(buf, 8);
#define CheckSaveHeaderWithLength(buf, len, a, b, c, d, size) SkipSaveBuf(buf, 8);
#endif

#endif // __GTA_SAVEBUF_H__
