//	VirtualDub - Video processing and capture application
//	Copyright (C) 1998-2001 Avery Lee
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

#ifndef f_FILTERS_H
#define f_FILTERS_H

#include <malloc.h>

#include <windows.h>
#include <vfw.h>

#include "List.h"
#include "VBitmap.h"
#include "FilterSystem.h"
#include "filter.h"
#include "error.h"

//////////////////

struct CScriptObject;

//////////////////

#ifndef f_FILTER_GLOBALS
#define EXTERN extern
#define INIT(x)
#else
#define EXTERN
#define INIT(x) =x
#endif

///////////////////

class FilterInstance : public ListNode, public FilterActivation {
public:
	VFBitmap realSrc, realDst, realLast;
	LONG flags;
	HBITMAP hbmDst, hbmLast;
	HGDIOBJ hgoDst, hgoLast;
	void *pvDstView, *pvLastView;
	int srcbuf, dstbuf;
	int origw, origh;
	bool fNoDeinit;

	FilterStateInfo *pfsiDelayRing;
	FilterStateInfo *pfsiDelayInput;
	FilterStateInfo fsiDelay;
	FilterStateInfo fsiDelayOutput;
	int nDelayRingPos;
	int nDelayRingSize;

	///////

	FilterInstance();
	FilterInstance(const FilterInstance& fi);
	FilterInstance(FilterDefinition *);
	~FilterInstance();

	FilterInstance *Clone();
	void Destroy();
	void ForceNoDeinit();
};

class FilterPreview : public IFilterPreview {
private:
	HWND hdlg, hwndButton;
	HWND hwndParent;
	HDRAWDIB hdd;
	FilterSystem filtsys;
	BITMAPINFOHEADER bih;
	FilterStateInfo fsi;
	List *pFilterList;
	FilterInstance *pfiThisFilter;

	FilterPreviewButtonCallback		pButtonCallback;
	void							*pvButtonCBData;
	FilterPreviewSampleCallback		pSampleCallback;
	void							*pvSampleCBData;

	MyError		mFailureReason;

	static BOOL CALLBACK DlgProc(HWND hdlg, UINT message, UINT wParam, LONG lParam);
	long FetchFrame();
	long FetchFrame(long);

public:
	FilterPreview(List *, FilterInstance *);
	~FilterPreview();

	void SetButtonCallback(FilterPreviewButtonCallback, void *);
	void SetSampleCallback(FilterPreviewSampleCallback, void *);

	bool isPreviewEnabled();
	void InitButton(HWND);
	void Toggle(HWND);
	void Display(HWND, bool);
	void RedoFrame();
	void UndoSystem();
	void RedoSystem();
	void Close();
	bool SampleCurrentFrame();
	long SampleFrames();
};

//////////

EXTERN FilterModule		*filter_module_list INIT(0);
EXTERN FilterDefinition *filter_list;
EXTERN List				g_listFA;

EXTERN FilterSystem filters;

int					FilterAutoloadModules(int &fail_count);
void				FilterLoadModule(char *szModule);
void				FilterUnloadModule(FilterModule *fm);
void				FilterUnloadAllModules();
FilterDefinition *	FilterAdd(FilterModule *fm, FilterDefinition *pfd, int fd_len);
void				FilterRemove(FilterDefinition *fd);


LONG FilterGetSingleValue(HWND hWnd, LONG cVal, LONG lMin, LONG lMax, char *title);

#undef EXTERN
#undef INIT

#endif
