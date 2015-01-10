heyChicken
==========

The Hey Chicken! project seeks to solve the age old problem of ensuring your backyard flock is safe and cozy, even when you are away from home. Our design uses an Arduino Uno, Wifi Shield, and various sensors to detect the state of the coop like temperature, light, and whether the chickens are roosting to ultimately determine what the door and heater should be doing at that moment. A server on the local network functions as the “brains” of the operation and wirelessly runs the automation based on the coop conditions the Arduino is reporting. Of course, this project would not be complete without a remote control panel iOS app that can be used to control the chicken door and heater, or just to spy on your flock via a coop webcam.  For more details, see [this blog post](http://squishylab.com/2015/01/07/hey-chicken-coop-automation-project/).

Hardware Used
  - Arduino (Uno)
  - WiFi shield
  - [DS18B20](https://www.sparkfun.com/products/11050) One-wire temperature sensors
  - [Flexiforce Pressure sensor](https://www.sparkfun.com/products/8685)




*NOTES*
  - **OneWire**: The OneWire library is a [git submodule](http://git-scm.com/book/en/v2/Git-Tools-Submodules#Cloning-a-Project-with-Submodules). After checking out the heyChicken project, you will need to initialize and and update the submodule. Then, [add the OneWire library to your Arduino sketch](http://arduino.cc/en/Guide/Libraries). 
  - **Network name and password**: You will need to create a new [Tab file](http://arduino.cc/en/Hacking/BuildProcess) in your Arduino sketch called "WiFiInfo.h". In this file, add #define directives for your network SSID name and password as WIFI_PASSWORD and WIFI_NETWORK, as well as for the server IP address. 
