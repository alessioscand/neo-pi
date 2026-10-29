/*
 * neo-pi - Geolith file I/O on the SD card (FatFs)
 */
#include <fatfs/ff.h>
#include <circle/types.h>

extern "C" {
#include <stdint.h>
#include "geo_vfs.h"
}

static void *VfsOpen (const char *pPath, unsigned nMode)
{
	FIL *pFile = new FIL;
	if (pFile == 0)
	{
		return 0;
	}
	BYTE nFlags = nMode == GEO_VFS_WRITE ? (FA_WRITE | FA_CREATE_ALWAYS) : FA_READ;
	if (f_open (pFile, pPath, nFlags) != FR_OK)
	{
		delete pFile;
		return 0;
	}
	return pFile;
}

static int VfsClose (void *pHandle)
{
	FIL *pFile = (FIL *) pHandle;
	FRESULT r = f_close (pFile);
	delete pFile;
	return r == FR_OK ? 0 : -1;
}

static int64_t VfsRead (void *pHandle, void *pData, int64_t nLen)
{
	UINT nRead = 0;
	if (f_read ((FIL *) pHandle, pData, (UINT) nLen, &nRead) != FR_OK)
	{
		return -1;
	}
	return nRead;
}

static int64_t VfsWrite (void *pHandle, const void *pData, int64_t nLen)
{
	UINT nWritten = 0;
	if (f_write ((FIL *) pHandle, pData, (UINT) nLen, &nWritten) != FR_OK)
	{
		return -1;
	}
	return nWritten;
}

static int64_t VfsSeek (void *pHandle, int64_t nOffset, int nOrigin)
{
	FIL *pFile = (FIL *) pHandle;
	FSIZE_t nBase = nOrigin == GEO_VFS_SEEK_CUR ? f_tell (pFile)
		      : nOrigin == GEO_VFS_SEEK_END ? f_size (pFile) : 0;
	if (f_lseek (pFile, nBase + nOffset) != FR_OK)
	{
		return -1;
	}
	return f_tell (pFile);
}

static int64_t VfsTell (void *pHandle)
{
	return f_tell ((FIL *) pHandle);
}

static int64_t VfsSize (void *pHandle)
{
	return f_size ((FIL *) pHandle);
}

static const geo_vfs_t s_VFS =
{
	VfsOpen, VfsClose, VfsRead, VfsWrite, VfsSeek, VfsTell, VfsSize, 0
};

void NeopiInstallVFS (void)
{
	geo_vfs_set_callbacks (&s_VFS);
}
