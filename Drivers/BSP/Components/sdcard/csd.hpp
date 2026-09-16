#pragma once

#include <cstdint>
#include <optional>

namespace sdcard
{

// Card capacity in 512-byte sectors from a raw 16-byte CSD register (as read
// with CMD9). Kept free of any bus access so it can be unit-tested on a host.
// Returns nullopt for CSD layouts this driver doesn't support (SDUC, or a
// block length outside the spec's 512-2048 bytes).
inline std::optional<std::uint32_t> SectorCountFromCsd(const std::uint8_t *csd)
{
  const std::uint8_t csdStructure = csd[0] >> 6;
  if (csdStructure == 1)
  {
    // CSD v2 (SDHC/SDXC): capacity = (C_SIZE + 1) * 512 KiB = (C_SIZE + 1) * 1024 sectors.
    const std::uint32_t cSize = (static_cast<std::uint32_t>(csd[7] & 0x3F) << 16) |
                                (static_cast<std::uint32_t>(csd[8]) << 8) | csd[9];
    const std::uint64_t sectors = (static_cast<std::uint64_t>(cSize) + 1) << 10;
    if (sectors > UINT32_MAX)
    {
      return std::nullopt; // exactly 2 TiB (C_SIZE = 0x3FFFFF) - beyond FatFs's 32-bit sector numbers
    }
    return static_cast<std::uint32_t>(sectors);
  }

  if (csdStructure == 0)
  {
    // CSD v1 (SDSC): capacity = (C_SIZE + 1) * 2^(C_SIZE_MULT + 2) * 2^READ_BL_LEN bytes.
    const std::uint32_t cSize = (static_cast<std::uint32_t>(csd[6] & 0x03) << 10) |
                                (static_cast<std::uint32_t>(csd[7]) << 2) | (csd[8] >> 6);
    const unsigned cSizeMult = (static_cast<unsigned>(csd[9] & 0x03) << 1) | (csd[10] >> 7);
    const unsigned readBlockLengthLog2 = csd[5] & 0x0F;
    if (readBlockLengthLog2 < 9 || readBlockLengthLog2 > 11)
    {
      return std::nullopt;
    }
    // Divide by 512 (2^9) to get sectors.
    return (cSize + 1) << (cSizeMult + 2 + readBlockLengthLog2 - 9);
  }

  return std::nullopt; // SDUC or unknown layout
}

} // namespace sdcard
