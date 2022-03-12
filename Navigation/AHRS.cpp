/** ***********************************************************************
 * @file		AHRS.cpp
 * @brief		AHRS Implementation: maintain aircraft attitude
 * @author		Dr. Klaus Schaefer
 **************************************************************************/

#include <AHRS.h>
#include "system_configuration.h"
#include "my_assert.h"
#include "GNSS.h"
#include "embedded_memory.h"

#define P_GAIN 0.03f			//!< Attitude controller: proportional gain
#define I_GAIN 0.00006f 		//!< Attitude controller: integral gain
#define H_GAIN 38.0f			//!< Attitude controller: horizontal gain
#define M_H_GAIN 10.0f			//!< Attitude controller: horizontal gain magnetic
#define CROSS_GAIN 0.05f		//!< Attitude controller: cross-product gain

#define HIGH_TURN_RATE 0.15 		//!< turn rate high limit
#define LOW_TURN_RATE  0.0707 		//!< turn rate low limit

/**
 * @brief initial attitude setup from observables
 */
void
AHRS_type::attitude_setup (const float3vector &acceleration,
			   const float3vector &mag)
{
  float3vector north, east, down;

  float3vector induction;
  if( compass_calibration.isCalibrationDone()) // use calibration if available
    induction = compass_calibration.calibrate(mag);
  else
    induction = mag;

  down = acceleration;

  down.negate ();
  down.normalize ();

  north = induction; // deviation neglected here
  north.normalize ();

  // setup world coordinate system
  east = down.vector_multiply (north);
  east.normalize ();
  north = east.vector_multiply (down);
  north.normalize ();

  // create rotation matrix from unity direction vectors
  float fcoordinates[] =
    { 	north.e[0], north.e[1], north.e[2],
	east.e[0], east.e[1], east.e[2],
	down.e[0], down.e[1], down.e[2] };

  float3matrix coordinates (fcoordinates);
  attitude.from_rotation_matrix (coordinates);
  attitude.get_rotation_matrix (body2nav);
  body2nav.transpose (nav2body);
  euler = attitude;
}

/**
 * @brief  decide about circling state
 */
circle_state_t
AHRS_type::update_circling_state ()
{
#if DISABLE_CIRCLING_STATE == 1
  return STRAIGHT_FLIGHT;
#else
  float turn_rate_abs = abs (turn_rate);

  if (circling_counter < CIRCLE_LIMIT)
    if (turn_rate_abs > HIGH_TURN_RATE)
      ++circling_counter;

  if (circling_counter > 0)
    if (turn_rate_abs < LOW_TURN_RATE)
      --circling_counter;

  if (circling_counter == 0)
    circle_state = STRAIGHT_FLIGHT;
  else if (circling_counter == CIRCLE_LIMIT)
    circle_state = CIRCLING;
  else
    circle_state = TRANSITION;

  return circle_state;
#endif
}

void
AHRS_type::feed_compass_calibration (const float3vector &mag)
{
  float3vector expected_induction = nav2body * expected_nav_induction;

  for (unsigned i = 0; i < 3; ++i)
    mag_calibrator[i].add_value (expected_induction.e[i], mag.e[i]);
}

AHRS_type::AHRS_type (float sampling_time)
:
  Ts(sampling_time),
  Ts_div_2 (sampling_time / 2.0f),
  gyro_integrator({0}),
  circling_counter(0),
  slip_angle_averager( ANGLE_F_BY_FS),
  nick_angle_averager( ANGLE_F_BY_FS),
  G_load_averager(     G_LOAD_F_BY_FS),
  antenna_DOWN_correction(  configuration( ANT_SLAVE_DOWN)  / configuration( ANT_BASELENGTH)),
  antenna_RIGHT_correction( configuration( ANT_SLAVE_RIGHT) / configuration( ANT_BASELENGTH))
{
  float inclination=configuration(INCLINATION);
  float declination=configuration(DECLINATION);

  expected_nav_induction[NORTH] = COS( inclination);
  expected_nav_induction[EAST]  = COS( inclination) * SIN( declination);
  expected_nav_induction[DOWN]  = SIN( inclination);

  compass_calibration.read_from_EEPROM();
}

