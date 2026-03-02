#include "vl53l3cx_lib.h"

#include "../app.h"
#include "app_delay.h"
#include "main.h"
#include "vl53l3cx.h"
#include "vl53lx_api.h"

#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define TOF_I2C_ADDRESS_8BIT      VL53L3CX_DEVICE_ADDRESS
#define TOF_I2C_TIMEOUT_MS        250U
#define TOF_TIMING_BUDGET_MS      30U
#define TOF_RESET_PULSE_MS        5U
#define TOF_BOOT_TIME_MS          10U

static VL53L3CX_Object_t s_tof_obj;
static VL53L3CX_Result_t result;
static uint8_t s_tof_initialized = 0U;
static uint8_t s_tof_started = 0U;
static bool s_first_reading = true;

static int32_t tof_bus_init(void);
static int32_t tof_bus_deinit(void);
static int32_t tof_bus_write(uint16_t address, uint8_t *data, uint16_t length);
static int32_t tof_bus_read(uint16_t address, uint8_t *data, uint16_t length);
static int32_t tof_bus_get_tick(void);
static bool tof_activate(void);
static bool tof_reset_and_boot(void);
static bool tof_low_level_init(void);
static bool tof_is_non_fatal_ref_spad_status(VL53LX_Error status);

static int32_t tof_bus_init(void)
{
  /* I2C1 is configured once by CubeMX in MX_I2C1_Init(). */
  return 0;
}

static int32_t tof_bus_deinit(void)
{
  return 0;
}

