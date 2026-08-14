#ifndef __GTA_MATHS_H__
#define __GTA_MATHS_H__

// wrapper around float versions of functions
// in gta they are in CMaths but that makes the code rather noisy

inline float Sin(float x) { return sinf(x); }
inline float Asin(float x) { return asinf(x); }
inline float Cos(float x) { return cosf(x); }
inline float Acos(float x) { return acosf(x); }
inline float Tan(float x) { return tanf(x); }
inline float Atan(float x) { return atanf(x); }
inline float Atan2(float y, float x) { return atan2f(y, x); }
inline float Abs(float x) { return fabsf(x); }
inline float Sqrt(float x) { return sqrtf(x); }
#ifdef GTA_PS2
inline float RecipSqrt(float x, float y)
{
	float ret;
	__asm__ __volatile__("\n\
		rsqrt.s	%0,%1,%2\n\
		" : "=f" (ret)
		  : "f" (x), "f" (y)
	);
	return ret;
}
#else
inline float RecipSqrt(float x, float y) { return x/Sqrt(y); }
#endif
inline float RecipSqrt(float x) { return RecipSqrt(1.0f, x); }
inline float Pow(float x, float y) { return powf(x, y); }
inline float Floor(float x) { return floorf(x); }
inline float Ceil(float x) { return ceilf(x); }

#endif // __GTA_MATHS_H__
