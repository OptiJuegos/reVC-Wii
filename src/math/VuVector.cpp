#include "common.h"
#include "VuVector.h"

float
CVuVector::MagnitudeSqr(void) const
{
#ifdef GTA_PS2
	float ret;
	__asm__ __volatile__("\n\
		lwc1	$f2,0(%1)\n\
		lwc1	$f3,0(%1)\n\
		lwc1	$f4,0(%1)\n\
		mula.s	$f2,$f2\n\
		madda.s	$f3,$f3\n\
		madd.s	%0,$f4,$f4\n\
	": "=f"(ret) : "r" (this));
	return ret;
#else
	return x*x + y*y + z*z;
#endif
}

void TransformPoint(CVuVector &out, const CMatrix &mat, const CVuVector &in)
{
#ifdef GTA_PS2
	__asm__ __volatile__("\n\
		lqc2    vf01,0x0(%2)\n\
		lqc2    vf02,0x0(%1)\n\
		lqc2    vf03,0x10(%1)\n\
		lqc2    vf04,0x20(%1)\n\
		lqc2    vf05,0x30(%1)\n\
		vmulax.xyz	ACC,   vf02,vf01\n\
		vmadday.xyz	ACC,   vf03,vf01\n\
		vmaddaz.xyz	ACC,   vf04,vf01\n\
		vmaddw.xyz	vf06,vf05,vf00\n\
		sqc2    vf06,0x0(%0)\n\
		": : "r" (&out) , "r" (&mat) ,"r" (&in): "memory");
#else
	out = mat * in;
#endif
}

void TransformPoint(CVuVector &out, const CMatrix &mat, const RwV3d &in)
{
#ifdef GTA_PS2
	__asm__ __volatile__("\n\
		ldr	$8,0x0(%2)\n\
		ldl	$8,0x7(%2)\n\
		lw	$9,0x8(%2)\n\
		pcpyld	$10,$9,$8\n\
		qmtc2	$10,vf01\n\
		lqc2    vf02,0x0(%1)\n\
		lqc2    vf03,0x10(%1)\n\
		lqc2    vf04,0x20(%1)\n\
		lqc2    vf05,0x30(%1)\n\
		vmulax.xyz	ACC,   vf02,vf01\n\
		vmadday.xyz	ACC,   vf03,vf01\n\
		vmaddaz.xyz	ACC,   vf04,vf01\n\
		vmaddw.xyz	vf06,vf05,vf00\n\
		sqc2    vf06,0x0(%0)\n\
		": : "r" (&out) , "r" (&mat) ,"r" (&in)
		 : "$8", "$9", "$10", "memory");
#else
	out = mat * in;
#endif
}

void TransformPoints(CVuVector *out, int n, const CMatrix &mat, const RwV3d *in, int stride)
{
#ifdef GTA_PS2
if(n == 0) return;		// TODO(PS2): this shouldn't happen
	__asm__ __volatile__("\n\
		paddub	$3,%4,$0\n\
		lqc2    vf02,0x0(%2)\n\
		lqc2    vf03,0x10(%2)\n\
		lqc2    vf04,0x20(%2)\n\
		lqc2    vf05,0x30(%2)\n\
		ldr	$8,0x0(%3)\n\
		ldl	$8,0x7(%3)\n\
		lw	$9,0x8(%3)\n\
		pcpyld	$10,$9,$8\n\
		qmtc2	$10,vf01\n\
	1:	vmulax.xyz	ACC,   vf02,vf01\n\
		vmadday.xyz	ACC,   vf03,vf01\n\
		vmaddaz.xyz	ACC,   vf04,vf01\n\
		vmaddw.xyz	vf06,vf05,vf00\n\
		add	%3,%3,$3\n\
		ldr	$8,0x0(%3)\n\
		ldl	$8,0x7(%3)\n\
		lw	$9,0x8(%3)\n\
		pcpyld	$10,$9,$8\n\
		qmtc2	$10,vf01\n\
		addi	%1,%1,-1\n\
		addiu	%0,%0,0x10\n\
		sqc2    vf06,-0x10(%0)\n\
		bnez	%1,1b\n\
		": : "r" (out) , "r" (n), "r" (&mat), "r" (in), "r" (stride)
		 : "$3", "$8", "$9", "$10", "memory");
#else
	while(n--){
		*out = mat * *in;
		in = (RwV3d*)((uint8*)in + stride);
		out++;
	}
#endif
}

void TransformPoints(CVuVector *out, int n, const CMatrix &mat, const CVuVector *in)
{
#ifdef GTA_PS2
	__asm__ __volatile__("\n\
		lqc2    vf02,0x0(%2)\n\
		lqc2    vf03,0x10(%2)\n\
		lqc2    vf04,0x20(%2)\n\
		lqc2    vf05,0x30(%2)\n\
		lqc2    vf01,0x0(%3)\n\
		nop\n\
	1:	vmulax.xyz	ACC,   vf02,vf01\n\
		vmadday.xyz	ACC,   vf03,vf01\n\
		vmaddaz.xyz	ACC,   vf04,vf01\n\
		vmaddw.xyz	vf06,vf05,vf00\n\
		lqc2	vf01,0x10(%3)\n\
		addiu	%3,%3,0x10\n\
		addi	%1,%1,-1\n\
		addiu	%0,%0,0x10\n\
		sqc2    vf06,-0x10(%0)\n\
		bnez	%1,1b\n\
		": : "r" (out) , "r" (n), "r" (&mat) ,"r" (in): "memory");
#else
	while(n--){
		*out = mat * *in;
		in++;
		out++;
	}
#endif
}
