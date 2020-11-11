/*
  MQTT 2 Sensor Node
  Chuck Bade 11/9/20
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

const char* MQTTServer = "192.168.1.13";

// change the following three lines for your sensor/output configuration
const int JMRISensorNumber1 = 438;  // The first sensor, a JMRI number, i.e. MS400, must be unique
const int JMRISensorNumber2 = 439;  // The second sensor, a JMRI number, i.e. MS400, must be unique

/*
  /////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 Mini for reporting the status of 2 sensors on a model railroad.

  It connects to the provided WiFi access point using ssid and password and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber1 must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect
  using a blocking reconnect function.   
  
  The sensor (input) numbers are set by the user.  The first sensor (input) is set as 
  JMRISensorNumber1.  Each sensor will be handled in JMRI as MS###, where ### is the sensor number.
  
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


/*
 * The battery reading reads whatever is on the 5V pin, if there is a 180k resistor 
 * between 5V and A0.  The battCalibrate provides a way of adjusting the reading
 * for variance in resistor values.  Use a 1% resistor if possible.
 */
const float AnalogCalibrate = 208.2;

// topics for publishing sensor data will be built in setup()
const String SensorTopic = "/trains/track/sensor/";  

// topic for reporting supply voltage
const String AnalogTopic = "/trains/track/analog/" + String(JMRISensorNumber1);  

WiFiClient espClient;
PubSubClient client(espClient);

// This class provides a structure for the input/output channel information.
class Gpio {
  public:
  int pin, state, signal;
  String topic;
};

Gpio Channel[2];
boolean AnalogSent = true;
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
    if (client.connect(String(JMRISensorNumber1).c_str())) {
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

  Channel[0].pin = 16; // D0/GPIO16
  Channel[0].state = 0;
  Channel[0].signal = 0;
  pinMode(Channel[0].pin, INPUT);
  Channel[0].topic = SensorTopic + String(JMRISensorNumber1); 

  Channel[1].pin = 5;  // D1/GPIO5
  Channel[1].state = 0;
  Channel[1].signal = 0;
  pinMode(Channel[1].pin, INPUT);
  Channel[1].topic = SensorTopic + String(JMRISensorNumber2); 

  Serial.println("Analog voltage=" + String(analogRead(A0) / AnalogCalibrate));
  setup_wifi();
  client.setServer(MQTTServer, 1883);
  client.setCallback(callback);
}



void sendAnalog() {
  // This requires a 182k resistor between 5V and A0
  publish(AnalogTopic, String(analogRead(A0) / AnalogCalibrate));
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

  for (int i = 0; i < 2; i++) {
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

  analogTime = millis() % 60000;  // send the analog value every minute 
  
  if ((!AnalogSent) && (analogTime < 1000)) { 
    sendAnalog();
    AnalogSent = true; 
  }
  
  if (analogTime > 1000)
    AnalogSent = false;
    
  delay(10);
}
