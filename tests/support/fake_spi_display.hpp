#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "gpio_pin.hpp"
#include "spi_device.hpp"

class FakeOutputPin final : public OutputPin
{
public:
  void Write(bool high) override
  {
    level = high;
    history.push_back(high);
  }

  bool level = false;
  std::vector<bool> history;
};

class FakeInputPin final : public InputPin
{
public:
  bool IsHigh() override
  {
    ++reads;
    return isHigh ? isHigh() : false;
  }

  std::function<bool()> isHigh;
  int reads = 0;
};

// Records each SPI transaction together with the D/C line level at the time,
// and regroups them into commands with their parameter bytes.
class RecordingSpiDevice final : public SpiDevice
{
public:
  struct Command
  {
    std::uint8_t code;
    std::vector<std::uint8_t> data;
  };

  explicit RecordingSpiDevice(const FakeOutputPin &dataCommand) : dataCommand_(dataCommand)
  {
  }

  bool Write(const std::uint8_t *data, std::size_t length) override
  {
    if (failWrites)
    {
      return false;
    }
    const bool isData = dataCommand_.level;
    if (!isData)
    {
      commands.push_back({data[0], {}});
      // A command is always sent on its own.
      if (length != 1)
      {
        malformed = true;
      }
      if (onCommand)
      {
        onCommand(data[0]);
      }
    }
    else if (commands.empty())
    {
      malformed = true; // data with no command before it
    }
    else
    {
      commands.back().data.insert(commands.back().data.end(), data, data + length);
    }
    return true;
  }

  std::vector<std::uint8_t> Codes() const
  {
    std::vector<std::uint8_t> codes;
    for (const auto &c : commands)
    {
      codes.push_back(c.code);
    }
    return codes;
  }

  // Data sent with the last occurrence of `code`, or empty.
  std::vector<std::uint8_t> LastDataFor(std::uint8_t code) const
  {
    for (auto it = commands.rbegin(); it != commands.rend(); ++it)
    {
      if (it->code == code)
      {
        return it->data;
      }
    }
    return {};
  }

  std::vector<Command> commands;
  bool failWrites = false;
  bool malformed = false;
  std::function<void(std::uint8_t code)> onCommand;

private:
  const FakeOutputPin &dataCommand_;
};
