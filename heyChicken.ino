
#include <SPI.h>
#include <OneWire.h> 

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

// temp 
#define MAX_DS1820_SENSORS  2

boolean foundAllDevices = false;
byte addr[MAX_DS1820_SENSORS][8];

OneWire ds(DS18S20_PIN);    // temp sensors on digital pin 2

// light & pressure
#define LIGHT_THRESHOLD       100
#define PRESSURE_THRESHOLD    400

// door state machine
#define DOOR_OPEN          0
#define DOOR_CLOSED        1
#define DOOR_OPENING       2
#define DOOR_CLOSING       3

int doorState = DOOR_CLOSED;

// door stop bumpers
#define BUMPER_TRIGGERED   LOW
#define BUMPER_CLEAR       HIGH 

int openBumper = BUMPER_CLEAR;
int closeBumper = BUMPER_CLEAR;

#define MOTOR_SPEED       255  // go as fast as you can!
// motor direction: swap these values if the door is going the wrong direction
#define MOTOR_CLOSE_DOOR  0
#define MOTOR_OPEN_DOOR   1

// powertail
int powertailState = LOW;    // off

// loop intervals
#define DOOR_IDLE_DELAY_MS        1000
#define DOOR_MOVING_DELAY_MS      250
#define PRINT_SENSORS_FREQ        5      // multiples of DOOR_IDLE_DELAY_MS

int loopCount = 0;

///////////    temperature sensors    ///////////

// Looks for devices and checks the address array CRC, as well as the device type
// If the device address info is obtained and the CRC and device type are ok for all devices, returns true
// Otherwise, returns false and resets the search for a subsequent retry
boolean findDS18S20Devices(void)
{
  boolean success = true;
  for (int sensor = 0; sensor < MAX_DS1820_SENSORS; sensor++)
  {
    if (!ds.search(addr[sensor])) 
    {
      Serial.print("Can't find DS18S20 Device ID: ");
      Serial.println(sensor);
      success = false;
    } else if (OneWire::crc8(addr[sensor], 7) != addr[sensor][7]) {
      Serial.println( "CRC is not valid" );
      success = false;
    } else if (addr[sensor][0] != 0x28) {
      Serial.print("Device is not a DS18S20 family device. Device ID: ");
      Serial.println(sensor);
      success = false;
    }
    if (!success)
    {
      //reset search for next try
      ds.reset_search();
      delay(250);
      break;
    }
  }
  return success;
}

// obtains the temperature from one DS18S20 in DEG Celsius as result
// returns true on success, false on sensor index out of bounds or CRC check failed on read
boolean getTemp(int sensor, float *result)
{
  boolean success = true;
  byte data[9]; 
  
  if (sensor >= MAX_DS1820_SENSORS)
  {
    Serial.println("Sensor index invalid");
    success = false;
  } else {
    ds.reset();
    ds.select(addr[sensor]);
    ds.write(0x44,1);    // start conversion

    //delay(1000);        // uncomment for parasitic power

    ds.reset();
    ds.select(addr[sensor]);        
    ds.write(0xBE);        // Read Scratchpad

    for (int i = 0; i < 9; i++)
    {                        
      data[i] = ds.read();
    }
    
    // CRC check on read data
    if (OneWire::crc8(data, 8) != data[8])
    {
      Serial.println("Data CRC failed");
      Serial.println(OneWire::crc8(data, 8));
      Serial.println(data[8]);
      success = false;
    } else {
      float tempRead = ((data[1] << 8) | data[0]);     //using two's compliment (???)
      *result = (tempRead / 16);
    }
  }
  return success;
}

///////////    photocell    ///////////

void getLight(int *value)
{
  *value = analogRead(PHOTOCELL_PIN);
}

///////////    pressure    ///////////

void getPressure(int *value)
{
  // This second call to getPressure is used to disregard bad ananlogRead readings.
  // When mutliple analogRead calls are made in close temporal proximity, 
  // the first will affect the value of the second. 
  *value = analogRead(PRESSURE_PIN);
   delay(500);
   *value = analogRead(PRESSURE_PIN);
   delay(500);
}

void readSensors(float *tempCoop, float *tempRun, int *light, int *pressure)
{
  getTemp(0, tempCoop);
  getTemp(1, tempRun);
  getPressure(pressure);
  getLight(light);
}

float CtoF(float tempCelsius)
{
  return (tempCelsius * 9 / 5 + 32);
}

void printSensors()
{
  float tempCoop = 0.0;
  float tempRun = 0.0;
  float tempF = 0.0;
  int light = 0;
  int pressure = 0;
  
  readSensors(&tempCoop, &tempRun, &light, &pressure);
  
  Serial.print("coop: ");
  tempF = CtoF(tempCoop);
  Serial.print(tempF, 1);
  Serial.print("F");
  Serial.print("\t");

  Serial.print("run: ");
  tempF = CtoF(tempRun);
  Serial.print(tempF, 1);
  Serial.print("F");
  Serial.print("\t");
  
  Serial.print("light: ");
  Serial.print(light);
  Serial.print("\t");

  Serial.print("pressure: ");
  Serial.println(pressure);
}

