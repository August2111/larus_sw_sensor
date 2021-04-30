/** ***********************************************************************
 * @file		communicator.cpp
 * @brief		talk to sensors and synchronize output
 * @author		Dr. Klaus Schaefer
 **************************************************************************/
#include "system_configuration.h"

#if RUN_COMMUNICATOR == 1

#include <FreeRTOS_wrapper.h>
#include "navigator.h"
#include "flight_observer.h"
#include "serial_io.h"
#include "NMEA_format.h"
#include "common.h"
#include "CAN_output.h"

void sync_logger(void);

COMMON output_data_t __ALIGNED(1024) output_data =  {{0}};
COMMON GNSS_type GNSS (output_data.c);

#ifdef DKCOM
ROM float SENSOR_MAPPING_MATRIX[] =
{
	-0.9914f, +0.0f, -0.13f,
	+0.0f,  -1.0f,   +0.0f,
	-0.13f, +0.0f,   +.9914f
};
#else
ROM float SENSOR_MAPPING_MATRIX[] = // mapping front left up into front right down
{
	+1.0f, +0.0f, +0.0f,
	+0.0f, -1.0f, +0.0f,
	+0.0f, +0.0f, -1.0f
    };
#endif

ROM float UNITY_MATRIX[3][3]=
{
  {1.0f, 0.0f, 0.0f},
  {0.0f, 1.0f, 0.0f},
  {0.0f, 0.0f, 1.0f}
};

void communicator_runnable (void*)
{
  navigator_t navigator;
  float3matrix sensor_mapping;
  float3vector acc, mag, gyro;

#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic push
  {
    float3matrix right; // start with unity matrix;
    float3matrix rotation( UNITY_MATRIX);
  }
#pragma GCC diagnostic pop

#if RUN_CAN_OUTPUT == 1
  uint8_t count_10Hz = 1; // de-synchronize CAN output by 1 cycle
#endif

#ifndef INFILE // if NOT in simulation mode

  for( unsigned i=0; i < 200; ++i) // wait 200 IMU loops
    notify_take (true);

  while( ! GNSS_new_data_ready) // another lousy spinlock !
    delay(100);
  navigator.update_GNSS( GNSS.coordinates);
  GNSS_new_data_ready = false;

  float present_heading=0.0f;
  if( D_GNSS_new_data_ready)
      present_heading = output_data.c.relPosHeading;
  navigator.set_attitude(0.0f, 0.0f, present_heading);

  navigator.update_pabs (output_data.m.static_pressure);
  navigator.reset_altitude();
#else
  uint32_t GNSS_sim=0;
#endif

  while (true)
    {
      notify_take (true); // wait for synchronization by IMU @ 100 Hz

      navigator.update_pabs (output_data.m.static_pressure);
      navigator.update_pitot(output_data.m.pitot_pressure);

#ifndef INFILE // if not in sim mode
      if (GNSS_new_data_ready) // triggered at 10 Hz by GNSS
	{
	  GNSS_new_data_ready = false;
#else
      if( ++GNSS_sim >=10)
	{
	  GNSS_sim=0;
	  GNSS.fix_type=FIX_3d; // has not been recorded ...
#endif
	  navigator.update_GNSS( GNSS.coordinates);
	}

      // rotate sensor coordinates into airframe coordinates
      acc  = sensor_mapping * output_data.m.acc;
      mag  = sensor_mapping * output_data.m.mag;
      gyro = sensor_mapping * output_data.m.gyro;

      navigator.update_IMU ( acc, mag, gyro);

      navigator.report_data (output_data);

#if RUN_CAN_OUTPUT == 1
      if (++count_10Hz >= 10)
	{
	  count_10Hz = 0;
	  trigger_CAN (); // todo alle abtastraten checken !
	}
#endif

#ifdef INFILE // in sim mode: logger: continue
      sync_logger();
#endif

    }
}

#define STACKSIZE 1024 // in 32bit words
static uint32_t __ALIGNED(STACKSIZE*4) stack_buffer[STACKSIZE];

static TaskParameters_t p =
  {
      communicator_runnable,
      "COM",
      STACKSIZE,
      0,
      COMMUNICATOR_PRIORITY,
      stack_buffer,
    {
      { COMMON_BLOCK, COMMON_SIZE, portMPU_REGION_READ_WRITE },
      { (void *)0x80f8000, 0x10000, portMPU_REGION_READ_WRITE }, // EEPROM access
      { 0, 0, 0 }
    }
  };

COMMON RestrictedTask communicator_task (p);

void sync_communicator (void)
{
  communicator_task.notify_give ();
}
#endif
