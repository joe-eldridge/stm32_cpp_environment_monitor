#include "app.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

#include "bme280.hpp"
#include "button.hpp"
#include "build_config.hpp"
#include "delay.hpp"
#include "ds3231.hpp"
#include "epaper_display.hpp"
#include "fat_time.hpp"
#include "fatfs.h"
#include "i2c_bus.hpp"
#include "hal_gpio_pin.hpp"
#include "hourly_history.hpp"
#include "i2c_fault_injection.hpp"
#include "main_screen.hpp"
#include "menu.hpp"
#include "menu_screen.hpp"
#include "low_power.hpp"
#include "main.h"
#include "mono_framebuffer.hpp"
#include "rotary_encoder.hpp"
#include "sdcard_diskio.h"
#include "spi_bus.hpp"
#include "stm32l0xx_ll_exti.h"
#include "text_format.hpp"
#include "time_sync.hpp"
#include "trend_screen.hpp"
#include "veml7700.hpp"

extern "C" UART_HandleTypeDef huart2;
extern "C" void SystemClock_Config(void);

namespace
{
// Blinks LD2 forever. Used only for a hardware/bus-level fault (RTC didn't
// even ack its own init write) - not recoverable via a UART prompt, unlike
// an untrustworthy-but-otherwise-working RTC.
[[noreturn]] void HaltWithError()
{
  while (true)
  {
    HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
    HAL_Delay(100);
  }
}

// TEMPORARY: 1 minute instead of the real 5, purely so a Stop-mode wake
// cycle can be observed in about a minute during testing rather than five.
constexpr std::uint8_t kWakeIntervalMinutes = 1;
static_assert(Ds3231::IsValidWakeInterval(kWakeIntervalMinutes), "wake interval must divide 60 evenly");

// A full refresh clears the ghosting partial refreshes leave behind. With a
// 1-minute wake interval this is once an hour.
constexpr std::uint32_t kSamplesPerFullRefresh = 60;

// Samples are averaged over this period, and the 24 most recent periods are
// plotted on the trend screen - so an hour each, covering a day. Shorten it
// (to 1, say) to watch the plot fill during testing; the trend screen then
// replaces the main screen on nearly every wake.
constexpr std::uint16_t kTrendPeriodMinutes = 60;

// Which metric the trend screen plots. The planned encoder menu will make
// this switchable at runtime.
constexpr Metric kTrendMetric = Metric::Temperature;

// A day of averages, about 700 bytes - static rather than on the 1 KB stack.
HourlyHistory g_history(kTrendPeriodMinutes);

// Quadrature steps per detent of the encoder: four transitions, as the part
// is built. Its datasheet is no help, quoting both 20 pulses and 30
// positions per revolution, so this was settled on the bench. Two appeared
// right while the encoder was polled, because a 2ms poll misses about half
// the transitions; decoding in the interrupt sees all four.
constexpr std::uint8_t kEncoderStepsPerDetent = 4;

// How often the switch is sampled while the menu is open. The knob is not
// polled at all - it is decoded in its interrupt - but a button is a level
// to debounce rather than edges to catch, and polling it needs no interrupt
// of its own.
constexpr std::uint32_t kMenuPollMs = 2;

// The menu closes itself if it is left untouched, so a knocked knob can't
// hold the device awake and flatten the battery.
constexpr std::uint32_t kMenuIdleTimeoutMs = 30000;

// A panel refresh takes the best part of a second, during which nothing can
// be polled. Waiting for the input to stop before redrawing means spinning
// the knob past three items costs one refresh rather than three, so the
// screen catches up with the knob instead of trailing it.
constexpr std::uint32_t kMenuRedrawQuietMs = 150;

// Menu messages are shown until dismissed, so one that isn't a literal needs
// somewhere to live that outlives the request that built it.
char g_menuMessage[20] = "";

// Set from the encoder switch's interrupt. Its only job is to wake the MCU
// from Stop mode and say why it woke.
volatile bool g_menuRequested = false;

// The encoder is decoded in the interrupt rather than polled. A display
// refresh blocks the main loop for the best part of a second, and a knob
// turned during one used to be lost entirely; the interrupt catches every
// edge whatever the loop is doing.
RotaryEncoder g_encoder(kEncoderStepsPerDetent);
volatile std::int32_t g_detents = 0;

// Takes what the encoder has decoded since the last call. Interrupts are held
// off for the read and clear together, so a detent arriving in between can't
// be dropped.
int TakeDetents()
{
  // Saved and restored rather than simply re-enabled: this must not turn
  // interrupts on for a caller that had deliberately turned them off.
  const std::uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const int detents = static_cast<int>(g_detents);
  g_detents = 0;
  __set_PRIMASK(primask);
  return detents;
}

// The encoder's interrupts are on only while the menu is open. Left enabled,
// a knocked knob would wake the MCU out of Stop mode and cost a wake cycle
// for nothing.
void SetEncoderInterrupts(bool enabled)
{
  if (enabled)
  {
    __HAL_GPIO_EXTI_CLEAR_IT(ENC_CLK_Pin | ENC_DT_Pin); // whatever happened while off isn't news
    LL_EXTI_EnableIT_0_31(LL_EXTI_LINE_1 | LL_EXTI_LINE_2);
  }
  else
  {
    LL_EXTI_DisableIT_0_31(LL_EXTI_LINE_1 | LL_EXTI_LINE_2);
  }
}

// The display framebuffer and a copy of what the panel is showing: 5 KB
// each, static so they aren't on the 1 KB stack. The second copy is what
// lets an update send only the rows that changed.
std::array<std::uint8_t, Ssd1681::kImageBytes> g_frame{};
std::array<std::uint8_t, Ssd1681::kImageBytes> g_panelImage{};
static_assert(MonoFramebuffer::BufferSize(Ssd1681::kWidth, Ssd1681::kHeight) == Ssd1681::kImageBytes);

// Set once the RTC is up, so FatFs's get_fattime() hook can timestamp files.
// FatFs only calls it from within this thread's f_* calls - never from an ISR.
Ds3231 *g_fatTimeRtc = nullptr;

// Blocking UART output, used for one-off boot messages.
void SendLine(const char *message)
{
  HAL_UART_Transmit(&huart2, reinterpret_cast<const uint8_t *>(message),
                     static_cast<uint16_t>(std::strlen(message)), HAL_MAX_DELAY);
}

void SendDecimal(std::int32_t value)
{
  if (value < 0)
  {
    const uint8_t minus = '-';
    HAL_UART_Transmit(&huart2, &minus, 1, HAL_MAX_DELAY);
    value = -value;
  }

  static const char kDigits[] = "0123456789";
  char digits[10];
  int count = 0;
  do
  {
    digits[count++] = kDigits[value % 10];
    value /= 10;
  } while (value != 0);
  while (count > 0)
  {
    const uint8_t ch = static_cast<uint8_t>(digits[--count]);
    HAL_UART_Transmit(&huart2, &ch, 1, HAL_MAX_DELAY);
  }
}

// Per-wake diagnostics. At 9600 baud each character keeps the MCU awake for
// about 1 ms, and a wake prints around 70 of them, so Release builds drop
// these. Boot-time messages use SendLine() directly and always print.
void WakeLog(const char *message)
{
  if (kDebugBuild)
  {
    SendLine(message);
  }
}

void WakeLogDecimal(std::int32_t value)
{
  if (kDebugBuild)
  {
    SendDecimal(value);
  }
}

constexpr const char *kLogFileName = "LOG.CSV";
constexpr const char *kLogHeader =
    "Timestamp,TemperatureCentiC,PressureCentiHpa,HumidityCentiPct,LuxCenti,LuxSaturated\n";

// Appends one CSV row for this sample: an RTC timestamp plus whichever
// sensor readings succeeded (blank field if a sensor read failed). Logging
// failures (SD not mounted, write error) are reported in the wake log but not
// fatal - sensor sampling and the sleep/wake cycle continue regardless,
// since a lost log entry shouldn't stop the device monitoring.
bool LogRecord(const std::optional<Ds3231::DateTime> &dt, const std::optional<Bme280::Measurements> &measurements,
               const std::optional<Veml7700::Reading> &light)
{
  if (!dt)
  {
    WakeLog(" LogSkipped(rtc)");
    return false;
  }

  FIL file;
  FRESULT result = f_open(&file, kLogFileName, FA_WRITE | FA_OPEN_APPEND);
  if (result == FR_DISK_ERR)
  {
    // With no card-detect input, a swapped card is only noticed when I/O to
    // it fails. That failure marks the card uninitialised, so one retry lets
    // FatFs re-initialise and re-mount it instead of losing this sample.
    // (FR_NOT_READY isn't retried: it means initialisation just failed, e.g.
    // no card inserted, and retrying would only double the time awake.)
    result = f_open(&file, kLogFileName, FA_WRITE | FA_OPEN_APPEND);
  }
  if (result != FR_OK)
  {
    WakeLog(" LogSkipped(open, FRESULT ");
    WakeLogDecimal(result);
    WakeLog(")");
    return false;
  }

  if (f_size(&file) == 0)
  {
    f_printf(&file, kLogHeader);
  }

  f_printf(&file, "%04u-%02u-%02u %02u:%02u:%02u,", dt->year, dt->month, dt->date, dt->hour, dt->minute,
           dt->second);

  if (measurements)
  {
    f_printf(&file, "%d,%d,%d,", measurements->temperatureCenti, measurements->pressureCentiHpa,
             measurements->humidityCentiPct);
  }
  else
  {
    f_printf(&file, ",,,");
  }

  if (light)
  {
    f_printf(&file, "%d,%d\n", light->luxCenti, light->saturated ? 1 : 0);
  }
  else
  {
    f_printf(&file, ",\n");
  }

  // f_close() flushes the buffered row to the card, so this is where a
  // failed write actually shows up.
  result = f_close(&file);
  if (result != FR_OK)
  {
    WakeLog(" LogFailed(close, FRESULT ");
    WakeLogDecimal(result);
    WakeLog(")");
    return false;
  }
  WakeLog(" Logged");
  return true;
}

// Runs the menu to its end: polls the encoder and switch, redraws when
// something changes, and carries out what the menu asks for. Returns when
// the menu closes or is left untouched for kMenuIdleTimeoutMs.
void RunMenu(Ds3231 &rtc, EpaperDisplay &display, const Ssd1681 &panel, InputPin &clk, InputPin &dt, InputPin &sw)
{
  Menu menu;
  const std::optional<Ds3231::DateTime> now = rtc.ReadDateTime();
  if (!now)
  {
    // Without the time there is nothing to seed the clock editor with, and
    // the RTC is the one thing the menu can't work around.
    WakeLog(" MenuSkipped(rtc)");
    return;
  }
  menu.Open(*now);

  // Start from where the knob is sitting, and throw away anything decoded
  // before the menu opened.
  g_encoder.Reset(clk.IsHigh(), dt.IsHigh());
  static_cast<void>(TakeDetents());
  SetEncoderInterrupts(true);

  Button button;
  // The switch that woke the MCU is usually still down: adopt it, or its
  // release would open the menu and immediately act on it.
  button.Reset(!sw.IsHigh(), HAL_GetTick());

  bool firstDraw = true;
  const auto drawMenu = [&display, &panel, &menu, &firstDraw](const char *what) {
    const MenuView view = menu.View();
    // The first draw replaces a whole different screen, so it is full;
    // moving about within the menu is partial, which is far quicker.
    const Ssd1681::RefreshMode mode = firstDraw ? Ssd1681::RefreshMode::Full : Ssd1681::RefreshMode::Partial;
    const std::uint32_t startedMs = HAL_GetTick();
    const bool updated = display.Update(mode, [&view](MonoFramebuffer &c) { DrawMenuScreen(c, view); });
    const std::uint32_t totalMs = HAL_GetTick() - startedMs;
    WakeLog(firstDraw ? "\r\nMenu full refresh " : "\r\nMenu partial refresh ");
    WakeLogDecimal(static_cast<std::int32_t>(totalMs));
    WakeLog("ms (panel ");
    WakeLogDecimal(static_cast<std::int32_t>(panel.LastRefreshMs()));
    WakeLog("ms, transfers ");
    WakeLogDecimal(static_cast<std::int32_t>(totalMs - panel.LastRefreshMs()));
    WakeLog(updated ? "ms)" : "ms) FAILED");
    WakeLog(what);
    firstDraw = false;
  };

  bool redraw = true;
  std::uint32_t lastInputMs = HAL_GetTick();
  // The opening screen is drawn at once; the quiet period only applies to
  // changes made from inside the menu.
  std::uint32_t lastChangeMs = HAL_GetTick() - kMenuRedrawQuietMs;

  while (menu.IsOpen())
  {
    if (redraw && HAL_GetTick() - lastChangeMs >= kMenuRedrawQuietMs)
    {
      drawMenu("");
      redraw = false;
      // Nothing is polled during a refresh, so the switch starts again from
      // where it is now. The knob needs no such treatment: its interrupts
      // kept running throughout, and what it did is waiting to be read.
      button.Reset(!sw.IsHigh(), HAL_GetTick());
      lastInputMs = HAL_GetTick();
    }

    const int detents = TakeDetents();
    const Button::Event event = button.Update(!sw.IsHigh(), HAL_GetTick()); // active low, with a pull-up
    if (detents != 0 || event != Button::Event::None)
    {
      lastInputMs = HAL_GetTick();
      lastChangeMs = lastInputMs;
      redraw = true;
    }

    const MenuRequest request = menu.Update(detents, event);
    switch (request.action)
    {
    case MenuAction::SetTime:
      menu.Complete(rtc.SetDateTime(request.time) && rtc.ClearOscillatorStopFlag() ? "Clock set" : "Clock NOT set");
      break;

    case MenuAction::EjectCard:
      // Unregister the volume and let go of the card, so what FatFs has
      // buffered is on it before it's pulled.
      static_cast<void>(f_mount(nullptr, USERPath, 0));
      SdCard_Deinit();
      menu.Complete("Safe to remove");
      break;

    case MenuAction::EraseLogs:
    {
      // Deleting the file, not rebuilding the filesystem. Making a new
      // filesystem means writing out the whole FAT, which on a 16GB card at
      // this bus speed takes over ten minutes; unlinking one file takes
      // milliseconds and is what "erase all logs" actually means. A card
      // that needs a real format is a job for a PC.
      const FRESULT result = f_unlink(kLogFileName);
      WakeLog(" erase FRESULT ");
      WakeLogDecimal(static_cast<std::int32_t>(result));
      if (result == FR_OK)
      {
        menu.Complete("Logs erased");
      }
      else if (result == FR_NO_FILE)
      {
        menu.Complete("No logs to erase");
      }
      else
      {
        char code[4] = "";
        static_cast<void>(text_format::Integer(code, sizeof(code), static_cast<std::int32_t>(result)));
        text_format::Copy(g_menuMessage, sizeof(g_menuMessage), "Erase failed ");
        text_format::Append(g_menuMessage, sizeof(g_menuMessage), code);
        menu.Complete(g_menuMessage);
      }
      break;
    }

    case MenuAction::Close:
    case MenuAction::None:
      break;
    }

    if (request.action != MenuAction::None && request.action != MenuAction::Close)
    {
      // An outcome is worth showing at once rather than after the quiet
      // period: the user is waiting on it, and no more input is coming.
      redraw = true;
      lastChangeMs = HAL_GetTick() - kMenuRedrawQuietMs;
      continue;
    }

    if (HAL_GetTick() - lastInputMs >= kMenuIdleTimeoutMs)
    {
      WakeLog(" MenuTimedOut");
      break;
    }

    DelayMs(kMenuPollMs);
  }

  SetEncoderInterrupts(false);

  // The card is registered again on the way out, whichever way the menu was
  // left, so the next sample logs as usual. This doesn't touch the card, so
  // it works with the card still out.
  static_cast<void>(f_mount(&USERFatFS, USERPath, 0));
}

} // namespace

