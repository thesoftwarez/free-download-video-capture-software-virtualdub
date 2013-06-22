#include <vector>
#include <vd2/system/memory.h>
#include <vd2/system/cpuaccel.h>
#include <vd2/system/vdstl.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/pixmapops.h>

using namespace nsVDPixmap;

namespace {
	typedef void (*tpPalettedBlitter)(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, vdpixsize w, vdpixsize h, const void *pal);
	typedef void (*tpChunkyBlitter)(void *dst, ptrdiff_t dstpitch, const void *src, ptrdiff_t srcpitch, vdpixsize w, vdpixsize h);
	typedef void (*tpPlanarBlitter)(const VDPixmap& dst, const VDPixmap& src);
}

bool VDPixmapBltDirect(const VDPixmap& dst, const VDPixmap& src, vdpixsize w, vdpixsize h);

void VDPixmapBltDirectPalettedConversion(const VDPixmap& dst, const VDPixmap& src, vdpixsize w, vdpixsize h, tpPalettedBlitter pBlitter) {
	uint8 palbytes[256 * 3];

	int palsize;

	switch(src.format) {
	case kPixFormat_Pal1:
		palsize = 2;
		break;
	case kPixFormat_Pal2:
		palsize = 4;
		break;
	case kPixFormat_Pal4:
		palsize = 16;
		break;
	case kPixFormat_Pal8:
		palsize = 256;
		break;
	default:
		VDNEVERHERE;
	}

	VDPixmap srcpal = { (void *)src.palette, NULL, palsize, 1, 0, kPixFormat_XRGB8888 };
	VDPixmap dstpal = { palbytes, NULL, palsize, 1, 0, dst.format };

	VDPixmapBltDirect(dstpal, srcpal, palsize, 1);

	pBlitter(dst.data, dst.pitch, src.data, src.pitch, w, h, palbytes);
}

tpVDPixBltTable VDPixmapGetBlitterTable() {
#if defined(_WIN32) && defined(_M_IX86)
	static tpVDPixBltTable pBltTable;
	
	if (CPUGetEnabledExtensions() & CPUF_SUPPORTS_MMX) {
		return VDGetPixBltTableX86MMX();
	} else {
		return VDGetPixBltTableX86Scalar();
	}
#else
	static tpVDPixBltTable pBltTable = VDGetPixBltTableReference();
	return pBltTable;
#endif
}

bool VDPixmapBltDirect(const VDPixmap& dst, const VDPixmap& src, vdpixsize w, vdpixsize h) {
	if ((unsigned)src.format >= kPixFormat_Max_Standard) {
		VDASSERT(false);
		return false;
	}

	if ((unsigned)dst.format >= kPixFormat_Max_Standard) {
		VDASSERT(false);
		return false;
	}

	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(src.format);

	if (src.format == dst.format) {
		const int qw = -(-w >> srcinfo.qwbits);
		const int qh = -(-h >> srcinfo.qhbits);
		const int auxw = -(-w >> srcinfo.auxwbits);
		const int auxh = -(-h >> srcinfo.auxhbits);

		switch(srcinfo.auxbufs) {
		case 2:
			VDMemcpyRect(dst.data3, dst.pitch3, src.data3, src.pitch3, auxw, auxh);
		case 1:
			VDMemcpyRect(dst.data2, dst.pitch2, src.data2, src.pitch2, auxw, auxh);
		case 0:
			VDMemcpyRect(dst.data, dst.pitch, src.data, src.pitch, srcinfo.qsize * qw, qh);
		}

		return true;
	}

	void *pBlitter = VDPixmapGetBlitterTable()[src.format][dst.format];

	if (!pBlitter)
		return false;

	if (srcinfo.auxbufs > 0 || VDPixmapGetInfo(dst.format).auxbufs > 0) {
		tpPlanarBlitter p = (tpPlanarBlitter)pBlitter;

		p(dst, src);
	} else if (src.format == kPixFormat_Pal1 || src.format == kPixFormat_Pal2 || src.format == kPixFormat_Pal4 || src.format == kPixFormat_Pal8) {
		tpPalettedBlitter p = (tpPalettedBlitter)pBlitter;

		if (dst.format == kPixFormat_XRGB8888)
			p(dst.data, dst.pitch, src.data, src.pitch, w, h, src.palette);
		else
			VDPixmapBltDirectPalettedConversion(dst, src, w, h, p);
	} else {
		tpChunkyBlitter p = (tpChunkyBlitter)pBlitter;

		p(dst.data, dst.pitch, src.data, src.pitch, w, h);
	}

	return true;
}

bool VDPixmapIsBltPossible(int dst_format, int src_format) {
	if (src_format == dst_format)
		return true;

	tpVDPixBltTable tab(VDPixmapGetBlitterTable());

	return tab[src_format][dst_format]
		||(tab[src_format][kPixFormat_YUV444_XVYU] && tab[kPixFormat_YUV444_XVYU][dst_format])
		||(tab[src_format][kPixFormat_XRGB8888] && tab[kPixFormat_XRGB8888][dst_format]);
}

