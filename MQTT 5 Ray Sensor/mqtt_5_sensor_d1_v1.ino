/*
  MQTT 5 Sensor Node
  Chuck Bade 3/20/21
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"
//#define MQTTIP "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

//const char* MQTTServer = "192.168.1.13";

// change the following three lines for your sensor/output configuration
const int JMRISensorNumber = 900;  // The first sensor, a JMRI number, i.e. MS400, must be unique
const int NumberOfSensors = 5;     // The number of inputs

/*
 The analog reading reads whatever is on the 5V pin, if there is a 182k resistor 
 between 5V and A0.  The AnalogCalibrate provides a way of adjusting the reading
 for variance in resistor and ADC values.  Use a 1% resistor if possible.
 The purpose for monitoring the 5V level is to determine if there is excessive voltage
 drop between the power supply and the devices.  If the 5V drops below 4.75V, there
 could be various malfunctions and data loss.  Adjust when programming the module.
 To adjust: New AnalogCalibrate = (reported/actual) * AnalogCalibrate
*/
const float AnalogCalibrate = 195.5;


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
  
  The sketch will periodically publish "battery" voltage, which measures the voltage on the 5V 
  pin.  The JMRISensorNumber value is also the ID used in the analog output supply voltage 
  messages, so it should be set to a number unique to that node.

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

// topic for reporting supply voltage
const String AnalogTopic = "/trains/track/analog/" + String(JMRISensorNumber);  

WiFiClient espClient;
PubSubClient client(espClient);

// This class provides a structure for the input/output channel information.
class Gpio {
  public:
  int pin, state, signal;
  String topic;
};

Gpio Channel[8];
boolean AnalogSent = true;
#define DEBOUNCE_COUNT 20
long Avg_analog = 1000000;



void publish(String topic, String payload) {
    Serial.println("Publish topic: " + topic + " message: " + payload);
    client.publish(topic.c_str() , payload.c_str(), true);
}



void setup_wifi() {
  delay(10);
  Serial.println();

  // We start by connecting to a WiFi network
  Serial.print("Connecting to " + String(MYSSID));
  WiFi.begin(MYSSID, PASSWD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println(" connected. IP address: " + WiFi.localIP().toString());
}



void sendOneBit(int bit) {
  if (Channel[bit].state)
    publish(Channel[bit].topic, "INACTIVE");
  else
    publish(Channel[bit].topic, "ACTIVE");
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
    if (client.connect(String(JMRISensorNumber).c_str())) {
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
    pinMode(Channel[i].pin, INPUT_PULLUP);
    Channel[i].topic = SensorTopic + String(JMRISensorNumber + i); 
  }

  Serial.println("Analog voltage=" + String(analogRead(A0) / AnalogCalibrate));
  setup_wifi();
  client.setServer(MQTTIP, 1883);
  client.setCallback(callback);
}



void sendAnalog(float avg) {
  if (avg == 0)
    avg = analogRead(A0);
    
  // This requires a 182k resistor between 5V and A0
  publish(AnalogTopic, String((avg / 1000) / AnalogCalibrate));
}



void loop() {
  long analogTime;
  
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


  // calculate a moving average of the analog reading
  Avg_analog = ((Avg_analog * 99) + (analogRead(A0) * 1000)) / 100;
  
  // send it every 60 seconds, with an offset to avoid pile-ups
  analogTime = ((millis() + ((JMRISensorNumber % 60) * 1000)) % 60000); 
  
  if ((!AnalogSent) && (analogTime < 1000)) { 
    sendAnalog(Avg_analog);
    AnalogSent = true; 
  }
  
  if (analogTime > 1000)
    AnalogSent = false;
    
  delay(10);
}
