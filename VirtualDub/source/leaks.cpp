//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2003 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "stdafx.h"

#if defined(_MSC_VER) && defined(_DEBUG) && defined(_M_IX86)

#include <crtdbg.h>
#include <dbghelp.h>
#include <vd2/system/filesys.h>

// must match CRT internal format -- using different name to avoid
// symbol conflicts
namespace {
	struct CrtBlockHeader {
		CrtBlockHeader *pNext, *pPrev;
		const char *pFilename;
		int			line;
		size_t		size;
		int			type;
		unsigned	reqnum;
		char		redzone_head[4];
		char		data[1];
	};
}

struct VDDbgHelpDynamicLoaderW32 {
public:
	BOOL (APIENTRY *pSymInitialize)(HANDLE hProcess, PSTR UserSearchPath, BOOL fInvadeProcess);
	BOOL (APIENTRY *pSymCleanup)(HANDLE hProcess);
	BOOL (APIENTRY *pSymSetSearchPath)(HANDLE hProcess, PSTR SearchPath);
	BOOL (APIENTRY *pSymLoadModule)(HANDLE hProcess, HANDLE hFile, PSTR ImageFile, PSTR ModuleName, DWORD BaseOfDll, DWORD SizeOfDll);
	BOOL (APIENTRY *pSymGetSymFromAddr)(HANDLE hProcess, DWORD Address, PDWORD Displacement, PIMAGEHLP_SYMBOL Symbol);
	BOOL (APIENTRY *pSymGetModuleInfo)(HANDLE hProcess, DWORD dwAddr, PIMAGEHLP_MODULE ModuleInfo);
	BOOL (APIENTRY *pUnDecorateSymbolName)(PCSTR DecoratedName, PSTR UnDecoratedName, DWORD UndecoratedLength, DWORD Flags);

	HMODULE hmodDbgHelp;

	VDDbgHelpDynamicLoaderW32();
	~VDDbgHelpDynamicLoaderW32();

	bool ready() const { return hmodDbgHelp != 0; }
};

VDDbgHelpDynamicLoaderW32::VDDbgHelpDynamicLoaderW32()
	: hmodDbgHelp(LoadLibrary("dbghelp"))
{
	static const char *const sFuncTbl[]={
		"SymInitialize",
		"SymCleanup",
		"SymSetSearchPath",
		"SymLoadModule",
		"SymGetSymFromAddr",
		"SymGetModuleInfo",
		"UnDecorateSymbolName",
	};
	enum { kFuncs = sizeof(sFuncTbl)/sizeof(sFuncTbl[0]) };

	if (hmodDbgHelp) {
		int i;
		for(i=0; i<kFuncs; ++i) {
			FARPROC fp = GetProcAddress(hmodDbgHelp, sFuncTbl[i]);

			if (!fp)
				break;

			((FARPROC *)this)[i] = fp;
		}

		if (i >= kFuncs)
			return;

		FreeModule(hmodDbgHelp);
		hmodDbgHelp = 0;
	}

	for(int j=0; j<kFuncs; ++j)
		((FARPROC *)this)[j] = 0;
}

VDDbgHelpDynamicLoaderW32::~VDDbgHelpDynamicLoaderW32() {
	if (hmodDbgHelp) {
		FreeModule(hmodDbgHelp);
		hmodDbgHelp = 0;
	}
}

namespace {
	template<class T>
	class heapvector {
	public:
		typedef	T *					pointer_type;
		typedef	const T *			const_pointer_type;
		typedef T&					reference_type;
		typedef const T&			const_reference_type;
		typedef pointer_type		iterator;
		typedef	const_pointer_type	const_iterator;
		typedef size_t				size_type;
		typedef	ptrdiff_t			difference_type;

		heapvector() : pStart(0), pEnd(0), pEndAlloc(0) {}
		~heapvector() {
			if (pStart)
				HeapFree(GetProcessHeap(), 0, pStart);
		}

		iterator begin() { return pStart; }
		const_iterator begin() const { return pStart; }
		iterator end() { return pEnd; }
		const_iterator end() const { return pEnd; }

		reference_type operator[](size_type i) { return pStart[i]; }
		const_reference_type operator[](size_type i) const { return pStart[i]; }

		size_type size() const { return pEnd-pStart; }
		size_type capacity() const { return pEndAlloc-pStart; }

		void resize(size_type s) {
			if (capacity() < s)
				reserve(std::min<size_type>(size()*2, s));

			pEnd = pStart + s;
		}

		void reserve(size_type s) {
			if (s > capacity()) {
				HANDLE h = GetProcessHeap();
				size_type siz = size();
				T *pNewBlock = (T*)HeapAlloc(h, 0, s * sizeof(T));

				if (pStart) {
					memcpy(pNewBlock, pStart, (char *)pEnd - (char *)pStart);
					HeapFree(h, 0, pStart);
				}

				pStart = pNewBlock;
				pEnd = pStart + siz;
				pEndAlloc = pStart + s;
			}
		}
			
