/*
  MQTT (DCCPPD) Block Controller Node
  Chuck Bade 3/16/21
*/

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"
//#define MQTTIP "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

// change the following three lines for your sensor/output configuration
const int JMRISensorNumber = 522;  // This is a JMRI number, i.e. MS400, must be unique
const int JMRIGreenNumber  = 866;    // These are JMRI numbers, i.e. MT55
const int JMRIYellowNumber = JMRIGreenNumber + 1;
const int JMRIRedNumber    = JMRIGreenNumber + 2;
const int JMRIAuxNumber    = JMRIGreenNumber + 3;

/*
 The analog reading reads whatever is on the 5V pin, if there is a 182k resistor 
 between 5V and A0.  The AnalogCalibrate provides a way of adjusting the reading
 for variance in resistor and ADC values.  Use a 1% resistor if possible.
 The purpose for monitoring the 5V level is to determine if there is excessive voltage
 drop between the power supply and the devices.  If the 5V drops below 4.75V, there
 could be various malfunctions and data loss.  Adjust when programming the module.
 To adjust: New AnalogCalibrate = (reported/actual) * AnalogCalibrate
*/
const float AnalogCalibrate = 196.3;


/*
  ///////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////
  
  Written for Wemos (or clone) D1 Mini for operating as a block controller on a model railroad,
  including an occupancy detector and 4 channels of digital output for driving a three light 
  signal head plus one auxillary output.

  It connects to the provided WiFi access point using Ssid and Pswd and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect
  using a blocking reconnect function.   
  
  The sensor (input) and signal head output numbers are set by the user.  The sensor (input) is
  set as JMRISensorNumber.  The sensor will be handled in JMRI as MS###, where ### is the sensor
  number.  The turnout number, defined by JRMIGreenNumber, JMRIYellowNumber, JMRIRedNumber, and 
  JMRIAuxNumber will be handled in JMRI as MT###, where ### is the output number.

  If the occupancy sensor status changes, the sketch will publish the latest state so JMRI will
  read it and set the state for the corresponding sensor.  It publishes the message "ACTIVE" or 
  "INACTIVE" using the topic "/trains/track/sensor/###", where ### is the sensor number in JMRI. 
  
  Upon setup, the sketch subscribes to messages from JMRI that are directed to the signal head 
  outputs.  The topic for the turnout change is "/trains/track/turnout/###", where ### is the 
  turnout number in JMRI.  Using this topic, JMRI will publish the message "CLOSED"
  or "THROWN" to set the state of the turnout.  
  
  Multiple nodes can respond to the same output numbers, if they subsribe to the same topic.
  In other words, two or more nodes could all throw or close MT55.  However, the sensors numbers
  would need to be unique.

  The sketch will periodically publish an analog voltage, which measures the voltage on the 5V 
  pin.  The JMRISensorNumber value is also the ID used in the analog output supply voltage 
  messages, so it should be set to a number unique to that node.

  The D1 Mini has 8 pins that will work for I/O but some pins better for certain purposes
  than others.  See the definitions below.
  */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>




// topic used for publishing sensor data
const String SensorTopic = "/trains/track/sensor/" + String(JMRISensorNumber);    

// topics for subscribing to incoming output commands
const String GreenTopic = "/trains/track/turnout/" + String(JMRIGreenNumber);
const String YellowTopic = "/trains/track/turnout/" + String(JMRIYellowNumber);
const String RedTopic = "/trains/track/turnout/" + String(JMRIRedNumber);
const String AuxTopic = "/trains/track/turnout/" + String(JMRIAuxNumber);

// topic used for publishing supply voltage
const String AnalogTopic = "/trains/track/analog/" + String(JMRISensorNumber);  

          // unused = 16; // D0/GPIO16  Not a good input, must be high on startup.
const int SensorPin = 5;  // D1/GPIO5    
          // unused = 4;  // D2/GPIO4   
const int GreenPin  = 14; // D5/GPIO14
const int YellowPin = 12; // D6/GPIO12
const int RedPin    = 13; // D7/GPIO13
const int AuxPin    = 15; // D8/GPIO15  12k pull down
const int LedPin    = 2;  // D4/GPIO2   Pulled up to 3.3v with LED1 and 1k resistor 

const int StartupDelay = JMRISensorNumber % 15;
WiFiClient espClient;
PubSubClient client(espClient);

