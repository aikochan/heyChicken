#include <OneWire.h> 

#define MAX_DS1820_SENSORS  2
#define DS18S20_Pin         2 

boolean foundAllDevices = false;
byte addr[MAX_DS1820_SENSORS][8];

OneWire ds(DS18S20_Pin);    // on digital pin 2

void errorMessage(char *msg)
{
  
}

void setup(void) 
{
    Serial.begin(9600);  
}

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

void loop(void) 
{
    if (!foundAllDevices)
    {
        if (!(foundAllDevices = findDS18S20Devices())) return;
    } 
    
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
