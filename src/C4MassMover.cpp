/*
 * LegacyClonk
 *
 * Copyright (c) 1998-2000, Matthes Bender (RedWolf Design)
 * Copyright (c) 2017-2019, The LegacyClonk Team and contributors
 *
 * Distributed under the terms of the ISC license; see accompanying file
 * "COPYING" for details.
 *
 * "Clonk" is a registered trademark of Matthes Bender, used with permission.
 * See accompanying file "TRADEMARK" for details.
 *
 * To redistribute this file separately, substitute the full license texts
 * for the above references.
 */

/* Move liquids in the landscape using individual transport spots */

#include <C4Include.h>
#include <C4MassMover.h>

#include <C4Random.h>
#include <C4Material.h>
#include <C4Game.h>
#include <C4Wrappers.h>

// Note: creation optimized using advancing CreatePtr, so sequential
// creation does not keep rescanning the complete set for a free
// slot. (This had caused extreme delays.) This had the effect that
// MMs created by another MM being executed were oftenly executed
// within the same frame repetitiously leading to long distance mass
// movement in no-time. To avoid this, set execution is done in
// opposite direction. We now have have smoothly running masses in
// a mathematical triangular shape with no delays! Since masses are
// running slower and smoother, overall MM counts are much lower,
// hardly ever exceeding 1000. October 1997

C4MassMoverSet::C4MassMoverSet()
{
	Default();
}

C4MassMoverSet::~C4MassMoverSet()
{
	Clear();
}

void C4MassMoverSet::Clear() {}

void C4MassMoverSet::Execute()
{
	C4MassMover *cmm;
	// Init counts
	Count = 0;
	// Execute & count
	for (int32_t speed = 2; speed > 0; speed--)
	{
		cmm = &(Set[C4MassMoverChunk - 1]);
		for (int32_t cnt = 0; cnt < C4MassMoverChunk; cnt++, cmm--)
			if (cmm->Mat != MNone)
			{
				Count++; cmm->Execute();
			}
	}
}

bool C4MassMoverSet::Create(int32_t x, int32_t y, bool fExecute)
{
	if (Count == C4MassMoverChunk) return false;
#ifdef DEBUGREC
	C4RCMassMover rc;
	rc.x = x; rc.y = y;
	AddDbgRec(RCT_MMC, &rc, sizeof(rc));
#endif
	int32_t cptr = CreatePtr;
	do
	{
		cptr++;
		if (cptr >= C4MassMoverChunk) cptr = 0;
		if (Set[cptr].Mat == MNone)
		{
			if (!Set[cptr].Init(x, y)) return false;
			CreatePtr = cptr;
			if (fExecute) Set[cptr].Execute();
			return true;
		}
	} while (cptr != CreatePtr);
	return false;
}

bool C4MassMover::Init(int32_t tx, int32_t ty)
{
	// Out of bounds check
	if (!Inside<int32_t>(tx, 0, GBackWdt - 1) || !Inside<int32_t>(ty, 0, GBackHgt - 1))
		return false;
	// Check mat
	Mat = GBackMat(tx, ty);
	x = tx; y = ty;
	Game.MassMover.Count++;
	return (Mat != MNone);
}

void C4MassMover::Cease()
{
#ifdef DEBUGREC
	C4RCMassMover rc;
	rc.x = x; rc.y = y;
	AddDbgRec(RCT_MMD, &rc, sizeof(rc));
#endif
	Game.MassMover.Count--;
	Mat = MNone;
}

