#include "heyChicken.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include "WiFiInfo.h"
#include <SPI.h>
#include <OneWire.h> 


#if USE_WEB_SERVER || USE_UDP
// wifi
char ssid[] = WIFI_NETWORK;    // network SSID (name) 
char pwd[] = WIFI_PASSWORD;    // network password

int status = WL_IDLE_STATUS;
#endif

#if USE_UDP
// udp
unsigned int udpPort = 9999;          // local port to listen for UDP packets
IPAddress serverAddress(SERVER_IPV4_BYTE0, SERVER_IPV4_BYTE1, SERVER_IPV4_BYTE2, SERVER_IPV4_BYTE3);
const int UDP_PACKET_SIZE = 48; 
byte packetBuffer[ UDP_PACKET_SIZE];    //buffer to hold incoming and outgoing packets
WiFiUDP Udp;
char currentRequest = MSG_NO_OP;
#endif

#if USE_WEB_SERVER
// web server
WiFiServer server(80);
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

// obtains the temperature from one DS18S20 in degrees F as result
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
    Serial.println("Turning heater on...");
  } else if (coopTemp > tempHeaterOn_F && powertailState) { // if the coop is too hot and the heater is on, turn it off
    setPowertail(LOW);
    heaterChange = CHANGED_OFF;
    Serial.println("Turning heater off...");
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

///////////    pressure    ///////////
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
boolean okToCloseDoor(boolean lightChanged, boolean roostChanged)
{
  boolean isOK = false;

  // might want to check pressure here too
  if (CHANGED_OFF == lightChanged /* && CHANGED_ON == roostChanged */)
  {
    isOK = true;
    Serial.println("It is dark outside and the chickies are sleeping.");
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
  
  if (light > lightThreshold)
  {
    isOK = true;
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

#pragma mark ---wifi---

#if USE_WEB_SERVER || USE_UDP
void wifiSetup(void)
{
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println("WiFi shield not present"); 
    while(true);  // don't continue if the shield is not there
  } 

  while ( status != WL_CONNECTED) 
  { 
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
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
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}

#endif     // USE_WEB_SERVER || USE_UDP

#pragma mark ---UDP---

#if USE_UDP
void udpSetup()
{
  Serial.println("\nStarting connection to UDP server...");
  Udp.begin(udpPort);
  
  // send "awake" message to server at startup
  sendAliveMessage();
}

void sendAliveMessage()
{
  memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
  sprintf((char *)packetBuffer, "A \n");
  sendUDPPacket();
}

void handleUDP(float tempCoop, float tempRun, int light, int pressure, CoopChange heaterChange, 
               CoopChange lightChanged, CoopChange roostChanged)
{
  // read packet if present
  if ( Udp.parsePacket() ) 
  {
    Serial.println("packet received");
    Udp.read(packetBuffer, UDP_PACKET_SIZE); // read the packet into the buffer
    Serial.println((char *)packetBuffer);
    currentRequest = *(char *)packetBuffer;
    Serial.println(currentRequest);
  }

  // process packet
  if (currentRequest != MSG_NO_OP)
  {
    switch (currentRequest)
    {
      case MSG_REQ_STATUS:
        // packet format: tempCoop | tempRun | light | pressure | heater on/off | heater changed |
        //                light changed | roost changed
        memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
        sprintf((char *)packetBuffer, "%c %d %d %d %d %s %d %d %d ", MSG_STATUS, int(round(tempCoop)), int(round(tempRun)), light, pressure, 
                                                           (powertailState ? "on":"off"), heaterChange, lightChanged, roostChanged);
        break;
      case MSG_REQ_TUNING:
        // packet format: type | light | pressure | heater on | heater off | smoothing (as an integer 0-100)
        memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
        sprintf((char *)packetBuffer, "%c %d %d %d %d %d ", MSG_TUNING, lightThreshold, pressureThreshold, tempHeaterOn_F, tempHeaterOff_F, 
                                                         int(smoothingFactor * 100));
        break;
      case MSG_SET_TUNING: 
       int smoothingFactorInt = 0; 
        // packet format: type | light | pressure | heater on | heater off | smoothing (as an integer 0-100)
        sscanf((char *)packetBuffer, "%c %d %d %d %d %d", currentRequest, &lightThreshold, &pressureThreshold, &tempHeaterOn_F, &tempHeaterOff_F, 
                                                         &smoothingFactorInt);
        smoothingFactor = float(smoothingFactorInt/100.0);
        // reply with new parameters
        memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
        sprintf((char *)packetBuffer, "%c %d %d %d %d %d ", MSG_TUNING, lightThreshold, pressureThreshold, tempHeaterOn_F, tempHeaterOff_F, 
                                                            smoothingFactorInt);
        break;
    } 
    sendUDPPacket();    // always reply with a message
  } else if (loopCount % PERIODIC_TASKS_FREQ == 0) {	// No message received this time so let's take care of periodic tasks
  			// Send an alive "ping" to the server
  			sendAliveMessage();
  			loopCount = 0;
  }
  currentRequest = MSG_NO_OP;
}

void sendUDPPacket()
{
  Serial.print("Sending UDP packet: ");
  Serial.println((char *)packetBuffer);
  Udp.beginPacket(serverAddress, udpPort);
  Udp.write(packetBuffer, UDP_PACKET_SIZE);
  Udp.endPacket();
  delay(1000);   // don't knw why this is here
}
#endif      // USE_UDP

#pragma mark ---web server---

#if USE_WEB_SERVER
void handleWebRequest(float tempCoop, float tempRun, int light, int pressure)
{
  // listen for incoming clients
  WiFiClient client = server.available();
  if (client) 
  {
  	//String HTTP_req = "";
    Serial.println("new client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) 
    {
      if (client.available()) 
      {
        char c = client.read();
        //HTTP_req += c;
        Serial.write(c);
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) 
        {
        	//checkThresholdUpdate(HTTP_req);
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println("Refresh: 120");  // refresh the page automatically every 30 sec
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html><body>");
          // body
          client.println("<h1>Status</h1>");
          client.println("    <table border=\"1\" cellpadding=\"3\">");
          client.print("        <tr><th>Coop</th><td>");
          client.print(int(tempCoop));
          client.print("&deg F</td>");
          printHeaterCell(client);
          printRoostCell(client, pressure);
          client.println("</tr>");
          client.print("        <tr><th>Run</th><td>");
          client.print(int(tempRun));
          client.print("&deg F</td>");
          printLightCell(client, light);
          client.println("</tr>");
          client.println("    </table>");
          client.println("<h1>Threshold Tuning</h1>");
          client.println("        <form method=\"get\">");
          client.println("    <table>");
          printInputForParameter(client, 1, lightThreshold);
          printInputForParameter(client, 2, pressureThreshold);
          printInputForParameter(client, 3, tempHeaterOn_F);
          printInputForParameter(client, 4, tempHeaterOff_F);
          client.println("    </table>");
          client.println("        </form>");
          client.println("</body></html>");
          //HTTP_req = "";		// clear this guy for the next round
          break;
        }
        if (c == '\n') 
        {
          // you're at the end of a line, reset for the next line
          currentLineIsBlank = true;
        }
        else if (c != '\r') 
        {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);

    // close the connection:
    client.stop();
    Serial.println("client disonnected");
  }
}

void printInputForParameter(WiFiClient client, int field, int curValue)
{
	client.print("        <tr><th>field");
	client.print(field);
	client.print("</th><td><input type=\"text\" name=\"");
	client.print(field);
	client.print("_field\" value=\"");
	client.print(curValue);
	client.print("\">");
	client.println("</td><td><input type=\"submit\" value=\"submit\"></td></tr>");
}

void checkThresholdUpdate(String http_req)
{
	if (http_req.substring(9) == "200 OK")
	{

	}

}

void printHeaterCell(WiFiClient client)
{
	client.print("<td bgcolor=\"");
	if (HIGH == powertailState) 
	{
		client.print(BG_COLOR_ON);
		client.print("\"> heater ON</td>");
	} else {
		client.print(BG_COLOR_OFF);
		client.print("\"> heater OFF</td>");
	}
}

void printRoostCell(WiFiClient client, int pressure)
{
	client.print("<td bgcolor=\"");
	if (onRoost) 
	{
		client.print(BG_COLOR_ON);
		client.print("\"> roost ON</td>");
	} else {
		client.print(BG_COLOR_OFF);
		client.print("\"> roost OFF (");
		client.print(pressure);
		client.print(")</td>");
	}
}

void printLightCell(WiFiClient client, int light)
{
	client.print("<td bgcolor=\"");
	if (sunIsUp) 
	{
		client.print(BG_COLOR_DAY);
		client.print("\"> day (");
	} else {
		client.print(BG_COLOR_NIGHT);
		client.print("\"> night (");
	}
	client.print(light);
	client.print(")</td>");
}

#endif  // USE_WEB_SERVER

///////////    powertail    ///////////
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

///////////    utility    ///////////

void readSensors(float *tempCoop, float *tempRun, int *light, int *pressure)
{
  getTemp(TEMP_COOP_INDEX, tempCoop);
  getTemp(TEMP_RUN_INDEX, tempRun);
  getPressure(pressure);
  getLight(light);
  
  Serial.print("coop: ");
  Serial.print(*tempCoop, 1);
  Serial.print("F");
  Serial.print("\t");

  Serial.print("run: ");
  Serial.print(*tempRun, 1);
  Serial.print("F");
  Serial.print("\t");
  
  Serial.print("light: ");
  Serial.print(*light);
  Serial.print("\t");

  Serial.print("pressure: ");
  Serial.print(*pressure);
  Serial.print("\t");

  Serial.print("heater: ");
  Serial.println(powertailState);
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

///////////    general arduino stuff    ///////////

void setup(void) 
{
  Serial.begin(9600); 
  wifiSetup();
#if OPERATE_DOOR 
  doorSetup();
#endif
  powertailSetup(); 
}

void loop(void) 
{
  //Serial.println("Loop function called");
  
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
  
  readSensors(&tempCoop, &tempRun, &light, &pressure);
  heaterChanged = checkHeater(tempCoop);
  roostChanged = checkChickensOnRoost(pressure);
  lightChanged = checkLightChanged(light);

#if OPERATE_DOOR  
  Serial.println("We are in the door code");
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
      if (okToCloseDoor(lightChanged, roostChanged))
      {
        closeTheDoor();
      } else {
        performDoorIdleTasks();
        delay(DOOR_IDLE_DELAY_MS); 
      }
      break;
      
    case DOOR_CLOSED:
      // do we need to open the door?
      if (okToOpenDoor(lightChanged))
      {
        openTheDoor();
      } else {
        performDoorIdleTasks();
        delay(DOOR_IDLE_DELAY_MS); 
      }
      break;
  }
#endif    // OPERATE_DOOR
#if USE_UDP
  handleUDP(tempCoop, tempRun, light, pressure, heaterChanged, lightChanged, roostChanged);
#endif
#if USE_WEB_SERVER  
  handleWebRequest(tempCoop, tempRun, light, pressure);
#endif
#if !OPERATE_DOOR
  delay(IDLE_DELAY);
#endif
}

