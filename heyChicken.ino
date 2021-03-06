#include "heyChicken.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "WiFiInfo.h"
#include <SPI.h>
#include <OneWire.h> 

//#define DEBUG

#ifdef DEBUG
 #define DEBUG_PRINT(x)     		Serial.print (x)
 #define DEBUG_PRINTDEC(x)  		Serial.print (x, DEC)
 #define DEBUG_PRINTFLOAT(x,y)  Serial.print (x, y)
 #define DEBUG_PRINTLN(x)   		Serial.println (x)
#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTDEC(x)
 #define DEBUG_PRINTFLOAT(x,y)
 #define DEBUG_PRINTLN(x) 
#endif

#if USE_UDP
// wifi
char ssid[] = WIFI_NETWORK;    // network SSID (name) 
char pwd[] = WIFI_PASSWORD;    // network password

int status = WL_IDLE_STATUS;
#endif

#if USE_UDP
// udp
WiFiUDP Udp;
unsigned int udpPort = 9999;          // local port to listen for UDP packets
IPAddress clientAddress;
const int UDP_PACKET_SIZE = 48; 
char packetBuffer[UDP_PACKET_SIZE];
char replyBuffer[UDP_PACKET_SIZE];
char currentRequest = MSG_NO_OP;
#endif

// temp 
boolean foundAllDevices = false;
byte addr[MAX_DS1820_SENSORS][8];
OneWire ds(DS18S20_PIN);    // temp sensors on digital pin 2

// light
boolean sunIsUp = false;

// pressure
float pressureExpMovingAve = 0.0;
float smoothingFactor = 0.01;
int roostChangeCount = 0;    // consecutive loops of pressure readings crossing threshold
boolean onRoost = false;

// sensor thresholds for action
int lightThreshold = 100;
int pressureThreshold = 400;
int tempHeaterOn_F = 20;
int tempHeaterOff_F = 40;

// door
int doorState = DOOR_CLOSED;
int openBumper = BUMPER_CLEAR;
int closeBumper = BUMPER_CLEAR;

// powertail
int powertailState = LOW;    // off

// main loop count
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
      DEBUG_PRINT("Can't find DS18S20 Device ID: ");
      DEBUG_PRINTLN(sensor);
      success = false;
    } else if (OneWire::crc8(addr[sensor], 7) != addr[sensor][7]) {
      DEBUG_PRINTLN( "CRC is not valid" );
      success = false;
    } else if (addr[sensor][0] != 0x28) {
      DEBUG_PRINT("Device is not a DS18S20 family device. Device ID: ");
      DEBUG_PRINTLN(sensor);
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

// obtains the temperature from one DS18S20 in degrees F as result
// returns true on success, false on sensor index out of bounds or CRC check failed on read
boolean getTemp(int sensor, float *result)
{
  boolean success = true;
  byte data[9]; 
  
  if (sensor >= MAX_DS1820_SENSORS)
  {
    DEBUG_PRINTLN("Sensor index invalid");
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
      DEBUG_PRINTLN("Data CRC failed");
      DEBUG_PRINTLN(OneWire::crc8(data, 8));
      DEBUG_PRINTLN(data[8]);
      success = false;
    } else {
      float tempRead = ((data[1] << 8) | data[0]);     //using two's compliment (???)
      *result = CtoF((tempRead / 16));
    }
  }
  return success;
}

// returns any change in heater state
CoopChange checkHeater(float coopTemp)
{
  CoopChange heaterChange = NO_CHANGE;
  if (coopTemp < tempHeaterOn_F && !powertailState) // if the coop is too cold and the heater is off, turn it on
  {
    setPowertail(HIGH);
    heaterChange = CHANGED_ON;
    DEBUG_PRINTLN("Turning heater on...");
  } else if (coopTemp > tempHeaterOff_F && powertailState) { // if the coop is too hot and the heater is on, turn it off
    setPowertail(LOW);
    heaterChange = CHANGED_OFF;
    DEBUG_PRINTLN("Turning heater off...");
  } 
  return heaterChange;
}

float CtoF(float tempCelsius)
{
  return (tempCelsius * 9 / 5 + 32);
}

#pragma mark ---photocell-----

void lightSetup()
{
  int currentLight = 0;
  getLight(&currentLight);
  sunIsUp = (currentLight > lightThreshold);
}

CoopChange checkLightChanged(int curValue)
{
  CoopChange changed = NO_CHANGE;
  if ((!sunIsUp && (curValue > lightThreshold)) || (sunIsUp && (curValue <= lightThreshold)))
  {
    sunIsUp = !sunIsUp;
    changed = (sunIsUp) ? CHANGED_ON : CHANGED_OFF;
  } 
  return changed;
}

