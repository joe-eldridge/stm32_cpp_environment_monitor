#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "i2c_device.hpp"

// An in-memory register map standing in for a real I2C sensor. Records every
// write, can be told to fail, and lets a test hook reads/writes to simulate
// device behaviour (status bits that clear over time, reset side effects...).
class FakeI2cDevice final : public I2cDevice
{
public:
  struct Write
  {
    std::uint8_t reg;
    std::vector<std::uint8_t> data;
  };

  bool WriteRegisters(std::uint8_t startReg, const std::uint8_t *data, std::uint8_t count) override
  {
    if (failWrites || !InRange(startReg, count))
    {
      return false;
    }
    writes.push_back({startReg, std::vector<std::uint8_t>(data, data + count)});
    for (std::uint8_t i = 0; i < count; ++i)
    {
      registers[startReg + i] = data[i];
    }
    if (onWrite)
    {
      onWrite(startReg, writes.back().data);
    }
    return true;
  }

  bool ReadRegisters(std::uint8_t startReg, std::uint8_t *buffer, std::uint8_t count) override
  {
    if (failReads || !InRange(startReg, count))
    {
      return false;
    }
    if (onRead)
    {
      onRead(startReg, count);
    }
    for (std::uint8_t i = 0; i < count; ++i)
    {
      buffer[i] = registers[startReg + i];
    }
    return true;
  }

  void SetLe16(std::uint8_t reg, std::uint16_t value)
  {
    registers[reg] = static_cast<std::uint8_t>(value & 0xFF);
    registers[reg + 1] = static_cast<std::uint8_t>(value >> 8);
  }

  std::vector<Write> WritesTo(std::uint8_t reg) const
  {
    std::vector<Write> result;
    for (const auto &w : writes)
    {
      if (w.reg == reg)
      {
        result.push_back(w);
      }
    }
    return result;
  }

  std::array<std::uint8_t, 256> registers{};
  std::vector<Write> writes;
  bool failWrites = false;
  bool failReads = false;
  std::function<void(std::uint8_t reg, const std::vector<std::uint8_t> &data)> onWrite;
  std::function<void(std::uint8_t reg, std::uint8_t count)> onRead;

private:
  static bool InRange(std::uint8_t startReg, std::uint8_t count)
  {
    return count != 0 && startReg + count <= 256;
  }
};