void
AHRS_type::update (const float3vector &gyro,
		   const float3vector &acc,
		   const float3vector &mag,
		   const float3vector &GNSS_acceleration,
		   float GNSS_heading,
		   bool GNSS_heading_valid)
{
  if( GNSS_heading_valid)
    update_diff_GNSS (gyro, acc, mag, GNSS_acceleration, GNSS_heading);
  else
    update_compass(gyro, acc, mag, GNSS_acceleration);
}

/**
 * @brief  generic update of AHRS
 *
 * Side-effect: create rotation matrices, NAV-acceleration, NAV-induction
 */
void
AHRS_type::update_attitude ( const float3vector &acc,
			     const float3vector &gyro,
			     const float3vector &mag)
{
  attitude.rotate (gyro.e[ROLL] * Ts_div_2,
		   gyro.e[NICK] * Ts_div_2,
		   gyro.e[YAW]  * Ts_div_2);

  attitude.normalize ();

  attitude.get_rotation_matrix (body2nav);
  body2nav.transpose (nav2body);

  acceleration_nav_frame = body2nav * acc;
  induction_nav_frame 	 = body2nav * mag;
  euler = attitude;

  float3vector nav_rotation;
  nav_rotation = body2nav * gyro;
  turn_rate = nav_rotation[DOWN];

  slip_angle_averager.respond( ATAN2( -acc.e[RIGHT], -acc.e[DOWN]));
  nick_angle_averager.respond( ATAN2( +acc.e[FRONT], -acc.e[DOWN]));
  G_load_averager.respond( acc.abs());
}

/**
 * @brief  update attitude from IMU data D-GNSS compass
 */
void
AHRS_type::update_diff_GNSS (const float3vector &gyro,
			     const float3vector &acc,
			     const float3vector &mag_sensor,
			     const float3vector &GNSS_acceleration,
			     float GNSS_heading)
{
  circle_state_t old_circle_state = circle_state;
  update_circling_state ();

  float3vector mag;
  if( compass_calibration.isCalibrationDone()) // use calibration if available
      mag = compass_calibration.calibrate(mag_sensor);
  else
      mag = mag_sensor;

  float3vector nav_acceleration = body2nav * acc;

  float heading_gnss_work = GNSS_heading	// correct for antenna alignment
      + antenna_DOWN_correction  * SIN (euler.r)
      - antenna_RIGHT_correction * COS (euler.r);

  heading_gnss_work = heading_gnss_work - euler.y; // = heading difference D-GNSS - AHRS

  if (heading_gnss_work > M_PI_F) // map into { -PI PI}
    heading_gnss_work -= 2.0f * M_PI_F;
  if (heading_gnss_work < -M_PI_F)
    heading_gnss_work += 2.0f * M_PI_F;

  nav_correction[NORTH] = - nav_acceleration.e[EAST]  + GNSS_acceleration.e[EAST];
  nav_correction[EAST]  = + nav_acceleration.e[NORTH] - GNSS_acceleration.e[NORTH];

  if( USE_CROSS_ACCELERATION_WHILE_CIRCLING && (circle_state == CIRCLING)) // heading correction using acceleration cross product GNSS * INS
    {
      float cross_correction = // vector cross product GNSS-acc und INS-acc -> heading error
	   + nav_acceleration.e[NORTH] * GNSS_acceleration.e[EAST]
	   - nav_acceleration.e[EAST]  * GNSS_acceleration.e[NORTH];

      nav_correction[DOWN] = cross_correction * CROSS_GAIN; // no MAG or D-GNSS use here !
    }
  else
      nav_correction[DOWN]  =   heading_gnss_work * H_GAIN;

  gyro_correction = nav2body * nav_correction;
  gyro_correction *= P_GAIN;

  if (circle_state == STRAIGHT_FLIGHT)
      gyro_integrator += gyro_correction; // update integrator

  gyro_correction = gyro_correction + gyro_integrator * I_GAIN;
  update_attitude (acc, gyro + gyro_correction, mag);

  // only here we get fresh magnetic entropy
  // and: wait for low control loop error
  if ( (circle_state == CIRCLING) && ( nav_correction.abs() < 5.0f)) // value 5.0 observed from flight data
      feed_compass_calibration (mag_sensor);

  if( ( old_circle_state == CIRCLING) && (circle_state == TRANSITION))
    {
    compass_calibration.set_calibration( mag_calibrator, 'S', (turn_rate > 0.0f), true);
    handle_magnetic_calibration();
    }
}

