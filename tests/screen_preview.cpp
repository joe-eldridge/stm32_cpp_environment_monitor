// Renders the display's screens to PBM image files, to check layouts
// without flashing the board:
//
//   build/tests/screen_preview [output-directory]
//
// Not a test - nothing is asserted.
#include <cstdio>
#include <string>
#include <vector>

#include "main_screen.hpp"
#include "ssd1681.hpp"

namespace
{

bool WritePbm(const std::string &path, const MonoFramebuffer &canvas)
{
  std::FILE *file = std::fopen(path.c_str(), "wb");
  if (file == nullptr)
  {
    return false;
  }
  std::fprintf(file, "P4\n%u %u\n", canvas.Width(), canvas.Height());
  // PBM uses 1 = black, the inverse of the framebuffer.
  for (std::size_t i = 0; i < canvas.SizeBytes(); ++i)
  {
    std::fputc(static_cast<unsigned char>(~canvas.Data()[i]), file);
  }
  return std::fclose(file) == 0;
}

} // namespace

int main(int argc, char **argv)
{
  const std::string directory = argc > 1 ? argv[1] : ".";
  std::vector<std::uint8_t> memory(Ssd1681::kImageBytes);
  MonoFramebuffer canvas(memory.data(), Ssd1681::kWidth, Ssd1681::kHeight);

  MainScreenData normal;
  normal.time = Ds3231::DateTime{2026, 9, 17, 14, 35, 0};
  normal.climate = Bme280::Measurements{2384, 100893, 5020};
  normal.light = Veml7700::Reading{120180, false};
  normal.storageOk = true;
  normal.wakeIntervalMinutes = 1;

  MainScreenData extremes = normal;
  extremes.climate = Bme280::Measurements{-4000, 110000, 10000};
  extremes.light = Veml7700::Reading{440395, true};
  extremes.wakeIntervalMinutes = 60;

  MainScreenData empty;
  empty.wakeIntervalMinutes = 5;

  const struct
  {
    const char *name;
    const MainScreenData &data;
  } screens[] = {{"main_normal", normal}, {"main_extremes", extremes}, {"main_no_data", empty}};

  for (const auto &screen : screens)
  {
    DrawMainScreen(canvas, screen.data);
    const std::string path = directory + "/" + screen.name + ".pbm";
    if (!WritePbm(path, canvas))
    {
      std::fprintf(stderr, "failed to write %s\n", path.c_str());
      return 1;
    }
    std::printf("wrote %s\n", path.c_str());
  }
  return 0;
}
