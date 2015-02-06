heyChicken at Boulder Maker Faire, Jan 2015
======================================================

The Hey Chicken! project seeks to solve the age old problem of ensuring your backyard flock is safe and cozy, even when you are away from home. Our design for the Boulder Maker Faire used an Arduino Uno and various sensors to detect the state of the coop like light and pressure of the chickens on the roost bar to ultimately determine what the door should be doing at that moment. Instead of controlling the heater, we used the Powertail to control the exhibit lights to indicate day and night. For more details, see [these blog posts](http://squishylab.com/category/hey-chicken/).

Hardware Used for Maker Faire Demo
  - Arduino (Uno)
  - 2x [DS18B20](https://www.sparkfun.com/products/11050) One-wire temperature sensors
  - [Flexiforce Pressure sensor](https://www.sparkfun.com/products/8685)
  - photo resistor
  - [303 rpm motor](https://www.sparkfun.com/products/12147)
  - [motor controller](https://www.sparkfun.com/products/9457)
  - [PowerSwitch Tail II](https://www.sparkfun.com/products/10747)
  - 2x [microswitches](https://www.sparkfun.com/products/13119)

*NOTES*
  - **OneWire**: The OneWire library is a [git submodule](http://git-scm.com/book/en/v2/Git-Tools-Submodules#Cloning-a-Project-with-Submodules). After checking out the heyChicken project, you will need to initialize and and update the submodule. Then, [add the OneWire library to your Arduino sketch](http://arduino.cc/en/Guide/Libraries). 
