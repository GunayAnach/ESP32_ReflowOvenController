ESP32 Reflow Oven Controller with WiFi
====================

**Arduino-based reflow oven controller with:**
* [PID] loop control
* [Phase Fired] control for AC outputs
* graphic TFT LC-Display, drawing the temperature curves
  * using an [Adafruit 1.8" TFT] or derivate display
* solely controlled using a cheap rotary encoder and its single button
* Webapp to monitor the the temperatures and start and stop the reflow Programm 
* stores up to 30 temperature profiles in EEPROM
* configurable PID-parameters with bulid in auto tune sytem
* simple, small hardware to drive loads up to 600V and up to 8A (just schametic at the moment)
* hardware can
  * measure two temperatures independently
  * drive two AC loads, such as heater and fan
* could also be used for slow coockers
* Manual power controll 
* *Please Note*: Recommend ARDUINO 1.8.9
* Libraries included in the Projekt folder to avoid problems with versions 
* (c) 2019 Patrick Knöbel <reflow@im-pro.at> in part based on:
  * (c) 2014 Karl Pitrich <karl@pitrich.com>
  * (c) 2013 Ed Simmons <ed@estechnical.co.uk>

**!!!DANGER!!!**

![Warning]

## Warning: This project operates with possibly lethal mains voltage. If you are unsure what to do, don't do it and get help from an experienced tinkerer with professional training.

## Warning: Do not leave the Oven unattended! The Power electronics is not develop professionally and can fail and potentially catch fire!

**Completed build**

![Oven](images/IMG_20190613_075913.jpg?raw=true)


|![Completed1](images/IMG_20190613_075921.jpg?raw=true) | ![Completed2](images/IMG_20190613_075926.jpg?raw=true)|
|------------ | -------------|


Introduction
====================

This Reflow Oven Controller relies on an [ESP32], 

<div align="center">
  <a href="https://www.youtube.com/watch?v=oBlvcZ-5PeM"><img src="https://img.youtube.com/vi/oBlvcZ-5PeM/0.jpg" alt="Youtube Video"></a>
</div>

There is a schematic but no board design at the moment. There is an eagle project in the forked repository.

![Schematic](Schamatic_ESP8266.png?raw=true)

I used the original PCB with some modification and made an adapter to use the ESP32 devkit v1 for it:

![adapter](images/adapter_s.jpg?raw=true&s=200)


The board contains the [ESP32], Very simple [Zero crossing] detection circuit, used to align control logic to mains frequency, two [MAX31855] thermocouple-to-digital converters and two [Sharp S202S01] PCB-mount solid state relays, mounted on cheap [Fischer SK409 50,8] heat sinks. The current software uses only one of the thermocouples, so you need to populate one IC only. If you're lucky, you can get free samples of the MAX31855 from Maxim.

The oven I used for my build: [OVEN]

The software uses [PID] control of the heater. The fan output for improved temperature distribution is not supported at the moment.

The software should work in 50 and 60Hz mains, the 60Hz version is not tested, though.

Screenshots and usage information
========

Image | Information
------------ | -------------
![CycleWithOverflow](images/IMG_20190615_144259.jpg?raw=true) | *Display after a cylcle has been completed. The blue line is the setpoint, the yellow line the actual temperature measured by the thermocouple. Note that the graph wraps around automatically. 'Sp' is the current setpoint calculated by the PID loop. In the lower line there are: Current heater output in percent, and the current temperature rise or drop rate in °C per second. The graph will draw orientation lines every 50°C up to the peak temperature set in the selected profile.*
![MenuDefault](images/IMG_20190613_075943.jpg?raw=true) | *The main menu can be navigated by rotating the encoder. Clicking enters the menu item, or navigates to the submenu. Doubleclick moves up or back or exits the menu item. Up to 30 Profiles can be loaded and saved. You have to do this manually, so that you can have 'save-as' functionality without overwriting existing profiles.*
![ProfileEdit](images/IMG_20190613_075950.jpg?raw=true) | *To edit a setting, click once to enter edit mode (red cursor), then rotate to change the value, click again to save. Doubleclick will exit without saving.*
![PIDValues](images/IMG_20190613_075958.jpg?raw=true) ![Autotune](images/IMG_20190613_080001.jpg?raw=true) ![AutotuneRun](images/IMG_20190613_080009.jpg?raw=true)| *You can chnage PID settings manually or use the Autotune functions. For more information see [PID Autotune]*
![ManualHeating](images/IMG_20190613_080029.jpg?raw=true) | *Just want to turn on the Oven? No Problem. Just choose how much % of power you want.*
![WiFi](images/IMG_20190613_080040.jpg?raw=true) ![WiFiPassword](images/IMG_20190613_080046.jpg?raw=true) | *To connect to the last Saved connection click on "Connect to Saved". Or just search for the Wifi in the list you want to conncet and enter the password.*

Webserver
====================

When connected to the Wifi the Reflow Oven acts as a Webserver. Just access its IP-Address shown on the display in the webbrowser.
You can start an Reflow Cycle over the browser remotely.

**Reflow Cycle**
![Reflow Cycle](images/Screenshot_20190615-144125.jpg?raw=true) 
**Tuning**
![Tuning](images/WebTuning.png?raw=true) 


Obtaining the source code
====================

Just download the repository. In the upper right corner click **Clown or download** and download zip file.

Installation
====================

Of course, you need to have the Arduino IDE installed. I've worked with version 1.8.9 only and I will not support older versions. Get it from the [Arduino Download] page or upgrade you current Arduino setup.

There no dependencies all libraries are included in the Projekt folder.

Select the right hardware from the Tools->Board menu. Follow the instructions on [ESP32]

Compile the firmware (Sketch->Verify) to test everything is installed correctly. 
If something's wrong you maybe have to delete some libraries from you Arduino folder because they are already in the Projekt folder.

Now, choose the correct serial port from Tools->Serial Port and then upload the code.


Things to note
====================

* The [MAX31855] does not like the thermocouple being grounded; It must be isolated from ground or earth.
* The PID Loop must be tuned individually for each oven. It will *not* work out of the box. 
* [PID Autotune] is not very useful, as it seems to be able to tune only to keep a specific temperature value, which is not what we do in a reflow oven. Also, at least my oven seems to be very non-linear when heating up.
* When rewiring inside your oven, use only wiring that can withstand high temperatures. I use silicone coated lace.
* Do not solder wiring inside you oven, the temperature might desolder you joints. **Crimp everything.**
* Use proper earth ground connection for your ovens chassis.

Ideas and todo
====================
* Add fan control for even heat distribution and controlled cooldown (I did not need a fan)
* Make settings accessible over the Web Page 

Licensing
====================
```
The MIT License (MIT)
Copyright (c) 2019 Patrick Knöbel reflow@im-pro.at
Copyright (c) 2014 karl@pitrich.com
All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

[ESP32]:https://github.com/espressif/arduino-esp32
[OVEN]:https://geizhals.at/severin-to-2052-mini-backofen-a942484.html
[PID Autotune]:https://github.com/br3ttb/Arduino-PID-AutoTune-Library
[Arduino Download]:http://arduino.cc/en/Main/Software
[PID]:http://en.wikipedia.org/wiki/PID_controller
[Phase Fired]:http://en.wikipedia.org/wiki/Phase-fired_controllers 
[Adafruit 1.8" TFT]:http://www.adafruit.com/products/358
[MAX31855]:http://www.maximintegrated.com/en/products/analog/sensors-and-sensor-interface/MAX31855.html
[Fischer SK409 50,8]:http://www.pollin.de/shop/dt/NzE5OTY1OTk-
[Sharp S202S01]:http://sharp-world.com/products/device/lineup/data/pdf/datasheet/s102s01_e.pdf
[Zero crossing]:http://en.wikipedia.org/wiki/Zero_crossing
[Menu]:https://github.com/0xPIT/menu

[Warning]:https://i.imgur.com/D3Ph8ci_d.jpg?maxwidth=640&shape=thumb&fidelity=medium