bool C4MassMover::Execute()
{
	int32_t tx, ty;

	// Lost target material
	if (GBackMat(x, y) != Mat) { Cease(); return false; }

	// Check for transfer target space
	C4Material *pMat = Game.Material.Map + Mat;
	tx = x; ty = y;
	if (!Game.Landscape.FindMatPath(tx, ty, +1, pMat->Density, pMat->MaxSlide))
	{
		// Contact material reaction check: corrosion/evaporation/inflammation/etc.
		if (Corrosion(+0, +1) || Corrosion(-1, +0) || Corrosion(+1, +0))
		{
			// material has been used up
			Game.Landscape.ExtractMaterial(x, y);
			return true;
		}

		// No space, die
		Cease(); return false;
	}

	// Save back material that is about to be overwritten.
	int omat;
	if (Game.C4S.Game.Realism.LandscapeInsertThrust)
		omat = GBackMat(tx, ty);

	// Transfer mass
	if (Random(10))
		SBackPix(tx, ty, Mat2PixColDefault(Game.Landscape.ExtractMaterial(x, y)) + GBackIFT(tx, ty));
	else
		Game.Landscape.InsertMaterial(Game.Landscape.ExtractMaterial(x, y), tx, ty, 0, 1);

	// Reinsert material (thrusted aside)
	if (Game.C4S.Game.Realism.LandscapeInsertThrust && MatValid(omat) && Game.Material.Map[omat].Density > 0)
		Game.Landscape.InsertMaterial(omat, tx, ty + 1);

	// Create new mover at target
	Game.MassMover.Create(tx, ty, !Rnd3());

	return true;
}

bool C4MassMover::Corrosion(int32_t dx, int32_t dy)
{
	// check reaction map of massmover-mat to target mat
	int32_t tmat = GBackMat(x + dx, y + dy);
	C4MaterialReaction *pReact = Game.Material.GetReactionUnsafe(Mat, tmat);
	if (pReact)
	{
		C4Fixed xdir = Fix0, ydir = Fix0;
		if ((*pReact->pFunc)(pReact, x, y, x + dx, y + dy, xdir, ydir, Mat, tmat, meeMassMove, nullptr))
			return true;
	}
	return false;
}

void C4MassMoverSet::Default()
{
	int32_t cnt;
	for (cnt = 0; cnt < C4MassMoverChunk; cnt++) Set[cnt].Mat = MNone;
	Count = 0;
	CreatePtr = 0;
}

bool C4MassMoverSet::Save(C4Group &hGroup)
{
	int32_t cnt;
	// Consolidate
	Consolidate();
	// Recount
	Count = 0;
	for (cnt = 0; cnt < C4MassMoverChunk; cnt++)
		if (Set[cnt].Mat != MNone)
			Count++;
	// All empty: delete component
	if (!Count)
	{
		hGroup.Delete(C4CFN_MassMover);
		return true;
	}
	// Save set
	if (!hGroup.Add(C4CFN_MassMover, Set, Count * sizeof(C4MassMover)))
		return false;
	// Success
	return true;
}

bool C4MassMoverSet::Load(C4Group &hGroup)
{
	// clear previous
	Clear(); Default();

	size_t iBinSize, iMoverSize = sizeof(C4MassMover);
	if (!hGroup.AccessEntry(C4CFN_MassMover, &iBinSize)) return false;
	if ((iBinSize % iMoverSize) != 0) return false;

	// load new
	Count = iBinSize / iMoverSize;
	if (!hGroup.Read(Set, iBinSize)) return false;
	return true;
}

void C4MassMoverSet::Consolidate()
{
	// Consolidate set
	int32_t iSpot, iPtr, iConsolidated;
	for (iSpot = -1, iPtr = 0, iConsolidated = 0; iPtr < C4MassMoverChunk; iPtr++)
	{
		// Empty: set new spot if needed
		if (Set[iPtr].Mat == MNone)
		{
			if (iSpot == -1) iSpot = iPtr;
		}
		// Full: move down to empty spot if possible
		else if (iSpot != -1)
		{
			// Move to spot
			Set[iSpot] = Set[iPtr];
			Set[iPtr].Mat = MNone;
			iConsolidated++;
			// Advance empty spot (as far as ptr)
			for (; iSpot < iPtr; iSpot++)
				if (Set[iSpot].Mat == MNone)
					break;
			// No empty spot below ptr
			if (iSpot == iPtr) iSpot = -1;
		}
	}
	// Reset create ptr
	CreatePtr = 0;
}

void C4MassMoverSet::Synchronize()
{
	Consolidate();
}

void C4MassMoverSet::Copy(C4MassMoverSet &rSet)
{
	Clear();
	Count = rSet.Count;
	CreatePtr = rSet.CreatePtr;
	for (int32_t cnt = 0; cnt < C4MassMoverChunk; cnt++) Set[cnt] = rSet.Set[cnt];
}