void getLight(int *value)
{
  *value = analogRead(PHOTOCELL_PIN);
}

#pragma mark ---pressure---

 // when starting, one observation is sufficient to determine if the birds on are on the roost
void pressureSetup()
{
  int currentPressure = 0;
  getPressure(&currentPressure);
  if (currentPressure > pressureThreshold)
  {
    onRoost = true; 
  }
}

// We are using an exponential moving average (EMA) to smooth our pressure readings.
// EMAcur = factor x newValue + (1 - factor) x EMAprev
// where factor is a smoothing factor between 0 and 1
void updatePressureEMA(int newValue)
{
  if (0 == pressureExpMovingAve)
  {
    pressureExpMovingAve = float(newValue);
  } else {
    pressureExpMovingAve = smoothingFactor * float(newValue) + ((1.0 - smoothingFactor) * pressureExpMovingAve);
  }
}

void getPressure(int *value)
{
  int valueRead = 0;
  // This second call to getPressure is used to disregard bad ananlogRead readings.
  // When mutliple analogRead calls are made in close temporal proximity, 
  // the first will affect the value of the second. 
  valueRead = analogRead(PRESSURE_PIN);
  delay(500);
  valueRead = analogRead(PRESSURE_PIN);
  delay(500);
  updatePressureEMA(valueRead);
  *value = int(round(pressureExpMovingAve));
}

// In order to detect a change, the roost pressure must pass the threshold for a given period of time
/*CoopChange checkChickensOnRoost(int currentPressure)
{
  CoopChange somethingChanged = NO_CHANGE;
  if ((!onRoost && currentPressure > pressureThreshold) || (onRoost && currentPressure <= pressureThreshold))
  {
    roostChangeCount++;		// change detected
  } else {
    roostChangeCount = 0;	// if no change, the count starts over
  }
  if (roostChangeCount > ROOST_PERSISTENCE_MULT)		// change is persistent
  {
    onRoost = !onRoost;
    somethingChanged = (onRoost) ? CHANGED_ON : CHANGED_OFF;
    roostChangeCount = 0;
  }
  return somethingChanged;
}*/

// try this simple check first
CoopChange checkChickensOnRoost(int currentPressure)
{
  CoopChange somethingChanged = NO_CHANGE;
  if ((!onRoost && currentPressure > pressureThreshold) || (onRoost && currentPressure <= pressureThreshold))
  {
    onRoost = !onRoost;
    somethingChanged = (onRoost) ? CHANGED_ON : CHANGED_OFF;
  } 
  return somethingChanged;
}

