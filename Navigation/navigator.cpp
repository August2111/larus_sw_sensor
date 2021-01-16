#include <navigator.h>

// to be called at 100 Hz
void navigator_t::update_IMU (
    const float3vector &acc,
    const float3vector &mag,
    const float3vector &gyro)
{
  ins.update_diff_GNSS (
	  gyro, acc, mag,
	  GNSS_acceleration,
	  GNSS_heading);

  ins_magnetic.update_compass(
	  gyro, acc, mag,
	  GNSS_acceleration);

  true_airspeed[NORTH] = ins.get_north () * TAS;
  true_airspeed[EAST]  = ins.get_east ()  * TAS;
  true_airspeed[DOWN]  = ins.get_down ()  * TAS; // todo: do we need this one ?

  flight_observer.update (
      GNSS_velocity,
      GNSS_acceleration,
      ins.get_nav_acceleration (),
      true_airspeed,
      GNSS_altitude,
      TAS);
}
#if 1
// to be called at 10 Hz
void navigator_t::update_GNSS (const coordinates_t &coordinates)
{
  GNSS_velocity 	= coordinates.velocity;
  GNSS_acceleration	= coordinates.acceleration;
#if USE_DIFF_GNSS == 1
  GNSS_heading 		= coordinates.relPosHeading;
#endif
  GNSS_altitude 	= coordinates.position.e[DOWN]; // negative altitude
  GNSS_speed 		= coordinates.speed_motion;

  vario_integrator.update (flight_observer.get_vario_INS(),
			   ins.get_euler ().y,
			   ins.get_circling_state ());
}
#else
// to be called at 10 Hz
void navigator_t::update_GNSS_old (const coordinates_t &coordinates, float3vector acceleration)
{
  GNSS_velocity 	= coordinates.velocity;
  GNSS_acceleration	= acceleration;
  GNSS_heading 		= coordinates.relPosHeading;
  GNSS_altitude 	= coordinates.position.e[DOWN]; // negative altitude
  GNSS_speed 		= coordinates.speed_motion;

  vario_integrator.update (flight_observer.get_vario_INS(),
			   ins.get_euler ().y,
			   ins.get_circling_state ());
}
#endif

void navigator_t::report_data(output_data_t &d)
{
    d.TAS 			= TAS;
    d.IAS 			= IAS;

    d.euler			= ins.get_euler();
    d.q				= ins.attitude;

    d.euler_magnetic		= ins_magnetic.get_euler();
    d.q_magnetic		= ins_magnetic.attitude;

    d.vario			= flight_observer.get_vario_INS();
    d.integrator_vario		= vario_integrator.get_value();
    d.vario_uncompensated 	= flight_observer.get_vario_uncompensated();

    d.wind			= flight_observer.get_wind();

    d.speed_compensation_TAS 	= flight_observer.get_speed_compensation_TAS();
    d.speed_compensation_INS 	= flight_observer.get_speed_compensation_INS();
    d.effective_vertical_acceleration = flight_observer.get_effective_vertical_acceleration();

    d.circle_mode 		= ins.get_circling_state();
    d.gyro_correction		= ins.get_gyro_correction();
    d.nav_acceleration_gnss 	= ins.get_nav_acceleration();
    d.nav_acceleration_mag 	= ins.get_nav_acceleration();
    d.nav_induction_gnss 	= ins.get_nav_induction();
    d.nav_induction_mag 	= ins.get_nav_induction();
}