#define DEBOUNCE_COUNT 20
int SensorState = 0;
int SensorSignal = 0;
boolean AnalogSent = true;
long Avg_analog = 1000000;



void publish(String topic, String payload) {
    Serial.println("Publish topic: " + topic + " message: " + payload);
    client.publish(topic.c_str() , payload.c_str(), true);
}



void subscribe(String topic) {
  // publish an empty output message to clear any retained messages
  client.publish(topic.c_str(), "", true);   
  Serial.println("Clearing previously retained messages for topic: " + topic);
  
  client.subscribe(topic.c_str());
  Serial.println("Subscribed to : " + topic);
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



void sendOneBit() {
  // publish a message when the occupancy bit changes
  
  if (SensorState)
    publish(SensorTopic, "INACTIVE");
  else
    publish(SensorTopic, "ACTIVE");
}



void setOutput(int pin, char* value) {
  // if THROWN, set the pin HIGH, else LOW
  
  if (!strcmp(value, "THROWN")) {
    Serial.println("Setting output pin " + String(pin) + " HIGH");
    digitalWrite(pin, HIGH);
  }
  else {
    Serial.println("Setting output pin " + String(pin) + " LOW");
    digitalWrite(pin, LOW);
  }
}



void callback(char* topic, byte* payload, unsigned int length) {
  /*
   * When a message is received that has been subscribed to, this function 
   * will be called.  
   */
  int outID;
  char* id;
  char* msg;
  
  Serial.print("Message arrived [" + String(topic) + "] ");

  // terminate the byte array
  payload[length] = 0;

  // copy the byte string to a char string
  msg = (char *) payload;
  Serial.println(String(msg));

  // parse the first word from the incoming topic
  strtok(topic, "/");

  // parse until there are none left, outID will hold the ID number
  while (id = strtok(NULL, "/"))
    outID = atoi(id);

  // set the appropriate output
  switch(outID) {
    case JMRIGreenNumber:
      setOutput(GreenPin, msg);
      break;
    case JMRIYellowNumber:
      setOutput(YellowPin, msg);
      break;
    case JMRIRedNumber:
      setOutput(RedPin, msg);
      break;
    case JMRIAuxNumber:
      setOutput(AuxPin, msg);
      break;
    default:
      break;
  }
}



void reconnect() {
  // Loop until we're reconnected
  Serial.println("client.state()=" + String(client.state()) + " client.connected()=" + String(client.connected()));

  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (client.connect(String(JMRISensorNumber).c_str())) {
      Serial.println("connected");
      subscribe(GreenTopic);
      subscribe(YellowTopic);
      subscribe(RedTopic);
      subscribe(AuxTopic);
  } else {
      Serial.print("failed, rc=" + String(client.state()) + " wifi=" + WiFi.status());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  Serial.println("client.state()=" + String(client.state()) + " client.connected()=" + String(client.connected()));
}



void setup() {
  long sum;
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Waiting " + String(StartupDelay) + " seconds before starting to reduce server pile-up.");
  delay(StartupDelay * 1000);
  Serial.println("Starting setup");

  pinMode(SensorPin, INPUT);
  pinMode(GreenPin, OUTPUT);
  pinMode(YellowPin, OUTPUT);
  pinMode(RedPin, OUTPUT);
  pinMode(AuxPin, OUTPUT);
  pinMode(LedPin, OUTPUT);

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
  if (client.connected() == false)
    reconnect();
  
  client.loop();

  // for the OD sensor input, read the pin, smooth it, and if it has changed send the data

  // Rather than reacting to a single glitch then waiting a "debounce" period, this
  // code requires the input to be predominantly changed for a number of cycles
  // before sending the data to JMRI.
  
  int sensor = digitalRead(SensorPin);
  
  if (sensor == 1 && SensorSignal < DEBOUNCE_COUNT) 
    SensorSignal++;
  if (sensor == 0 && SensorSignal > 0) 
    SensorSignal--;
    
  if (SensorState == 1 && SensorSignal == 0){
    SensorState = 0;
    digitalWrite(LedPin, SensorState);
    sendOneBit();
  } else if (SensorState == 0 && SensorSignal == DEBOUNCE_COUNT){
    SensorState = 1;
    digitalWrite(LedPin, SensorState);
    sendOneBit();
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
