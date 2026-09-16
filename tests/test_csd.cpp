#include <gtest/gtest.h>

#include <array>

#include "csd.hpp"

namespace
{

using Csd = std::array<std::uint8_t, 16>;

// Sets bits [msb:lsb] using the SD specification's CSD bit numbering (bit
// 127 is the MSB of the first byte on the wire) - deliberately a different
// route to the fields than the byte arithmetic in the code under test.
void SetField(Csd &csd, int msb, int lsb, std::uint32_t value)
{
  for (int bit = lsb; bit <= msb; ++bit)
  {
    const int byteIndex = 15 - bit / 8;
    const int bitInByte = bit % 8;
    if ((value >> (bit - lsb)) & 1u)
    {
      csd[byteIndex] = static_cast<std::uint8_t>(csd[byteIndex] | (1u << bitInByte));
    }
  }
}

Csd MakeV1(std::uint32_t cSize, std::uint32_t cSizeMult, std::uint32_t readBlockLengthLog2)
{
  Csd csd{};
  csd.fill(0);
  SetField(csd, 127, 126, 0);                   // CSD_STRUCTURE = 1.0
  SetField(csd, 83, 80, readBlockLengthLog2);   // READ_BL_LEN
  SetField(csd, 73, 62, cSize);                 // C_SIZE
  SetField(csd, 49, 47, cSizeMult);             // C_SIZE_MULT
  // Neighbouring fields set, to catch masks that read too wide.
  SetField(csd, 79, 76, 0xF);
  SetField(csd, 61, 50, 0xFFF);
  SetField(csd, 46, 39, 0xFF);
  return csd;
}

Csd MakeV2(std::uint32_t cSize)
{
  Csd csd{};
  csd.fill(0);
  SetField(csd, 127, 126, 1); // CSD_STRUCTURE = 2.0
  SetField(csd, 83, 80, 9);
  SetField(csd, 69, 48, cSize); // C_SIZE
  SetField(csd, 75, 70, 0x3F);  // reserved/neighbouring bits
  SetField(csd, 47, 40, 0xFF);
  return csd;
}

std::uint64_t V1CapacityBytes(std::uint32_t cSize, std::uint32_t cSizeMult, std::uint32_t readBlockLengthLog2)
{
  return (static_cast<std::uint64_t>(cSize) + 1) * (1ull << (cSizeMult + 2)) * (1ull << readBlockLengthLog2);
}

} // namespace

TEST(Csd, V2SectorCount)
{
  // A typical 32 GB SDHC card.
  const std::uint32_t cSize = 60868;
  const auto sectors = sdcard::SectorCountFromCsd(MakeV2(cSize).data());
  ASSERT_TRUE(sectors.has_value());
  EXPECT_EQ(*sectors, (cSize + 1u) * 1024u);
  EXPECT_EQ(static_cast<std::uint64_t>(*sectors) * 512, (cSize + 1ull) * 512 * 1024);
}

TEST(Csd, V2LargestRepresentableCard)
{
  const auto sectors = sdcard::SectorCountFromCsd(MakeV2(0x3FFFFE).data());
  ASSERT_TRUE(sectors.has_value());
  EXPECT_EQ(*sectors, 0x3FFFFFull * 1024);
}

TEST(Csd, V2RejectsCapacityBeyond32BitSectors)
{
  // (0x3FFFFF + 1) * 1024 = 2^32 - would wrap to 0.
  EXPECT_FALSE(sdcard::SectorCountFromCsd(MakeV2(0x3FFFFF).data()).has_value());
}

struct V1Case
{
  std::uint32_t cSize;
  std::uint32_t cSizeMult;
  std::uint32_t readBlockLengthLog2;
};

class CsdV1Test : public ::testing::TestWithParam<V1Case>
{
};

TEST_P(CsdV1Test, SectorCountMatchesCapacity)
{
  const V1Case &c = GetParam();
  const auto sectors = sdcard::SectorCountFromCsd(MakeV1(c.cSize, c.cSizeMult, c.readBlockLengthLog2).data());
  ASSERT_TRUE(sectors.has_value());
  EXPECT_EQ(*sectors, V1CapacityBytes(c.cSize, c.cSizeMult, c.readBlockLengthLog2) / 512);
}

INSTANTIATE_TEST_SUITE_P(SdscCards, CsdV1Test,
                         ::testing::Values(V1Case{3794, 7, 10},  // ~2 GB, 1024-byte blocks
                                           V1Case{3861, 7, 9},   // ~1 GB, 512-byte blocks
                                           V1Case{4095, 7, 11},  // largest v1 layout, 2048-byte blocks
                                           V1Case{0, 0, 9}));    // smallest

TEST(Csd, V1RejectsBlockLengthsOutsideSpec)
{
  EXPECT_FALSE(sdcard::SectorCountFromCsd(MakeV1(100, 3, 8).data()).has_value());
  EXPECT_FALSE(sdcard::SectorCountFromCsd(MakeV1(100, 3, 12).data()).has_value());
}

TEST(Csd, RejectsUnsupportedStructures)
{
  Csd csd = MakeV2(1000);
  csd[0] = static_cast<std::uint8_t>((csd[0] & 0x3F) | 0x80); // CSD_STRUCTURE = 2 (SDUC)
  EXPECT_FALSE(sdcard::SectorCountFromCsd(csd.data()).has_value());
  csd[0] = static_cast<std::uint8_t>(csd[0] | 0xC0); // reserved
  EXPECT_FALSE(sdcard::SectorCountFromCsd(csd.data()).has_value());
}