/**
 * @brief  update attitude from IMU data and magnetometer
 */
void
AHRS_type::update_compass (const float3vector &gyro, const float3vector &acc,
			   const float3vector &mag_sensor,
			   const float3vector &GNSS_acceleration)
{
  float3vector mag;
  if (compass_calibration.isCalibrationDone ()) // use calibration if available
      mag = compass_calibration.calibrate (mag_sensor);
  else
      mag = mag_sensor;

  float3vector nav_acceleration = body2nav * acc;
  float3vector nav_induction    = body2nav * mag;

  // calculate horizontal leveling error
  nav_correction[NORTH] = -nav_acceleration.e[EAST]  + GNSS_acceleration.e[EAST];
  nav_correction[EAST]  = +nav_acceleration.e[NORTH] - GNSS_acceleration.e[NORTH];

  // *******************************************************************************************************
  // calculate heading error depending on the present circling state
  // on state changes handle MAG auto calibration

  circle_state_t old_circle_state = circle_state;
  update_circling_state ();

  if (isnan( GNSS_acceleration.e[NORTH])) // no GNSS fix, fixme todo remove this section

    // just keep gyro offsets but do not calculate correction
      gyro_correction = gyro_integrator * I_GAIN;

  else
    {
      switch ( circle_state)
	{
	case STRAIGHT_FLIGHT:
	case TRANSITION:
	  {
	    // todo hier fehlt magnet-modell erde
	    float mag_correction =
		+ nav_induction[NORTH] * expected_nav_induction[EAST]
		- nav_induction[EAST]  * expected_nav_induction[NORTH];

#if 0 // thsi calculation is way too complicated
	    mag_correction /= SQRT(
		(SQR( nav_induction[NORTH]) + SQR(nav_induction[EAST])) *
		(SQR( expected_nav_induction[NORTH])+SQR( expected_nav_induction[EAST]))
		);
#endif
	    // todo needs to be uptdated if inclination is changed !
	    mag_correction *= 4.0f; // normalize by vector projection magnitude

	    nav_correction[DOWN] = mag_correction * M_H_GAIN;
	    gyro_correction = nav2body * nav_correction;
	    gyro_correction *= P_GAIN;
	    gyro_integrator += gyro_correction; // update integrator
	  }
	  break;
	  // *******************************************************************************************************
	case CIRCLING:
	  {
	    float cross_correction = // vector cross product GNSS-acc und INS-acc -> heading error
		+ nav_acceleration.e[NORTH] * GNSS_acceleration.e[EAST]
		- nav_acceleration.e[EAST]  * GNSS_acceleration.e[NORTH];

	    nav_correction[DOWN] = cross_correction * CROSS_GAIN; // no MAG or D-GNSS use here !
	    gyro_correction = nav2body * nav_correction;
	    gyro_correction *= P_GAIN;
	    feed_compass_calibration (mag_sensor);
	  }
	  break;
	default:
	  ASSERT(0);
	  break;
	}

      gyro_correction = gyro_correction + gyro_integrator * I_GAIN;
    }

  // feed quaternion update with corrected sensor readings
  update_attitude(acc, gyro + gyro_correction, mag);

  // only here we get fresh magnetic entropy
  // and: wait for low control loop error
  if ( (circle_state == CIRCLING) && ( nav_correction.abs() < 5.0f)) // value 5.0 observed from flight data
      feed_compass_calibration (mag_sensor);

  if( ( old_circle_state == CIRCLING) && (circle_state == TRANSITION))
    {
    compass_calibration.set_calibration( mag_calibrator, 'M', (turn_rate > 0.0f), true);
    handle_magnetic_calibration();
    }
}

void AHRS_type::handle_magnetic_calibration (void) const
{
  if( false == compass_calibration.isCalibrationDone())
    return;

  // make calibration permanent if precision improved or values have changed significantly
  if( ( SQRT( compass_calibration.get_variance_average()) < configuration(MAG_STD_DEVIATION) )
      ||
      (       compass_calibration.parameters_changed_significantly())
      )
    {
      lock_EEPROM( false);
      compass_calibration.write_into_EEPROM();
      lock_EEPROM( true);
    }
}
