
Chuck Bade's Guide to using MQTT on ESP8266-Based Modules with JMRI for Model Trains

This is an inexpensive system of hardware and software for running model train accessories with JMRI.  I'm using a DCC++ setup for a base station, but any base station should be able to be used, as long as JMRI will allow the MQTT protocol to exist along side the other protocol.  Each module has a ESP8266-based Wemos D1 Mini (or compatible) that sends and receives messages to and from JMRI.  These messages are sent via the MQTT protocol, which makes programming very straightforward using the Arduino IDE.  JMRI is setup to use both the MQTT and DCC++ protocols.

Note this uses both the DCC++ connection from JMRI, and the MQTT connection.  Each module connects to the MQTT broker and uses specific topics and messages to communicate with JMRI.  Outputs are prefixed with MT and inputs are prefixed with MS.

To build a system, you will need some knowledge of electronics and software, but only at a hobbyist's level.  Before taking this on, I would recommend buying a D1 Mini and programming it to flash the on-board LED, then hook up an external LED and make it flash.  If you get that working, you've done the hard part, getting Arduino IDE set up.

I currently have 8 circuit boards designed for use with JMRI/MQTT.  
- BC Shield - Used for feed the DCC signal to block controllers using standard CAT5 cables.
- Block Controller - Tested and in use on my layout.
- Turnout Controller - Tested and in use on my layout.
- Turntable Controller - Nearly ready, still has a bug.
- 5 Ray Sensor - For 5 rays of a staging yard or turntable.
- Bridge Detector - For connecting two optical sensors under a turntable bridge.
- MQTT Monitor - A useful LCD-based monitoring tool that can send messages.
- Robocut - For running a servo inside a boxcar to uncouple other cars from it.
- 
I may sell blank boards or just provide the files to have your own made.  I haven't decided which way to go with that.

I am a retired software engineer, and I have been involved in electronics since my dad bought some CK722 transistors and a book from Berstein Applebee, from which we built various circuits. Now I design my own hardware and write the software to run it.  I was trained in electronics (PMEL) in the Marine Corps and since I never had a formal education in electrical engineering, my designs are based on what I've learned along the way and are a product of much trial and some error. I have a Bachelor's Degree in Computer Science.

I had tried to use S88N to monitor sensors around the layout, but found it to be prone to electrical noise, so S88 is gone, but I've repurposed the CAT5 cables to route the DCC signal and power to the block controllers.  There's too much voltage drop to maintain an adequate level from the five volt supply, so some extra voltage taps are required at the far ends of the the layout.  My train room is 15' by 15' with track on all four walls.
