/** ***********************************************************************
 * @file		atmosphere.h
 * @brief		ICAO STD atmosphere calculations
 * @author		Dr. Klaus Schaefer
 * air density formula developed by Philipp Puetz
 **************************************************************************/
#ifndef APPLICATION_ATMOSPHERE_H_
#define APPLICATION_ATMOSPHERE_H_

#include "embedded_math.h"

#define RECIP_STD_DENSITY_TIMES_2 1.632f

/*! The gas constant of dry air in J/kg/K */
#define GAS_CONST_DRY_AIR	287.058f
/*! The gas constant of water vapor in J/kg/K */
#define GAS_CONST_WATER_VAPOR	461.523f
/*! The ratio of the gas constant of dry air to the gas contant of water vapor */
#define ONE_MINUS_RATIO_GAS_CONSTANTS 0.378f
/*! The offest for the conversion from degree celsius to kelvin */
#define CELSIUS_TO_KELVIN_OFFSET 273.15f

class atmosphere_t
{
public:
  atmosphere_t( float p_abs)
  : pressure ( p_abs),
    have_ambient_air_data(false)
  {}
  void set_pressure( float p_abs)
  {
    pressure = p_abs;
  }
  float get_pressure( void) const
  {
    return pressure;
  }
  float get_density( void) const
  {
    return  (1.0496346613e-5f * pressure + 0.1671546011f);
  }
  float get_negative_altitude( void) const
  {
    float tmp = 8.104381531e-4f * pressure;
    return - tmp * tmp  + 0.20867299170f * pressure - 14421.43945f;
  }
  float get_TAS_from_dynamic_pressure( float dynamic_pressure) const
  {
    return dynamic_pressure < 0.0f ? 0.0f : SQRT( 2 * dynamic_pressure / get_density());
  }
  float get_IAS_from_dynamic_pressure( float dynamic_pressure) const
  {
    return dynamic_pressure < 0.0f ? 0.0f : SQRT( dynamic_pressure * RECIP_STD_DENSITY_TIMES_2);
  }
  void set_ambient_air_data( float temperature, float humidity)
  {
    this->temperature = temperature;
    this->humidity = humidity;
    have_ambient_air_data = true;
  }
  void disregard_ambient_air_data( void)
  {
    have_ambient_air_data = false;
  }
private:
  float calculateGasConstantHumAir(
      float humidity, float pressure, float temperature);
  float calculateAirDensity(
      float humidity, float pressure, float temperature);
  float calculateSaturationVaporPressure(float temp);
  bool have_ambient_air_data;
  float pressure;
  float temperature;
  float humidity;
  float density;
};

#endif /* APPLICATION_ATMOSPHERE_H_ */
