/* Copyright (C) 1998-2000  Matthes Bender  RedWolf Design */

/* Holds crew member information */

#pragma once

#include <C4Surface.h>
#include <C4InfoCore.h>
#ifdef C4ENGINE
#include <C4Object.h>
#include <C4FacetEx.h>
#endif

class C4ObjectInfo : public C4ObjectInfoCore
{
public:
	C4ObjectInfo();
	~C4ObjectInfo();

public:
	bool WasInAction;
	bool InAction;
	int32_t InActionTime;
	bool HasDied;
	int32_t ControlCount;
	class C4Def *pDef; // definition to ID - only eresolved if defs were loaded at object info loading time
#ifdef C4ENGINE
	C4Portrait Portrait; // portrait link (usually to def graphics)
	C4Portrait *pNewPortrait; // new permanent portrait link (usually to def graphics)
	C4Portrait *pCustomPortrait; // if assigned, the Clonk has a custom portrait to be set via SetPortrait("custom")
#endif
	char Filename[_MAX_PATH + 1];
	C4ObjectInfo *Next;

public:
	void Default();
	void Clear();
	void Evaluate();
	void Retire();
	void Recruit();
	void Draw(C4Facet &cgo, bool fShowPortrait, bool fShowCaptain, C4Object *pOfObj);
	bool Save(C4Group &hGroup, bool fStoreTiny, C4DefList *pDefs);
	bool Load(C4Group &hGroup, bool fLoadPortrait);
	bool Load(C4Group &hMother, const char *szEntryname, bool fLoadPortrait);
#ifdef C4ENGINE
	bool SetRandomPortrait(C4ID idSourceDef, bool fAssignPermanently, bool fCopyFile);
	bool SetPortrait(const char *szPortraitName, C4Def *pSourceDef, bool fAssignPermanently, bool fCopyFile);
	bool SetPortrait(C4PortraitGraphics *pNewPortraitGfx, bool fAssignPermanently, bool fCopyFile);
	bool ClearPortrait(bool fPermanently);
#endif
};
