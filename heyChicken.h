#ifndef _HEYCHICKEN_H
#define _HEYCHICKEN_H

#define OPERATE_DOOR        false
#define TALK_TO_SERVER      true

// pin assignments
#define DS18S20_PIN         2     // temperature
#define PHOTOCELL_PIN       A0
#define PRESSURE_PIN        A2
#define STBY_PIN            4     // motor standby
#define PWMA_PIN            3     // motor speed control 
#define AIN1_PIN            9     // motor direction
#define AIN2_PIN            8     // motor direction
#define BUMP_OPEN_PIN       A4 
#define BUMP_CLOSE_PIN      A5
#define POWERTAIL_PIN       6

#define MAX_DS1820_SENSORS  2

// sensor thresholds for action
#define LIGHT_THRESHOLD       100
#define PRESSURE_THRESHOLD    400
#define TEMPF_HEATER_ON       20
#define TEMPF_HEATER_OFF      40

enum CoopChange {
  NO_CHANGE,
  CHANGED_OFF,
  CHANGED_ON
};

// for exponential moving average smoothing
#define SMOOTHING_FACTOR      0.01

// door state machine
enum DoorState {
  DOOR_OPEN,
  DOOR_CLOSED,
  DOOR_OPENING,
  DOOR_CLOSING
};

#define BUMPER_TRIGGERED   LOW
#define BUMPER_CLEAR       HIGH 

#define MOTOR_SPEED       255  // go as fast as you can!

// motor direction: swap these values if the door is going the wrong direction
#define MOTOR_CLOSE_DOOR  0
#define MOTOR_OPEN_DOOR   1

// loop intervals
#define DOOR_IDLE_DELAY_MS        1000
#define DOOR_MOVING_DELAY_MS      250
#define PRINT_SENSORS_FREQ        5      // multiples of DOOR_IDLE_DELAY_MS
#define IDLE_DELAY                5000

#endif /* _HEYCHICKEN_H */