bool VDPixmapBltFast(const VDPixmap& dst, const VDPixmap& src, vdpixsize w, vdpixsize h) {
	if (VDPixmapBltDirect(dst, src, w, h))
		return true;

	// Oro... let's see if we can do a two-stage conversion.
	const VDPixmapFormatInfo& srcinfo = VDPixmapGetInfo(src.format);
	const VDPixmapFormatInfo& dstinfo = VDPixmapGetInfo(dst.format);

	if (srcinfo.auxbufs > 0 || dstinfo.auxbufs > 0)
		return false;		// fail, planar buffers involved

	if (srcinfo.qh > 1)
		return false;		// fail, vertically packed formats involved

	// Allocate a 4xW buffer and try round-tripping through either
	// RGB32 or XYVU.
	vdblock<uint32>		tempBuf(w + 1);

	tpVDPixBltTable tab(VDPixmapGetBlitterTable());

	void *dstp = dst.data;
	const void *srcp = src.data;

	tpChunkyBlitter pb1 = (tpChunkyBlitter)tab[src.format][kPixFormat_YUV444_XVYU];
	tpChunkyBlitter pb2 = (tpChunkyBlitter)tab[kPixFormat_YUV444_XVYU][dst.format];
	if (!pb1 || !pb2) {
		pb1 = (tpChunkyBlitter)tab[src.format][kPixFormat_XRGB8888];
		pb2 = (tpChunkyBlitter)tab[kPixFormat_XRGB8888][dst.format];
		if (!pb1 || !pb2)
			return false;
	}

	do {
		pb1(&tempBuf[0], 0, srcp, 0, w, 1);
		pb2(dstp, 0, &tempBuf[0], 0, w, 1);
		vdptrstep(srcp, src.pitch);
		vdptrstep(dstp, dst.pitch);
	} while(--h);
	return true;
}

bool VDPixmapBlt(const VDPixmap& dst, const VDPixmap& src) {
	vdpixsize w = std::min<vdpixsize>(src.w, dst.w);
	vdpixsize h = std::min<vdpixsize>(src.h, dst.h);

	if (!w || !h)
		return true;

	return VDPixmapBltFast(dst, src, w, h);
}

bool VDPixmapBlt(const VDPixmap& dst, vdpixpos x1, vdpixpos y1, const VDPixmap& src, vdpixpos x2, vdpixpos y2, vdpixsize w, vdpixsize h) {
	if (x1 < 0) {
		x2 -= x1;
		w -= x1;
		x1 = 0;
	}

	if (y1 < 0) {
		y2 -= y1;
		h -= y1;
		y1 = 0;
	}

	if (x2 < 0) {
		x1 -= x2;
		w -= x2;
		x2 = 0;
	}

	if (y2 < 0) {
		y1 -= y2;
		h -= y2;
		y2 = 0;
	}

	if (w > dst.w - x1)
		w = dst.w - x1;

	if (h > dst.h - y1)
		h = dst.h - y1;

	if (w > src.w - x2)
		w = src.w - x2;

	if (h > src.h - y2)
		h = src.h - y2;

	if (w>=0 && h >= 0) {
		VDPixmap dst2(VDPixmapOffset(dst, x1, y1));
		VDPixmap src2(VDPixmapOffset(src, x2, y2));

		return VDPixmapBltFast(dst2, src2, w, h);
	}

	return true;
}

extern bool VDPixmapStretchBltNearest_reference(const VDPixmap& dst, sint32 x1, sint32 y1, sint32 x2, sint32 y2, const VDPixmap& src, sint32 u1, sint32 v1, sint32 u2, sint32 v2);
extern bool VDPixmapStretchBltBilinear_reference(const VDPixmap& dst, sint32 x1, sint32 y1, sint32 x2, sint32 y2, const VDPixmap& src, sint32 u1, sint32 v1, sint32 u2, sint32 v2);

bool VDPixmapStretchBltNearest(const VDPixmap& dst, const VDPixmap& src) {
	return VDPixmapStretchBltNearest(dst, 0, 0, dst.w<<16, dst.h<<16, src, 0, 0, src.w<<16, src.h<<16);
}

bool VDPixmapStretchBltNearest(const VDPixmap& dst, sint32 x1, sint32 y1, sint32 x2, sint32 y2, const VDPixmap& src, sint32 u1, sint32 v1, sint32 u2, sint32 v2) {
	return VDPixmapStretchBltNearest_reference(dst, x1, y1, x2, y2, src, u1, v1, u2, v2);
}

bool VDPixmapStretchBltBilinear(const VDPixmap& dst, const VDPixmap& src) {
	return VDPixmapStretchBltBilinear(dst, 0, 0, dst.w<<16, dst.h<<16, src, 0, 0, src.w<<16, src.h<<16);
}

bool VDPixmapStretchBltBilinear(const VDPixmap& dst, sint32 x1, sint32 y1, sint32 x2, sint32 y2, const VDPixmap& src, sint32 u1, sint32 v1, sint32 u2, sint32 v2) {
	return VDPixmapStretchBltBilinear_reference(dst, x1, y1, x2, y2, src, u1, v1, u2, v2);
}
