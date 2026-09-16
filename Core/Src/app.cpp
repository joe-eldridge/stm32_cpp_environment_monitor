#include "app.h"

#include <cstdint>
#include <cstring>
#include <optional>

#include "bme280.hpp"
#include "build_config.hpp"
#include "ds3231.hpp"
#include "fat_time.hpp"
#include "fatfs.h"
#include "i2c_bus.hpp"
#include "i2c_fault_injection.hpp"
#include "low_power.hpp"
#include "main.h"
#include "time_sync.hpp"
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

// Appends one CSV row for this wake cycle: an RTC timestamp plus whichever
// sensor readings succeeded (blank field if a sensor read failed). Logging
// failures (SD not mounted, write error) are reported in the wake log but not
// fatal - sensor sampling and the sleep/wake cycle continue regardless,
// since a lost log entry shouldn't stop the device monitoring.
void LogRecord(Ds3231 &rtc, const std::optional<Bme280::Measurements> &measurements,
               const std::optional<Veml7700::Reading> &light)
{
  const std::optional<Ds3231::DateTime> dt = rtc.ReadDateTime();
  if (!dt)
  {
    WakeLog(" LogSkipped(rtc)");
    return;
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
    return;
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
    return;
  }
  WakeLog(" Logged");
}

} // namespace

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

  if (!rtc.ScheduleNextAlarm(kWakeIntervalMinutes))
  {
    // No alarm armed means nothing will ever wake the device again - treat
    // this the same as an init failure rather than sleeping into a dead end.
    HaltWithError();
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

  MX_FATFS_Init();
  if (f_mount(&USERFatFS, USERPath, 1) != FR_OK)
  {
    // Not fatal: sensor sampling and Stop-mode sleep/wake still work
    // without a card, and each LogRecord() retries the mount, so logging
    // starts by itself once a card is inserted.
    SendLine("\r\nSD mount failed - will retry each wake");
  }

  std::uint32_t wakeCount = 0;

  for (;;)
  {
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

    WakeLog("\r\nWoke #");
    WakeLogDecimal(static_cast<std::int32_t>(wakeCount++));
    WakeLog(" ");

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

    LogRecord(rtc, measurements, light);

    // TODO: refresh the eInk display once per hour.

    if (!rtc.ScheduleNextAlarm(kWakeIntervalMinutes))
    {
      HaltWithError();
    }
  }
}