static int32_t tof_bus_write(uint16_t address, uint8_t *data, uint16_t length)
{
  return (app_i2c_master_transmit(&hi2c1, address, data, length, TOF_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int32_t tof_bus_read(uint16_t address, uint8_t *data, uint16_t length)
{
  return (app_i2c_master_receive(&hi2c1, address, data, length, TOF_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : -1;
}

static int32_t tof_bus_get_tick(void)
{
  return (int32_t)HAL_GetTick();
}

static bool tof_reset_and_boot(void)
{
  HAL_GPIO_WritePin(VL53L3CX_xshout_GPIO_Port, VL53L3CX_xshout_Pin, GPIO_PIN_RESET);
  app_delay_ms(TOF_RESET_PULSE_MS);
  HAL_GPIO_WritePin(VL53L3CX_xshout_GPIO_Port, VL53L3CX_xshout_Pin, GPIO_PIN_SET);
  app_delay_ms(TOF_BOOT_TIME_MS);

  return true;
}

static bool tof_is_non_fatal_ref_spad_status(VL53LX_Error status)
{
  return (status == VL53LX_WARNING_REF_SPAD_CHAR_NOT_ENOUGH_SPADS) ||
         (status == VL53LX_WARNING_REF_SPAD_CHAR_RATE_TOO_HIGH) ||
         (status == VL53LX_WARNING_REF_SPAD_CHAR_RATE_TOO_LOW);
}

static bool tof_low_level_init(void)
{
  VL53LX_Error status;

  status = VL53LX_WaitDeviceBooted(&s_tof_obj);
  if (status != VL53LX_ERROR_NONE)
  {
    printf("ToF init: WaitDeviceBooted failed (%ld)\r\n", (long)status);
    return false;
  }

  status = VL53LX_DataInit(&s_tof_obj);
  if (status != VL53LX_ERROR_NONE)
  {
    printf("ToF init: DataInit failed (%ld)\r\n", (long)status);
    return false;
  }

  status = VL53LX_PerformRefSpadManagement(&s_tof_obj);
  if ((status != VL53LX_ERROR_NONE) && !tof_is_non_fatal_ref_spad_status(status))
  {
    printf("ToF init: RefSpadManagement failed (%ld)\r\n", (long)status);
    return false;
  }

  if (tof_is_non_fatal_ref_spad_status(status))
  {
    printf("ToF init: RefSpadManagement warning (%ld), continuing\r\n", (long)status);
  }

  s_tof_obj.IsRanging = 0U;
  s_tof_obj.IsBlocking = 0U;
  s_tof_obj.IsContinuous = 0U;
  s_tof_obj.IsAmbientEnabled = 0U;
  s_tof_obj.IsSignalEnabled = 0U;
  s_tof_obj.IsInitialized = 1U;

  return true;
}

bool tof_init(void)
{
  VL53L3CX_IO_t io_ctx;
  VL53L3CX_ProfileConfig_t profile;
  uint32_t sensor_id;

  if (s_tof_initialized == 1U)
  {
    return true;
  }

  s_tof_started = 0U;
  s_first_reading = true;
  memset(&s_tof_obj, 0, sizeof(s_tof_obj));

  (void)tof_reset_and_boot();

  io_ctx.Init = tof_bus_init;
  io_ctx.DeInit = tof_bus_deinit;
  io_ctx.Address = TOF_I2C_ADDRESS_8BIT;
  io_ctx.WriteReg = tof_bus_write;
  io_ctx.ReadReg = tof_bus_read;
  io_ctx.GetTick = tof_bus_get_tick;

  if (VL53L3CX_RegisterBusIO(&s_tof_obj, &io_ctx) != VL53L3CX_OK)
  {
    return false;
  }

  sensor_id = 0U;
  if (VL53L3CX_ReadID(&s_tof_obj, &sensor_id) != VL53L3CX_OK)
  {
    printf("ToF init: ReadID failed\r\n");
    return false;
  }

  if (sensor_id != VL53L3CX_ID)
  {
    printf("ToF init: unexpected sensor ID 0x%04lX\r\n", (unsigned long)sensor_id);
    return false;
  }

  if (!tof_low_level_init())
  {
    return false;
  }

  profile.RangingProfile = VL53L3CX_PROFILE_LONG;
  profile.TimingBudget = TOF_TIMING_BUDGET_MS;
  profile.Frequency = 0U;
  profile.EnableAmbient = 0U;
  profile.EnableSignal = 0U;

  if (VL53L3CX_ConfigProfile(&s_tof_obj, &profile) != VL53L3CX_OK)
  {
    return false;
  }

  s_tof_initialized = 1U;
  return true;
}

static bool tof_activate(void)
{
  if (s_tof_started == 1U)
  {
    return true;
  }

  /*
   * Blocking mode prevents timeout/stale reads when the caller polls faster
   * than the sensor produces new samples.
   */
  if (VL53L3CX_Start(&s_tof_obj, VL53L3CX_MODE_BLOCKING_CONTINUOUS) != VL53L3CX_OK)
  {
    return false;
  }

  s_tof_started = 1U;
  return true;
}

int32_t tof_get_distance(void)
{
  int32_t status;
  uint32_t target_count;

  if (!tof_init())
  {
    return -1;
  }

  if (!tof_activate())
  {
    return -1;
  }

  memset(&result, 0, sizeof(result));

  if (s_first_reading)
  {
	VL53L3CX_GetDistance(&s_tof_obj, &result);
	s_first_reading = false;
	app_delay_ms(100U);
  }

  status = VL53L3CX_GetDistance(&s_tof_obj, &result);
  if (status != VL53L3CX_OK)
  {
    return -1;
  }

  target_count = result.ZoneResult[0].NumberOfTargets;

  if ((result.ZoneResult[0].Status[0] == 0U) &&
          (result.ZoneResult[0].Distance[0] > 0U) &&
		  target_count > 0)
  {
	  return (int32_t)result.ZoneResult[0].Distance[0];
  }
  else
  {
	  return -1;
  }
  return -1;
}

void VL53L3CX_TestOnce(void)
{
  int32_t distance_mm;

  distance_mm = tof_get_distance();

  if (distance_mm < 0)
  {
    printf("VL53L3CX: read failed\r\n");
    return;
  }

  printf("VL53L3CX: %ld mm\r\n", distance_mm);
}
