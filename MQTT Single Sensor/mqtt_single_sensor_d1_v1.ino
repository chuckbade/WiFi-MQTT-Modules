/*
  MQTT Single Sensor Node
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
const int JMRISensorNumber = 300;  // This is a JMRI number, i.e. MS400, must be unique
const int JMRIPwrUpNumber = 301;  // This is a JMRI number, i.e. MS401, must be unique

/*
  /////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 Mini for reporting the status of a sensor on a model railroad.

  It connects to the provided WiFi access point using ssid and password and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect
  using a blocking reconnect function.   
  
  The sensor (input) numbers is set by the user.  The sensor (input) is
  set as JMRISensorNumber.  The sensor will be handled in JMRI as MS###, where ### is the sensor
  number.

  If the state of the sensor changes, it will publish the latest state so JMRI will read it and set 
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

// pin numbers used
#define SENSOR 5    // D1
#define LED 2       // D4/GPIO2   Pulled up to 3.3v with LED1 and 1k resistor 

// topics for publishing sensor data
const String SensorTopic = "/trains/track/sensor/" + String(JMRISensorNumber);  
const String PwrUpTopic = "/trains/track/sensor/" + String(JMRIPwrUpNumber);  

WiFiClient espClient;
PubSubClient client(espClient);
#define DEBOUNCE_COUNT 20
int SensorState = 0;
int SensorSignal = 0;
boolean PowerUpSent = false;



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

 //print the local IP address
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


void sendOneBit() {
  // publish a message when the occupancy bit changes
  
  if (SensorState)
    publish(SensorTopic, "ACTIVE");
  else
    publish(SensorTopic, "INACTIVE");
}



void sendPowerUp() {
  publish(PwrUpTopic, "INACTIVE");
  delay(1000);
  publish(PwrUpTopic, "ACTIVE");
}



void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message arrived [" + String(topic) + "] ");
  Serial.println("Errant message.  We subscribe to nothing.");
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
  Serial.println("Starting setup");

  SensorState = 0;
  SensorSignal = 0;

  pinMode(SENSOR, INPUT_PULLUP);  // D1
  pinMode(LED, OUTPUT);
  
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

  // for the sensor input, read the pin, smooth it, and if it has changed send the data

  // Rather than reacting to a single glitch then waiting a "debounce" period, this
  // code requires the input to be predominantly changed for a number of cycles
  // before send the data to the base station.
  int sensor = digitalRead(SENSOR);
  
  if (sensor == 1 && SensorSignal < DEBOUNCE_COUNT) 
    SensorSignal++;
  if (sensor == 0 && SensorSignal > 0) 
    SensorSignal--;
    
  if (SensorState == 1 && SensorSignal == 0){
    SensorState = 0;
    digitalWrite(LED, SensorState);
    sendOneBit();
  } else if (SensorState == 0 && SensorSignal == DEBOUNCE_COUNT){
    SensorState = 1;
    digitalWrite(LED, SensorState);
    sendOneBit();
  }

  if (!PowerUpSent) {
    sendPowerUp();
    PowerUpSent = true;
  }
    
  delay(10);
}