		void push_back(const T& x) {
			if (pEnd == pEndAlloc)
				reserve(pEndAlloc==pStart ? 16 : size()*2);

			*pEnd++ = x;
		}

	protected:
		T *pStart, *pEnd, *pEndAlloc;

		union trivial_check { T x; };
	};

	struct BlockInfo {
		const CrtBlockHeader *pBlock;
		int refcount;
	};

}

void VDDumpMemoryLeaksVC() {
    _CrtMemState msNow;

	// disable CRT tracking of memory blocks
	_CrtSetDbgFlag(_CrtSetDbgFlag(0) & ~_CRTDBG_ALLOC_MEM_DF);

	VDDbgHelpDynamicLoaderW32 dbghelp;

	if (!dbghelp.ready()) {
		_CrtDumpMemoryLeaks();
		return;
	}

	HANDLE hProc = GetCurrentProcess();

	dbghelp.pSymInitialize(hProc, NULL, FALSE);

	char filename[MAX_PATH], path[MAX_PATH];
	GetModuleFileName(NULL, filename, sizeof filename);

	strcpy(path, filename);
	*VDFileSplitPath(path) = 0;

	dbghelp.pSymSetSearchPath(hProc, path);
	SetCurrentDirectory(path);
	DWORD dwAddr = dbghelp.pSymLoadModule(hProc, NULL, filename, NULL, 0, 0);

	IMAGEHLP_MODULE modinfo = {sizeof(IMAGEHLP_MODULE)};

	dbghelp.pSymGetModuleInfo(hProc, dwAddr, &modinfo);

	// checkpoint the current memory layout
    _CrtMemCheckpoint(&msNow);

	_RPT0(0, "\n\nDumping memory leaks:\n\n");

	// traverse memory

	typedef heapvector<BlockInfo> tHeapInfo;
	tHeapInfo heapinfo;

	const CrtBlockHeader *pHdr = (const CrtBlockHeader *)msNow.pBlockHeader;
	for(; pHdr; pHdr = pHdr->pNext) {
		const int type = (pHdr->type & 0xffff);

		if (type != _CLIENT_BLOCK && type != _NORMAL_BLOCK)
			continue;

		BlockInfo info = {
			pHdr,
			0
		};

		heapinfo.push_back(info);
	}

	for(tHeapInfo::iterator it(heapinfo.begin()), itEnd(heapinfo.end()); it!=itEnd; ++it) {
		BlockInfo& blk = *it;
		pHdr = blk.pBlock;

		char buf[1024], *s = buf;

		s += wsprintf(buf, "    #%-5d %p (%8ld bytes)", pHdr->reqnum, pHdr->data, (long)pHdr->size);

		if (pHdr->pFilename && !strcmp(pHdr->pFilename, "stack trace")) {
			void *pRet = (void *)pHdr->line;

			struct {
				IMAGEHLP_SYMBOL hdr;
				CHAR nameext[511];
			} sym;

			sym.hdr.SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
			sym.hdr.MaxNameLength = 512;

			if (dbghelp.pSymGetSymFromAddr(hProc, (DWORD)pRet, 0, &sym.hdr)) {
				s += wsprintf(s, "  Allocator: %p [%s]", pRet, sym.hdr.Name);
			} else
				s += wsprintf(s, "  Allocator: %p", pRet);
		}

		if (pHdr->size >= sizeof(void *)) {
			void *vtbl = *(void **)pHdr->data;

			if (vtbl >= (char *)modinfo.BaseOfImage && vtbl < (char *)modinfo.BaseOfImage + modinfo.ImageSize) {
				struct {
					IMAGEHLP_SYMBOL hdr;
					CHAR nameext[511];
				} sym;

				sym.hdr.SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
				sym.hdr.MaxNameLength = 512;

				char *t;

				if (dbghelp.pSymGetSymFromAddr(hProc, (DWORD)vtbl, 0, &sym.hdr) && (t = strstr(sym.hdr.Name, "::`vftable'"))) {
					*t = 0;
					s += wsprintf(s, " [Type: %s]", sym.hdr.Name);
				}
			}
		}

		*s = 0;

		_RPT1(0, "%s\n", buf);
	}

	dbghelp.pSymCleanup(hProc);
}

#if 0
	struct foo {
		~foo() { VDDumpMemoryLeaksVC(); }
	} bar;
#endif

#pragma data_seg(".CRT$XPB")
extern "C" static void (__cdecl *g_leaktrap)() = VDDumpMemoryLeaksVC;
#pragma data_seg()

#endif