///////////    door    ///////////
void doorSetup()
{
  // set the door state, which we assume is not moving during setup
  pinMode(STBY_PIN, OUTPUT);
  pinMode(PWMA_PIN, OUTPUT);
  pinMode(AIN1_PIN, OUTPUT);
  pinMode(AIN2_PIN, OUTPUT);
  pinMode(BUMP_OPEN_PIN, INPUT_PULLUP);
  pinMode(BUMP_CLOSE_PIN, INPUT_PULLUP);
  
  if (BUMPER_TRIGGERED == digitalRead(BUMP_OPEN_PIN))
  {
    doorState = DOOR_OPEN;
    // this should not happen
    if (BUMPER_TRIGGERED == digitalRead(BUMP_CLOSE_PIN))
    {
      Serial.println("ERROR: both open and closed bumper are HIGH. Assuming door is open.");
    } else {
      Serial.println("Door is open");
      closeTheDoor();
    }
  } else {    // door is closed as set by default
    if (BUMPER_CLEAR == digitalRead(BUMP_CLOSE_PIN))
    {
      Serial.println("ERROR: both open and closed bumper are LOW. Closing door.");
      // shut the door in this case since it is technically a valid state
      closeTheDoor();  // now we are in a known state to begin
    } else {
      Serial.println("Door is closed");
    }
  }
}

void move(int speed, int direction)
{
//Move specific motor at speed and direction
//speed: 0 is off, and 255 is full speed

  digitalWrite(STBY_PIN, HIGH); //disable standby

  if (MOTOR_OPEN_DOOR == direction)
  {
    digitalWrite(AIN1_PIN, LOW);
    digitalWrite(AIN2_PIN, HIGH);
  } else {
    digitalWrite(AIN1_PIN, HIGH);
    digitalWrite(AIN2_PIN, LOW);
  }
  analogWrite(PWMA_PIN, speed);
}

void stopTheDoor()
{
  Serial.println("Stopping door");
  digitalWrite(STBY_PIN, LOW); 
}

// It is ok to close if the chickens are on roost and it is dark outside
boolean okToCloseDoor()
{
  boolean isOK = false;
  int pressure = 0;
  int light = 0;
  
  getPressure(&pressure); 
  getLight(&light);
  
  if (pressure > PRESSURE_THRESHOLD && light < LIGHT_THRESHOLD)
  {
    isOK = true;
    togglePowertail();  // turn off the sun
    Serial.print("It is dark outside and the chickies are sleeping.\nLight: ");
    Serial.print(light);
    Serial.print("\tpressure: ");
    Serial.println(pressure);
  }
  return isOK;
}

void closeTheDoor()
{
  Serial.println("Closing door...");
  // door starting to move, all bumpers should be clear
  openBumper = BUMPER_CLEAR;
  closeBumper = BUMPER_CLEAR;
  doorState = DOOR_CLOSING;
  move(MOTOR_SPEED, MOTOR_CLOSE_DOOR);
}

// It is ok to open if it is light outside.
// We don't care about the roost situation. 
boolean okToOpenDoor()
{
  boolean isOK = false;
  int light = 0;
  
  getLight(&light);
  
  if (light > LIGHT_THRESHOLD)
  {
    isOK = true;
    togglePowertail();  // turn on the sun
    Serial.print("Looks like the sun is up! Light is ");
    Serial.println(light);
  }
  return isOK;
}

void openTheDoor()
{
  Serial.println("Opening door...");
  // door starting to move, all bumpers should be clear
  openBumper = BUMPER_CLEAR;
  closeBumper = BUMPER_CLEAR;
  doorState = DOOR_OPENING;
  move(MOTOR_SPEED, MOTOR_OPEN_DOOR);
}

///////////    powertail    ///////////
void powertailSetup()
{
  pinMode(POWERTAIL_PIN, OUTPUT);
  digitalWrite(POWERTAIL_PIN, LOW);  // make sure it is off
}

void togglePowertail()
{
  powertailState = !powertailState;
  digitalWrite(POWERTAIL_PIN, powertailState);
}

///////////    utility    ///////////

void errorMessage(char *msg)
{
  
}

///////////    general arduino stuff    ///////////

void setup(void) 
{
  Serial.begin(9600);
  powertailSetup(); 
  doorSetup();
}

void performDoorIdleTasks()
{
  if (loopCount++ % PRINT_SENSORS_FREQ == 0)
  {
    printSensors();
  }
}

void loop(void) 
{
//  Serial.print("Door state: ");
//  Serial.println(doorState);
  
  // make sure temperature sensors are there
  // don't do this if the door is moving
  if (doorState < 2 && !foundAllDevices)
  {
    if (!(foundAllDevices = findDS18S20Devices())) 
    {
      delay(5000);
      return;
    }
  }
  
  switch (doorState)
  {
    // DOOR_OPENING/DOOR_CLOSING: door is moving, need to poll frequently for bumper
    case DOOR_OPENING:
      if (BUMPER_TRIGGERED == digitalRead(BUMP_OPEN_PIN)) 
      {
        openBumper = BUMPER_TRIGGERED;
        stopTheDoor();
        doorState = DOOR_OPEN;
        delay(1000);
      } else {
        delay(DOOR_MOVING_DELAY_MS);
        return;
      }
      break;
    case DOOR_CLOSING:
      if (BUMPER_TRIGGERED == digitalRead(BUMP_CLOSE_PIN)) 
      {
        closeBumper = BUMPER_TRIGGERED;
        stopTheDoor();
        doorState = DOOR_CLOSED;
        delay(1000);
      } else {
        delay(DOOR_MOVING_DELAY_MS);
        return;
      }      
      break;
     
     //  DOOR_OPEN/DOOR_CLOSED: Idle state, poll less frequently and take care of periodic tasks
    case DOOR_OPEN:
      // do we need to close the door?
      if (okToCloseDoor())
      {
        closeTheDoor();
      } else {
        performDoorIdleTasks();
        delay(DOOR_IDLE_DELAY_MS); 
      }
      break;
      
    case DOOR_CLOSED:
      // do we need to open the door?
      if (okToOpenDoor())
      {
        openTheDoor();
      } else {
        performDoorIdleTasks();
        delay(DOOR_IDLE_DELAY_MS); 
      }
      break;
  }
}