#pragma mark ---door---

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
      DEBUG_PRINTLN("ERROR: both open and closed bumper are HIGH. Assuming door is open.");
    } else {
      DEBUG_PRINTLN("Door is open");
      closeTheDoor();
    }
  } else {    // door is closed as set by default
    if (BUMPER_CLEAR == digitalRead(BUMP_CLOSE_PIN))
    {
      DEBUG_PRINTLN("ERROR: both open and closed bumper are LOW. Closing door.");
      // shut the door in this case since it is technically a valid state
      closeTheDoor();  // now we are in a known state to begin
    } else {
      DEBUG_PRINTLN("Door is closed");
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
  DEBUG_PRINTLN("Stopping door");
  digitalWrite(STBY_PIN, LOW); 
}

// It is ok to close if the chickens are on roost and it is dark outside
boolean okToCloseDoor(boolean lightChanged, boolean roostChanged)
{
  boolean isOK = false;

  // might want to check pressure here too
  if (CHANGED_OFF == lightChanged /* && CHANGED_ON == roostChanged */)
  {
    isOK = true;
    DEBUG_PRINTLN("It is dark outside and the chickies are sleeping.");
  }
  return isOK;
}

void closeTheDoor()
{
  DEBUG_PRINTLN("Closing door...");
  // door starting to move, all bumpers should be clear
  openBumper = BUMPER_CLEAR;
  closeBumper = BUMPER_CLEAR;
  doorState = DOOR_CLOSING;
  move(MOTOR_SPEED, MOTOR_CLOSE_DOOR);
}

// It is ok to open if it is light outside.
// We don't care about the roost situation. 
boolean okToOpenDoor(boolean lightChanged)
{
  boolean isOK = false;
    
  if (CHANGED_ON == lightChanged)
  {
    isOK = true;
    DEBUG_PRINTLN("Looks like the sun is up!");
  }
  return isOK;
}

void openTheDoor()
{
  DEBUG_PRINTLN("Opening door...");
  // door starting to move, all bumpers should be clear
  openBumper = BUMPER_CLEAR;
  closeBumper = BUMPER_CLEAR;
  doorState = DOOR_OPENING;
  move(MOTOR_SPEED, MOTOR_OPEN_DOOR);
}

#pragma mark ---wifi---

#if USE_UDP
void wifiSetup(void)
{
  if (WiFi.status() == WL_NO_SHIELD)
  {
    DEBUG_PRINTLN("WiFi shield not present"); 
    while(true);  // don't continue if the shield is not there
  } 

  while ( status != WL_CONNECTED) 
  { 
    DEBUG_PRINT("Attempting to connect to SSID: ");
    DEBUG_PRINTLN(ssid);
    status = WiFi.begin(ssid, pwd);  // WPA/WPA2 network
    delay(10000);                    // wait 10 seconds for connection
  } 
  printWifiStatus();
#if USE_UDP
  udpSetup();
#else
  server.begin();
#endif
}

void printWifiStatus() 
{
  // print the SSID of the network you're attached to
  DEBUG_PRINT("SSID: ");
  DEBUG_PRINTLN(WiFi.SSID());

  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  DEBUG_PRINT("IP Address: ");
  DEBUG_PRINTLN(ip);

  // print the received signal strength
  long rssi = WiFi.RSSI();
  DEBUG_PRINT("signal strength (RSSI):");
  DEBUG_PRINTDEC(rssi);
  DEBUG_PRINTLN(" dBm");
}

#endif     // USE_UDP

#pragma mark ---UDP---

#if USE_UDP
void udpSetup()
{
  DEBUG_PRINTLN("\nStarting connection to UDP server...");
  Udp.begin(udpPort);
  
  // send "awake" message to server at startup
  //sendAliveMessage();
}

void sendAliveMessage()
{
  memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
  sprintf((char *)packetBuffer, "A \n");
  sendUDPPacket();
}

void handleUDP(float tempCoop, float tempRun, int light, int pressure)
{
  // read packet if present
  if ( Udp.parsePacket() ) 
  {
    DEBUG_PRINT("packet received: ");
    memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
    Udp.read(packetBuffer, UDP_PACKET_SIZE); // read the packet into the buffer
    DEBUG_PRINTLN(packetBuffer);
    currentRequest = *packetBuffer;
    DEBUG_PRINT("currentRequest: ");
    DEBUG_PRINTLN(currentRequest);
    clientAddress = Udp.remoteIP();
    DEBUG_PRINT("clientAddress: ");
    DEBUG_PRINTLN(clientAddress);
  }

  // process packet
  if (currentRequest != MSG_NO_OP)
  {
  	memset(replyBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
    switch (currentRequest)
    {
      case MSG_REQ_STATUS:
        // packet format: tempCoop | tempRun | light | pressure | heater on/off | day/night | on/off roost
        sprintf(replyBuffer, "%c %d %d %d %d %d %d %d ", MSG_STATUS, int(round(tempCoop)), int(round(tempRun)), light, pressure, 
                                                        int(powertailState), int(sunIsUp), int(onRoost));
        break;
      case MSG_REQ_TUNING:
        // packet format: type | light | pressure | heater on | heater off | smoothing (as an integer 0-100)
        sprintf(replyBuffer, "%c %d %d %d %d %d ", MSG_TUNING, lightThreshold, pressureThreshold, tempHeaterOn_F, tempHeaterOff_F, 
                                                         int(smoothingFactor * 100));
        break;
      case MSG_SET_TUNING: 
       int smoothingFactorInt = 0; 
        // packet format: type | light | pressure | heater on | heater off | smoothing (as an integer 0-100)
        sscanf(packetBuffer, "%c %d %d %d %d %d", currentRequest, &lightThreshold, &pressureThreshold, &tempHeaterOn_F, &tempHeaterOff_F, 
                                                         &smoothingFactorInt);
        smoothingFactor = float(smoothingFactorInt/100.0);
        // reply with new parameters
        sprintf(replyBuffer, "%c %d %d %d %d %d ", MSG_TUNING, lightThreshold, pressureThreshold, tempHeaterOn_F, tempHeaterOff_F, 
                                                            smoothingFactorInt);
        break;
    } 
    sendUDPPacket();    // always reply with a message
  } else if (loopCount % PERIODIC_TASKS_FREQ == 0) {	// No message received this time so let's take care of periodic tasks
  			// Send an alive "ping" to the server
  			//sendAliveMessage();
  			loopCount = 0;
  }
  currentRequest = MSG_NO_OP;
}

void sendUDPPacket()
{
	if (clientAddress)
	{
		DEBUG_PRINT("Sending UDP packet: ");
		DEBUG_PRINTLN(replyBuffer);
		Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
		Udp.write(replyBuffer, UDP_PACKET_SIZE);
		Udp.endPacket();
  } else {
  	DEBUG_PRINTLN("No client address");
  }
}
#endif      // USE_UDP

#pragma mark ---powertail---

void powertailSetup()
{
  pinMode(POWERTAIL_PIN, OUTPUT);
  setPowertail(LOW);  // make sure it is off
  powertailState = LOW;
}

void setPowertail(int state)
{
  digitalWrite(POWERTAIL_PIN, state);
  powertailState = state;
}

#pragma mark ---utility---

void readSensors(float *tempCoop, float *tempRun, int *light, int *pressure)
{
  getTemp(TEMP_COOP_INDEX, tempCoop);
  getTemp(TEMP_RUN_INDEX, tempRun);
  getPressure(pressure);
  getLight(light);
  
  DEBUG_PRINT("coop: ");
  DEBUG_PRINTFLOAT(*tempCoop, 1);
  DEBUG_PRINT("F");
  DEBUG_PRINT("\t");

  DEBUG_PRINT("run: ");
  DEBUG_PRINTFLOAT(*tempRun, 1);
  DEBUG_PRINT("F");
  DEBUG_PRINT("\t");
  
  DEBUG_PRINT("light: ");
  DEBUG_PRINTDEC(*light);
  DEBUG_PRINT("\t");

  DEBUG_PRINT("pressure: ");
  DEBUG_PRINTDEC(*pressure);
  DEBUG_PRINT("\t");

  DEBUG_PRINT("heater: ");
  DEBUG_PRINTLN(powertailState);
}

void setTunableParameter(int value, TunableParameter type)
{
  switch(type) {
    case LIGHT_THRESHOLD:
      lightThreshold = value;
      break;
    case PRESSURE_THRESHOLD:
      pressureThreshold = value;
      break;
    case TEMP_HEATER_ON:
      tempHeaterOn_F = value;
      break;
    case TEMP_HEATER_OFF:
      tempHeaterOff_F = value;
      break;
   case SMOOTHING_FACTOR:
      if (0 < value < 1) smoothingFactor = value;
      break;   
  }
}

void setSmoothingFactor(float value)
{
  
}

void errorMessage(char *msg)
{
  
}

#pragma mark ---general arduino stuff---

void setup(void) 
{
#ifdef DEBUG
  Serial.begin(9600); 
#endif
  wifiSetup();
  doorSetup();
  powertailSetup(); 
}

void loop(void) 
{
  //DEBUG_PRINTLN("Loop function called");
  
  float tempCoop = 0.0;
  float tempRun = 0.0;
  int light = 0;
  int pressure = 0;
  int originalPowertailState = powertailState;
  CoopChange movement = NO_CHANGE;  
  CoopChange heaterChanged = NO_CHANGE;
  CoopChange roostChanged = NO_CHANGE;
  CoopChange lightChanged = NO_CHANGE;
  
  loopCount++;
  
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
		//performDoorIdleTasks();
			readSensors(&tempCoop, &tempRun, &light, &pressure);
			heaterChanged = checkHeater(tempCoop);		// This will turn the heater on/off
			roostChanged = checkChickensOnRoost(pressure);
			lightChanged = checkLightChanged(light);
			
			#if USE_UDP
  			handleUDP(tempCoop, tempRun, light, pressure);
			#endif
				
      // do we need to close the door?
      if (okToCloseDoor(lightChanged, roostChanged))
      {
        closeTheDoor();
      } else {
        delay(DOOR_IDLE_DELAY_MS); 
      }
      break;
      
    case DOOR_CLOSED:
    	//performDoorIdleTasks();
			readSensors(&tempCoop, &tempRun, &light, &pressure);
			heaterChanged = checkHeater(tempCoop);		// This will turn the heater on/off
			roostChanged = checkChickensOnRoost(pressure);
			lightChanged = checkLightChanged(light);
			
			#if USE_UDP
  			handleUDP(tempCoop, tempRun, light, pressure);
			#endif
				
      // do we need to open the door?
      if (okToOpenDoor(lightChanged))
      {
        openTheDoor();
      } else {
        delay(DOOR_IDLE_DELAY_MS); 
      }
      break;
  }
  //delay(IDLE_DELAY);
}

