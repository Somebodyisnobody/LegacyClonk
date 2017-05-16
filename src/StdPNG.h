/*
 * LegacyClonk
 *
 * Copyright (c) RedWolf Design
 * Copyright (c) 2001, Sven2
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

// png file reading functionality

#pragma once

#include <Standard.h>
#include <png.h>
#include <zlib.h>

void PNGAPI CPNGReadFn(png_structp png_ptr, png_bytep data, size_t length); // reading proc (callback)

class CPNGFile
{
private:
	uint8_t *pFile; // loaded file in mem
	bool fpFileOwned; // whether file ptr was allocated by this class
	int iFileSize; // size of file in mem
	int iPixSize; // size of one pixel in image data mem
	FILE *fp; // opened file for writing

	uint8_t *pFilePtr; // current pos in file

	bool fWriteMode; // if set, the following png-structs are write structs
	png_structp png_ptr; // png main struct
	png_infop info_ptr, end_info; // png file info

	uint8_t *pImageData; // uncompressed image in memory
	int iRowSize; // size of one row of data (equals pitch)

	void Read(uint8_t *pData, int iLength); // read from file
	bool DoLoad(); // perform png-file loading after file data ptr has been set

public:
	unsigned long iWdt, iHgt; // image size
	int iBPC, iClrType, iIntrlcType, iCmprType, iFltrType; // image data info

public:
	CPNGFile();
	~CPNGFile();

	void ClearPngStructs(); // clear internal png structs (png_tr, info_ptr etc.);
	void Default(); // zero fields
	void Clear(); // clear loaded file
	bool Load(uint8_t *pFile, int iSize); // load from file that is completely in mem
	uint32_t GetPix(int iX, int iY); // get pixel value (rgba) - note that NO BOUNDS CHECKS ARE DONE due to performance reasons!

	// Use ONLY for PNG_COLOR_TYPE_RGB_ALPHA!
	const uint32_t *GetRow(int iY)
	{
		return reinterpret_cast<uint32_t *>(pImageData + iY * iRowSize);
	}

	bool Create(int iWdt, int iHgt, bool fAlpha); // create empty image
	bool SetPix(int iX, int iY, uint32_t dwValue); // set pixel value
	bool Save(const char *szFilename); // save current image to file; saving to mem is not supported because C4Group doesn't support streamed writing anyway...

	uint8_t *GetImageData() { return pImageData; } // return raw image data

	friend void PNGAPI CPNGReadFn(png_structp png_ptr, png_bytep data, size_t length);
};