extern "C" void HAL_GPIO_EXTI_Callback(std::uint16_t GPIO_Pin)
{
  // The RTC alarm on PC0 also lands here; it needs no handling beyond
  // having woken the MCU.
  if (GPIO_Pin == ENC_SW_Pin)
  {
    g_menuRequested = true;
  }
  else if (GPIO_Pin == ENC_CLK_Pin || GPIO_Pin == ENC_DT_Pin)
  {
    // Both lines are read on either line's edge: what the decoder needs is
    // the pair, not which of them moved.
    const bool clk = HAL_GPIO_ReadPin(ENC_CLK_GPIO_Port, ENC_CLK_Pin) == GPIO_PIN_SET;
    const bool dt = HAL_GPIO_ReadPin(ENC_DT_GPIO_Port, ENC_DT_Pin) == GPIO_PIN_SET;
    g_detents += g_encoder.Update(clk, dt);
  }
}

std::uint32_t AppGetFatTime()
{
  if (g_fatTimeRtc == nullptr)
  {
    return 0;
  }
  const std::optional<Ds3231::DateTime> dt = g_fatTimeRtc->ReadDateTime();
  if (!dt)
  {
    return 0;
  }
  return PackFatTime(*dt);
}

void AppMain()
{
  low_power::ConfigureStopMode();

  // Pins as configured by MX_I2C1_Init(): PB8 = SCL, PB9 = SDA.
  const I2cBus::Pin i2c1Scl{GPIOB, LL_GPIO_PIN_8};
  const I2cBus::Pin i2c1Sda{GPIOB, LL_GPIO_PIN_9};
  I2cBus i2c1(I2C1, i2c1Scl, i2c1Sda);

  // A reset can land mid-transaction and leave a sensor holding SDA low.
  // The peripheral can't detect that as "busy" (it never saw the START), so
  // clear it up front rather than letting the first transaction fail.
  const bool sdaStuckAtBoot = !LL_GPIO_IsInputPinSet(i2c1Sda.port, i2c1Sda.pinMask);
  const bool busReleased = i2c1.Recover();
  if (sdaStuckAtBoot)
  {
    SendLine("\r\nI2C: SDA held low at boot - recovery ");
    SendLine(busReleased ? "succeeded" : "FAILED");
  }

  // Test hook for the recovery above: hold B1 while pressing reset. The bus
  // is deliberately wedged mid-read (BME280 chip ID 0x60 starts with a 0
  // bit), then the MCU resets itself, as if a reset had hit a real read.
  // Release B1 before the reset completes, or the cycle repeats.
  if (HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_RESET)
  {
    SendLine("\r\nB1 held - wedging I2C bus: ");
    const bool wedged = WedgeI2cBus(I2C1, i2c1Scl, i2c1Sda, Bme280::kI2cAddressSdoHigh, 0xD0);
    SendLine(wedged ? "SDA held low, resetting" : "FAILED to wedge, resetting");
    NVIC_SystemReset();
  }

  I2cBusDevice rtcDevice(i2c1, Ds3231::kI2cAddress7Bit);
  I2cBusDevice bme280Device(i2c1, Bme280::kI2cAddressSdoHigh); // SDO tied to VDD, matching hello_world's wiring
  I2cBusDevice veml7700Device(i2c1, Veml7700::kI2cAddress7Bit);

  Ds3231 rtc(rtcDevice);
  if (!rtc.Init())
  {
    HaltWithError();
  }

  if (!rtc.ClearAlarmFlag())
  {
    // A1F may already be latched from earlier testing/manufacturing -
    // if so, SQW/INT is already held low, and PC0's falling-edge EXTI
    // trigger would never see an edge to wake on. Must start clean.
    HaltWithError();
  }

  if (rtc.IsOscillatorStopped())
  {
    SyncTimeFromUart(&huart2, rtc);
  }

  g_fatTimeRtc = &rtc;

  Bme280 bme280(bme280Device);
  if (!bme280.Init())
  {
    HaltWithError();
  }

  Veml7700 veml7700(veml7700Device);
  if (!veml7700.Init())
  {
    HaltWithError();
  }

  SpiBus einkSpi(SPI1, eInk_CS_GPIO_Port, eInk_CS_Pin);
  HalOutputPin einkDataCommand(eInk_D_C_GPIO_Port, eInk_D_C_Pin);
  HalOutputPin einkReset(eInk_RST_GPIO_Port, eInk_RST_Pin);
  HalInputPin einkBusy(eInk_BUSY_GPIO_Port, eInk_BUSY_Pin);
  Ssd1681 panel(einkSpi, einkDataCommand, einkReset, einkBusy);
  MonoFramebuffer canvas(g_frame.data(), Ssd1681::kWidth, Ssd1681::kHeight);
  EpaperDisplay display(panel, canvas, g_panelImage.data());

  // The SPI bit rate sets how long a 5KB image takes to send, so it is worth
  // knowing exactly rather than inferring it from the clock tree.
  WakeLog("\r\nSPI1 baud rate divider: 1/");
  WakeLogDecimal(static_cast<std::int32_t>(2u << ((SPI1->CR1 >> 3) & 0x7u)));
  WakeLog(", PCLK2 ");
  WakeLogDecimal(static_cast<std::int32_t>(HAL_RCC_GetPCLK2Freq()));
  WakeLog("Hz");

  MX_FATFS_Init();
  const FRESULT mounted = f_mount(&USERFatFS, USERPath, 1);
  const SdCardInitReport initReport = SdCard_GetInitReport();
  SendLine("\r\nSD init took ");
  SendDecimal(static_cast<std::int32_t>(initReport.durationMs));
  SendLine("ms, reached ");
  SendLine(initReport.stage);
  if (mounted != FR_OK)
  {
    // Not fatal: sensor sampling and Stop-mode sleep/wake still work
    // without a card, and each LogRecord() retries the mount, so logging
    // starts by itself once a card is inserted.
    SendLine("\r\nSD mount failed - will retry each wake");
  }

  // CubeMX enables both encoder EXTI lines at init; they stay masked until
  // the menu opens.
  SetEncoderInterrupts(false);

  HalInputPin encoderClk(ENC_CLK_GPIO_Port, ENC_CLK_Pin);
  HalInputPin encoderDt(ENC_DT_GPIO_Port, ENC_DT_Pin);
  HalInputPin encoderSwitch(ENC_SW_GPIO_Port, ENC_SW_Pin);

  // Sample 0 is taken straight after boot, so the screen shows real values
  // from the start; every later sample follows an RTC alarm wake.
  bool wasTrendScreen = false;
  bool leftMenu = false;
  for (std::uint32_t sample = 0;; ++sample)
  {
    WakeLog("\r\nSample #");
    WakeLogDecimal(static_cast<std::int32_t>(sample));
    WakeLog(" ");

    const std::optional<Ds3231::DateTime> now = rtc.ReadDateTime();

    const std::optional<Bme280::Measurements> measurements = bme280.Read();
    if (measurements)
    {
      WakeLog("T=");
      WakeLogDecimal(measurements->temperatureCenti);
      WakeLog(" P=");
      WakeLogDecimal(measurements->pressureCentiHpa);
      WakeLog(" H=");
      WakeLogDecimal(measurements->humidityCentiPct);
    }
    else
    {
      WakeLog("BME280 read failed");
    }

    const std::optional<Veml7700::Reading> light = veml7700.Read();
    if (light)
    {
      WakeLog(" Lux=");
      WakeLogDecimal(light->luxCenti);
      if (light->saturated)
      {
        WakeLog("(saturated)");
      }
    }
    else
    {
      WakeLog(" VEML7700 read failed");
    }

    const bool logged = LogRecord(now, measurements, light);

    // Without a timestamp a sample can't be placed on the time axis, so it
    // is logged (above, if the RTC read succeeded) but not averaged.
    const bool periodEnded = now ? g_history.Add(*now, measurements, light) : false;

    MainScreenData screen;
    screen.time = now;
    screen.climate = measurements;
    screen.light = light;
    screen.storageOk = logged;
    screen.wakeIntervalMinutes = kWakeIntervalMinutes;

    TrendScreenData trend;
    trend.history = &g_history;
    trend.metric = kTrendMetric;
    trend.time = now;

    // The trend screen appears for one wake whenever a period completes,
    // i.e. on the first sample of each hour; the main screen shows the rest
    // of the time. Swapping between two whole screens is worth a full
    // refresh, which also clears any accumulated ghosting.
    // The first update after boot is always full (EpaperDisplay enforces it),
    // clearing whatever the panel showed before. A failure isn't fatal:
    // sampling and logging carry on.
    const bool fullRefresh =
        periodEnded || wasTrendScreen || leftMenu || (sample % kSamplesPerFullRefresh == 0);
    const Ssd1681::RefreshMode mode = fullRefresh ? Ssd1681::RefreshMode::Full : Ssd1681::RefreshMode::Partial;
    const bool updated = periodEnded
                             ? display.Update(mode, [&trend](MonoFramebuffer &c) { DrawTrendScreen(c, trend); })
                             : display.Update(mode, [&screen](MonoFramebuffer &c) { DrawMainScreen(c, screen); });
    if (!updated)
    {
      WakeLog(" DisplayFailed");
    }
    wasTrendScreen = periodEnded;
    leftMenu = false;

    // A press while the device was awake is honoured here rather than being
    // held over until after the next alarm, when it would look like the
    // menu had opened by itself.
    if (g_menuRequested)
    {
      WakeLog("\r\nMenu opened");
      RunMenu(rtc, display, panel, encoderClk, encoderDt, encoderSwitch);
      leftMenu = true;
      // Every press inside the menu re-raises the interrupt, so the flag is
      // cleared on the way out rather than on the way in - otherwise the
      // menu would reopen the moment it was closed.
      g_menuRequested = false;
    }

    if (!rtc.ScheduleNextAlarm(kWakeIntervalMinutes))
    {
      // No alarm armed means nothing will ever wake the device again - halt
      // visibly rather than sleep into a dead end.
      HaltWithError();
    }

    WakeLog("\r\nSleeping...");

    // SysTick (HCLK-derived) has no clock source once the core stops in
    // Stop mode - suspending it first avoids it silently missing ticks
    // that would otherwise throw off HAL_GetTick()-based timeouts on wake.
    HAL_SuspendTick();
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

    // Stop mode always resumes on MSI regardless of what was running
    // before it, and fully powers down HSI16 (needed for I2C1's Fast Mode
    // kernel clock) - both need restoring before anything below can
    // safely touch the RTC over I2C.
    SystemClock_Config();
    HAL_ResumeTick();

    if (!rtc.ClearAlarmFlag())
    {
      // Alarm flag stuck set means SQW/INT stays asserted and this loop
      // would spin through Stop mode with no real sleep ever happening.
      HaltWithError();
    }

    // Woken by the encoder switch rather than the alarm: show the menu, then
    // carry on round the loop, which takes a fresh sample and re-arms the
    // alarm.
    if (g_menuRequested)
    {
      WakeLog("\r\nMenu opened");
      RunMenu(rtc, display, panel, encoderClk, encoderDt, encoderSwitch);
      leftMenu = true;
      // Every press inside the menu re-raises the interrupt, so the flag is
      // cleared on the way out rather than on the way in - otherwise the
      // menu would reopen the moment it was closed.
      g_menuRequested = false;
    }
  }
}
