#include <WiFi.h>
#include <WiFiUdp.h>
#include "WiFiInfo.h"
#include <SPI.h>
#include <OneWire.h> 

// wifi
char ssid[] = WIFI_NETWORK;    // network SSID (name) 
char pwd[] = WIFI_PASSWORD;    // network password

int status = WL_IDLE_STATUS;
unsigned int udpPort = 9999;          // local port to listen for UDP packets
IPAddress serverAddress(SERVER_IPV4_BYTE0, SERVER_IPV4_BYTE1, SERVER_IPV4_BYTE2, SERVER_IPV4_BYTE3);
const int UDP_PACKET_SIZE = 48; 
byte packetBuffer[ UDP_PACKET_SIZE];    //buffer to hold incoming and outgoing packets
WiFiUDP Udp;
char currentRequest = 'N';

// temp 
#define MAX_DS1820_SENSORS  2
#define DS18S20_PIN         2 

boolean foundAllDevices = false;
byte addr[MAX_DS1820_SENSORS][8];

OneWire ds(DS18S20_PIN);    // temp sensors on digital pin 2

// light & pressure
#define PHOTOCELL_PIN      A0
#define PRESSURE_PIN       A2

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

void doTemperatureStuff(void)
{
  float temp;
  for (int sensor = 0; sensor < MAX_DS1820_SENSORS; sensor++)
  {
    temp = 0;
    if (getTemp(sensor, &temp)) 
    {
      Serial.print("Temp Sensor ");
      Serial.print(sensor);
      Serial.print(": ");
      Serial.println(temp);
    } else {
      Serial.print("Failed to get reading for Temp Sensor ");
      Serial.println(sensor);
    }
    delay(1000);
  }
}

///////////    photocell    ///////////

void getLight(int *value)
{
  *value = analogRead(PHOTOCELL_PIN);
}

///////////    pressure    ///////////

void getPressure(int *value)
{
  *value = analogRead(PRESSURE_PIN);
}

///////////    wifi    ///////////

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
  
  Serial.println("\nStarting connection to UDP server...");
  Udp.begin(udpPort);
  
  // send "awake" message to server at startup
  sprintf((char *)packetBuffer, "A \n");
  Serial.println((char *)packetBuffer);
  Udp.beginPacket(serverAddress, udpPort);
  Udp.write(packetBuffer, UDP_PACKET_SIZE);
  Udp.endPacket();
  delay(1000);   // don't know why this is here
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

void handleUDP(void)
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

  // send packet if needed
  if (currentRequest != 'N')
  {
    switch (currentRequest)
    {
      case 'R':
        Serial.println("Current request: status");
        float tempCoop = 0.0;
        float tempRun = 0.0;
        int light = 0;
        int pressure = 0;

        getTemp(0, &tempCoop);
        getTemp(1, &tempRun);
        getPressure(&pressure);
        delay(500);
        // This second call to getPressure is used to disregard bad ananlogRead readings.
        // When mutliple analogRead calls are made in close temporal proximity, 
        // the first will affect the value of the second. 
        getPressure(&pressure); 
        delay(500);
        getLight(&light);
        
        memset(packetBuffer, 0, UDP_PACKET_SIZE);  // clear packet data
        sprintf((char *)packetBuffer, "S %d %d %d %d ", int(round(tempCoop)), int(round(tempRun)), light, pressure);
        Serial.print((char *)packetBuffer);
        break;
    }
    Udp.beginPacket(serverAddress, udpPort);
    Udp.write(packetBuffer, UDP_PACKET_SIZE);
    Udp.endPacket();
    delay(1000);   // don't knw why this is here
  }
}

///////////    utility    ///////////

void errorMessage(char *msg)
{
  
}

///////////    general arduino stuff    ///////////

void setup(void) 
{
  Serial.begin(9600); 
  wifiSetup(); 
}

void loop(void) 
{
  Serial.println("Loop function called");
  
  //make sure temperature sensors are there
  if (!foundAllDevices)
  {
    if (!(foundAllDevices = findDS18S20Devices())) return;
  } 
  handleUDP();
  delay(5000);
}

