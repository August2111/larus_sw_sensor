/**
 * @file    system_configuration.h
 * @brief   system-wide tweaks
 * @author  Dr. Klaus Schaefer klaus.schaefer@h-da.de
 */
#ifndef SRC_SYSTEM_CONFIGURATION_H_
#define SRC_SYSTEM_CONFIGURATION_H_

#define RUN_MTi_1_MODULE 	1
#define RUN_MS5611_MODULE 	1
#define RUN_CHIPSENS_MODULE 	1
#define RUN_PITOT_MODULE 	1
#define RUN_CAN_TESTER		0
#define ACTIVATE_USB		0

#define USE_DIFF_GNSS		1
#define UART3_LED_STATUS	0
#define UART4_LED_STATUS	0
#define uSD_LED_STATUS		1
#define RUN_GNSS_UPDATE_WITHOUT_FIX 1

#define RUN_DATA_LOGGER		1
#define LOG_OBSERVATIONS	0 // log only IMU + pressures
#define LOG_COORDINATES		0 // log also GNSS data
#define LOG_OUTPUT_DATA		1 // logging all inclusive
#define OUTFILE "LOG.F38"

#define RUN_OFFLINE_CALCULATION 0
#define RUN_COMMUNICATOR	1
<<<<<<< HEAD

#define RUN_CAN_OUTPUT		0
=======
>>>>>>> branch 'sensor_prototype' of https://github.com/MaxBaex/the_soar_instrument.git

#define RUN_SPI_TESTER		0
#define RUN_SDIO_TEST		0

#define MTI_PRIORITY		STANDARD_TASK_PRIORITY + 2

#define MS5611_PRIORITY		STANDARD_TASK_PRIORITY + 1
#define L3GD20_PRIORITY		STANDARD_TASK_PRIORITY + 1
#define PITOT_PRIORITY		STANDARD_TASK_PRIORITY + 1

<<<<<<< HEAD
#define COMMUNICATOR_PRIORITY	STANDARD_TASK_PRIORITY + 1

#define LOGGER_PRIORITY		STANDARD_TASK_PRIORITY + 3
#define CAN_PRIORITY		STANDARD_TASK_PRIORITY
=======
#define LOGGER_PRIORITY		STANDARD_TASK_PRIORITY
>>>>>>> branch 'sensor_prototype' of https://github.com/MaxBaex/the_soar_instrument.git

#endif /* SRC_SYSTEM_CONFIGURATION_H_ */
