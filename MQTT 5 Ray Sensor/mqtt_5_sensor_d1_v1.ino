/*
  MQTT 5 Sensor Node
  Chuck Bade 3/24/21
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"
//#define MQTTIP "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

// change the following three lines for your sensor/output configuration
const int JMRISensorNumber = 550;  // The first sensor, a JMRI number, i.e. MS400, must be unique
const int NumberOfSensors = 3;     // The number of inputs
const int Inverted = 1;  // set to 0 for no inversion, set to 1 to invert ACTIVE/INACTIVE output messages 


/*
  /////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 Mini for reporting the status of 5 sensors on a model railroad.

  It connects to the provided WiFi access point using ssid and password and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect
  using a blocking reconnect function.   
  
  The sensor (input) numbers are set by the user.  The first sensor (input) is set as 
  JMRISensorNumber.  Each sensor will be handled in JMRI as MS###, where ### is the sensor number.
  Sensor numbers are assigned sequentially starting with the JMRISensorNumber.

  If the state of the sensors change, they will publish the latest state so JMRI will read it and set 
  the state for the corresponding sensor.  It publishes the message "ACTIVE" or "INACTIVE" using the 
  topic "/trains/track/sensor/###", where ### is the sensor number in JMRI. 
  
  The D1 Mini has 8 pins that will work for I/O but some pins better for certain purposes
  than others. 
  D0/GPIO16  Can be used as an input if it will not be low on power up.  This can cause 
              the device to lock up.
  D1/GPIO5   Good for output or input. 
  D2/GPIO4   Good for output or input.
  D5/GPIO14  Good for output or input.
  D6/GPIO12  Good for output or input.
  D7/GPIO13  Good for output or input.
  D8/GPIO15  Good for output or low impedance input. Has a hardwired 12k pull down resistor.
  D4/GPIO2   Pulled up to 3.3v with LED1 and 1k resistor.  Good for PB input or LED output. 

  D4 and D8 can be used is inputs, but because of the hardware pull down resistors, they are not as 
  well suited for inputs.  This is why they are listed last and will be used as outputs.
 
 */

// topics for publishing sensor data will be built in setup()
const String SensorTopic = "/trains/track/sensor/";  

WiFiClient espClient;
PubSubClient client(espClient);

// This class provides a structure for the input/output channel information.
class Gpio {
  public:
  int pin, state, signal;
  String topic;
};

Gpio Channel[8];
#define DEBOUNCE_COUNT 20



void publish(String topic, String payload) {
    Serial.println("Publish topic: " + topic + " message: " + payload);
    client.publish(topic.c_str() , payload.c_str(), true);
}



void setup_wifi() {
  delay(10);
  Serial.println();

  // We start by connecting to a WiFi network
  Serial.print("Connecting to " + String(MYSSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(MYSSID, PASSWD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println(" connected. IP address: " + WiFi.localIP().toString());
  
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA Hostname: " + ArduinoOTA.getHostname());
}



void sendOneBit(int bit) {
  Serial.println("bit=" + String(bit) + " state=" + Channel[bit].state);
  
  if (Inverted) {
    if (Channel[bit].state) 
      publish(Channel[bit].topic, "INACTIVE");
    else
      publish(Channel[bit].topic, "ACTIVE");
  } else {  
    if (Channel[bit].state) 
      publish(Channel[bit].topic, "ACTIVE");
    else
      publish(Channel[bit].topic, "INACTIVE");
  }
}



void callback(char* topic, byte* payload, unsigned int length) {
  int i;
  Serial.print("Message arrived [" + String(topic) + "] ");

  Serial.println("Ignoring message.");
}



void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (client.connect((ArduinoOTA.getHostname() + ":" 
      + String(JMRISensorNumber)).c_str())) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=" + String(client.state()) + " wifi=" + WiFi.status());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting setup");
                                  // This order is based on the labels on the board
  Channel[0].pin = 5;  // D1/GPIO5   and on suitability for inputs and outputs, 
  Channel[1].pin = 4;  // D2/GPIO4   with inputs being first in the order.
  Channel[2].pin = 14; // D5/GPIO14
  Channel[3].pin = 12; // D6/GPIO12
  Channel[4].pin = 13; // D7/GPIO13
  
  // D0/GPIO16  
  // D8/GPIO15  12k pull down
  // D4/GPIO2   Pulled up to 3.3v with LED1 and 1k resistor 

  for (int i = 0; i < NumberOfSensors; i++) {
    Channel[i].state = 0;
    Channel[i].signal = 0;
//    pinMode(Channel[i].pin, INPUT_PULLUP);
    pinMode(Channel[i].pin, INPUT);
    Channel[i].topic = SensorTopic + String(JMRISensorNumber + i); 
  }

  setup_wifi();
  client.setServer(MQTTIP, 1883);
  client.setCallback(callback);
}



void loop() { 
  ArduinoOTA.handle();

  // confirm still connected to mqtt server
  if (!client.connected())
    reconnect();
  
  client.loop();

  // for the OD sensor input, read the pin, smooth it, and if it has changed send the data

  // Rather than reacting to a single glitch then waiting a "debounce" period, this
  // code requires the input to be predominantly changed for a number of cycles
  // before sending the data to JMRI.

  for (int i = 0; i < NumberOfSensors; i++) {
    int sensor = digitalRead(Channel[i].pin);
  
    if (sensor == 1 && Channel[i].signal < DEBOUNCE_COUNT) 
      Channel[i].signal++;
    if (sensor == 0 && Channel[i].signal > 0) 
      Channel[i].signal--;
    
    if (Channel[i].state == 1 && Channel[i].signal == 0){
      Channel[i].state = 0;
      digitalWrite(Channel[i].pin, Channel[i].state);
      sendOneBit(i);
    } else if (Channel[i].state == 0 && Channel[i].signal == DEBOUNCE_COUNT){
      Channel[i].state = 1;
      digitalWrite(Channel[i].pin, Channel[i].state);
      sendOneBit(i);
    }
  }

  delay(10);
}
