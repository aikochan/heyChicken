heyChicken
==========

The Hey Chicken! project seeks to solve the age old problem of ensuring your backyard flock is safe and cozy, even when you are away from home. Our design uses an Arduino Uno, Wifi Shield, and various sensors to detect the state of the coop like temperature, light, and whether the chickens are roosting to ultimately determine what the door and heater should be doing at that moment. A server on the local network functions as the “brains” of the operation and wirelessly runs the automation based on the coop conditions the Arduino is reporting. Of course, this project would not be complete without a remote control panel iOS app that can be used to control the chicken door and heater, or just to spy on your flock via a coop webcam.  For more details, see: http://squishylab.com/2015/01/07/hey-chicken-coop-automation-project/

Hardware Used
  - Arduino (Uno)
  - WiFi shield
  - DS18B20 One-wire temperature sensor(s) (https://www.sparkfun.com/products/11050)
  - Flexiforce Pressure sensor (https://www.sparkfun.com/products/8685)




*NOTE*: The OneWire library is a git submodule. After checking out the heyChicken project, you will need to initialize and and update the submodule. See this page: http://git-scm.com/book/en/v2/Git-Tools-Submodules#Cloning-a-Project-with-Submodules

Now, add the OneWire library to Arduino. (http://arduino.cc/en/Guide/Libraries)
