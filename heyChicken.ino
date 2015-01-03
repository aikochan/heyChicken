#include <OneWire.h> 

#define MAX_DS1820_SENSORS 2

int DS18S20_Pin = 2; //DS18S20 Signal pin on digital 2

//Temperature chip i/o
OneWire ds(DS18S20_Pin);	// on digital pin 2

byte addr[MAX_DS1820_SENSORS][8];

void setup(void) 
{
	Serial.begin(9600);
	if (!ds.search(addr[0])) 
	{
		//no more sensors on chain, reset search
		ds.reset_search();
		delay(250);
		return;
	}
	if (!ds.search(addr[1])) 
	{
		//no more sensors on chain, reset search
		ds.reset_search();
		delay(250);
		return;
	}
}

void loop(void) 
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

// obtains the temperature from one DS18S20 in DEG Celsius as result
// returns true on success, false on error
boolean getTemp(int sensor, float *result)
{
	boolean success = true;
	byte data[9]; 
	
	if (sensor >= MAX_DS1820_SENSORS)
	{
		Serial.println( "Sensor index invalid" );
		success = false;
	} else if (OneWire::crc8( addr[sensor], 7) != addr[sensor][7]) {
		Serial.println( "CRC is not valid" );
		success = false;
	} else if (addr[sensor][0] != 0x28) {
		Serial.print("Device is not a DS18S20 family device. Device ID: ");
		Serial.println(addr[sensor][0]);
		success = false;
	}

	if (success) 
	{
		ds.reset();
		ds.select(addr[sensor]);
		ds.write(0x44,1);	// start conversion, with parasite power on at the end
	
		//delay(1000);		// uncomment for parasitic power
	
		ds.reset();
		ds.select(addr[sensor]);		
		ds.write(0xBE);		// Read Scratchpad
	
		for ( int i = 0; i < 9; i++) 
		{						
			// we need 9 bytes
			data[i] = ds.read();
		}
	
		float tempRead = ((data[1] << 8) | data[0]);	 //using two's compliment (???)
		*result = (tempRead / 16);
	}
	return success;
}
