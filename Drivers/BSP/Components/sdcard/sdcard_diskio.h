#pragma once

// C bridge between FatFs's generated user_diskio.c (plain C, CubeMX-owned)
// and the C++ SdCard driver. Function names/signatures mirror the USER_*
// stubs user_diskio.c already declares, minus the "USER_" prefix, so the
// mapping at each call site is obvious.

#include "diskio.h"

#ifdef __cplusplus
extern "C"
{
#endif

  DSTATUS SdCard_DiskInitialize(void);
  DSTATUS SdCard_DiskStatus(void);
  DRESULT SdCard_DiskRead(BYTE *buff, DWORD sector, UINT count);
  DRESULT SdCard_DiskWrite(const BYTE *buff, DWORD sector, UINT count);
  DRESULT SdCard_DiskIoctl(BYTE cmd, void *buff);

  // For ejecting the card: call after f_unmount(). See SdCard::Deinit().
  void SdCard_Deinit(void);

  // How the last card initialisation went, for the boot log: which step it
  // reached, named, and how long it took. See SdCard::InitReport.
  typedef struct
  {
    const char *stage;
    unsigned long durationMs;
  } SdCardInitReport;

  SdCardInitReport SdCard_GetInitReport(void);

#ifdef __cplusplus
}
#endif
