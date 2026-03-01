#include "vl53l3cx_lib.h"

#include "main.h"
#include "vl53l3cx.h"

#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

#define TOF_I2C_ADDRESS_8BIT       VL53L3CX_DEVICE_ADDRESS
#define TOF_I2C_TIMEOUT_MS          100U
#define TOF_I2C1_TIMING_400KHZ      0x00000004U
#define TOF_TIMING_BUDGET_MS        30U

static VL53L3CX_Object_t s_tof_obj;
static VL53L3CX_Result_t result;
static uint8_t s_tof_initialized = 0U;
static uint8_t s_tof_started = 0U;

static int32_t tof_bus_init(void);
static int32_t tof_bus_deinit(void);
static int32_t tof_bus_write(uint16_t address, uint8_t *data, uint16_t length);
static int32_t tof_bus_read(uint16_t address, uint8_t *data, uint16_t length);
static int32_t tof_bus_get_tick(void);
static bool tof_activate(void);

static bool fresh = true;

static int32_t tof_bus_init(void)
{
  hi2c1.Init.Timing = TOF_I2C1_TIMING_400KHZ;

  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    return -1;
  }

  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    return -1;
  }

  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

static int32_t tof_bus_deinit(void)
{
  return 0;
}

static int32_t tof_bus_write(uint16_t address, uint8_t *data, uint16_t length)
{
  if (HAL_I2C_Master_Transmit(&hi2c1, address, data, length, TOF_I2C_TIMEOUT_MS) == HAL_OK)
  {
    return 0;
  }

  return -1;
}

static int32_t tof_bus_read(uint16_t address, uint8_t *data, uint16_t length)
{
  if (HAL_I2C_Master_Receive(&hi2c1, address, data, length, TOF_I2C_TIMEOUT_MS) == HAL_OK)
  {
    return 0;
  }

  return -1;
}

static int32_t tof_bus_get_tick(void)
{
  return (int32_t)HAL_GetTick();
}

bool tof_init(void)
{
  VL53L3CX_IO_t io_ctx;
  VL53L3CX_ProfileConfig_t profile;

  if (s_tof_initialized == 1U)
  {
    return true;
  }

  memset(&s_tof_obj, 0, sizeof(s_tof_obj));

  HAL_GPIO_WritePin(VL53L3CX_xshout_GPIO_Port, VL53L3CX_xshout_Pin, GPIO_PIN_RESET);
  HAL_Delay(2);
  HAL_GPIO_WritePin(VL53L3CX_xshout_GPIO_Port, VL53L3CX_xshout_Pin, GPIO_PIN_SET);
  HAL_Delay(5);

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

  if (VL53L3CX_Init(&s_tof_obj) != VL53L3CX_OK)
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

  if (VL53L3CX_Start(&s_tof_obj, VL53L3CX_MODE_ASYNC_CONTINUOUS) != VL53L3CX_OK)
  {
    return false;
  }

  s_tof_started = 1U;
  return true;
}


int32_t tof_get_distance(void)
{
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

  for(int i=0; i < 4; i++)
  {
	  VL53L3CX_GetDistance(&s_tof_obj, &result);
	  if(fresh)
	  {
		  fresh = !fresh;
		  HAL_Delay(100);
	  }
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
