## LitterEater
### An alternative motherboard for the Litter Robot 3

*Never ask an engineer to clean the litterbox* - my wife :')

---

This repository contains all data needed to make a working alternative motherboard for the Litter Robot 3.

## Code
There's only the LitterEater.ino file, really. It's a bit huge, admittedly, but I tried to keep it cleaned and split in separate blocks.

You'll need to set your Arduino IDE up for ESP32 programming, as well as the following libraries :
 * AsyncTCP
 * ESPAsyncWebServer
 * AsyncElegantOTA
 * WiFiConnect Lite

You'll also need to install the ESP32 Filesystem Uploader, [instructions here](https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/).

## How it works

The core of the code is a state machine that can execute code on every loop while in a state, as well as once during transitions. The full state diagram is as follows :

![LitterEater state diagram](statediagram.png "LitterEater state diagram")

## Electronics

The full electronics are pretty simple, actually :
 * An ESP32 is at the heart of it
 * The motor is driven through a L298N H-bridge. I'm using [this one](https://www.amazon.fr/gp/product/B07YXFQ8CZ) because it also supplies 5V to power the ESP32.
 * A simple voltage divider is made using the load sensitive resistor and a 2.2KOhm resistor. The resulting voltage goes in a pin for `analogRead` to pick up.
 * The two Hall sensors are wired up the most simple way. +5V in, pullup resistor, result goes in a digital input pin.

My best rendition of a schematic with MS Paint :

![LitterEater electronics schematic](electronics.png "LitterEater electronics schematic")

## Licensing

This project is licensed under the WTFPL v2 :

```
           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
                   Version 2, December 2004

Copyright (C) 2004 Sam Hocevar <sam@hocevar.net>

Everyone is permitted to copy and distribute verbatim or modified
copies of this license document, and changing it is allowed as long
as the name is changed.

           DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
  TERMS AND CONDITIONS FOR COPYING, DISTRIBUTION AND MODIFICATION

 0. You just DO WHAT THE FUCK YOU WANT TO.
```
