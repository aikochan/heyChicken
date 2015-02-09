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

enum CoopChange {
  NO_CHANGE,
  CHANGED_OFF,
  CHANGED_ON
};

enum ThresholdType {
  LIGHT_THRESHOLD,
  PRESSURE_THRESHOLD,
  TEMP_HEATER_ON,
  TEMP_HEATER_OFF
};

// door state machine
enum DoorState {
  DOOR_OPEN,
  DOOR_CLOSED,
  DOOR_OPENING,
  DOOR_CLOSING
};

// UDP message types
enum MsgType {
  MSG_ALIVE = 'A',
  MSG_REQ_STATUS = 'R',
  MSG_STATUS = 'S',
  MSG_THRESHOLD = 'T',
  MSG_DOOR = 'D',
  MSG_HEAT = 'H',
  MSG_ERROR = 'E',
  MSG_NO_OP = 'N'
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
