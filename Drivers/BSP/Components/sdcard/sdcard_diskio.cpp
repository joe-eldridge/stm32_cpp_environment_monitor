#include "sdcard_diskio.h"

#include "main.h"
#include "sdcard.hpp"

namespace
{
// Both in this one translation unit, so they're constructed in this order.
SpiBus g_sdSpi(SPI1, SD_CS_GPIO_Port, SD_CS_Pin);
SdCard g_sdCard(g_sdSpi);
} // namespace

DSTATUS SdCard_DiskInitialize(void)
{
  return g_sdCard.Init() ? 0 : STA_NOINIT;
}

DSTATUS SdCard_DiskStatus(void)
{
  return g_sdCard.IsReady() ? 0 : STA_NOINIT;
}

DRESULT SdCard_DiskRead(BYTE *buff, DWORD sector, UINT count)
{
  for (UINT i = 0; i < count; ++i)
  {
    if (!g_sdCard.ReadSector(sector + i, buff + i * 512))
    {
      return RES_ERROR;
    }
  }
  return RES_OK;
}

DRESULT SdCard_DiskWrite(const BYTE *buff, DWORD sector, UINT count)
{
  for (UINT i = 0; i < count; ++i)
  {
    if (!g_sdCard.WriteSector(sector + i, buff + i * 512))
    {
      return RES_ERROR;
    }
  }
  return RES_OK;
}

void SdCard_Deinit(void)
{
  g_sdCard.Deinit();
}

int SdCard_GetLastInitStage(void)
{
  return static_cast<int>(g_sdCard.GetLastInitStage());
}

DRESULT SdCard_DiskIoctl(BYTE cmd, void *buff)
{
  switch (cmd)
  {
  case CTRL_SYNC:
    return RES_OK;
  case GET_SECTOR_COUNT: {
    const auto sectors = g_sdCard.ReadSectorCount();
    if (!sectors)
    {
      return RES_ERROR;
    }
    *static_cast<DWORD *>(buff) = *sectors;
    return RES_OK;
  }
  case GET_SECTOR_SIZE:
    *static_cast<WORD *>(buff) = 512;
    return RES_OK;
  case GET_BLOCK_SIZE:
    *static_cast<DWORD *>(buff) = 1; // erase block size unknown - report "no info", 1 sector
    return RES_OK;
  default:
    return RES_PARERR;
  }
}
