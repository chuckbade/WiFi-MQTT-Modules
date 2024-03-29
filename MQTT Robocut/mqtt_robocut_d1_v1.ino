/*
  MQTT RoboCut Node
  Chuck Bade 03/24/21
*/

// Update and uncomment these 3 lines with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\

//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"
//#define MQTTIP "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

#define VCCCAL 200.0   // overriding the default value
#define CUT_TIME 5000  // how many milliseconds to hold the cut lever

// Change the following four lines for your sensor/turnout and servo configuration
//                                A  IDLE  B  
const int JMRISensorNumber[]  = {800,  0, 801};     // Sensor feedback for JMRI
const int JMRITurnoutNumber[] = {900,  0, 901};     // JMRI turnout numbers
const int ServoAngle[]        = { 10, 90, 170};     // Servo positions

// Time between each degree of movement in milliseconds
const int ServoDelay = 2;         // 25 is nice and slow. Zero is fastest.           


/*
  /////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 Mini for operating a servo inside a car on a model railroad, which 
  is tied to the couplers at both ends.

  It connects to the provided WiFi access point using ssid and password and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber[0] for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber[0] must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect using 
  a blocking reconnect function.   
  
  The sensor (input) and turnout (output) numbers are set by the user.  The sensor (input) is
  set as JMRISensorNumber.  The sensor will be handled in JMRI as MS###, where ### is the sensor
  number.  The turnout number, defined by JRMITurnoutNumber will be handled in JMRI as MT###, 
  where ### is the turnout number.

  If the closed/thrown status is changed, by a message from JMRI, it will publish the latest state so 
  JMRI will read it and set the state for the corresponding sensor.  It publishes the message "ACTIVE"
  or "INACTIVE" using the topic "/trains/track/sensor/###", where ### is the sensor number in JMRI. 

  When the uncoupler is thrown to either end, A or B, the servo will move to that end for 5 seconds then 
  return to center (IDLE) and a message of "INACTIVE" will be sent for both turnout numbers.
  
  Upon setup, the sketch subscribes to messages from the base station that are directed to the
  turnout numbers.  The topic for the turnout change is "/trains/track/turnout/###", where ### 
  is the turnout number in JMRI.  Using this topic, JMRI will publish the message "CLOSED" or 
  "THROWN" to set the state of the uncoupler.  
  
  Multiple nodes can respond to the same turnout numbers, if they subsribe to the same topic.
  In other words, two or more nodes could all throw or close MT55.  However, the sensors numbers
  would need to be unique.

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

*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Servo.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>


// Create meaningful names for the array indexes.
#define A 0
#define IDLE 1
#define B 2

const int LedPin[] = {12, 0, 14};  // High when coupler A (D6) or B (D5) is cut. 
// pin numbers used
#define SERVO 4    // D2 PWM Servo output.
#define PULSE 13   // D7 Pulse driver chip (unused but set low)

const String SensorTopic = "/trains/track/sensor/";
const String TurnoutTopic = "/trains/track/turnout/";
const int StartupDelay = JMRISensorNumber[A] % 15;WiFiClient EspClient;

PubSubClient Client(EspClient);
Servo MyServo;
int CurrentIndex = IDLE;
long IdleMillis = 0;



void publish(String topic, String payload) {
    Serial.println("Publish topic: " + topic + " message: " + payload);
    Client.publish(topic.c_str() , payload.c_str(), true);
}



void setup_wifi() {
  delay(10);
  Serial.println();

  // We start by connecting to a WiFi network
  Serial.print("Connecting to " + String(MYSSID));
  WiFi.mode(WIFI_STA);
  WiFi.begin(MYSSID, PASSWD);
  
  while (WiFi.status() != WL_CONNECTED) {  // wait for the wifi to connect
    delay(500);
    Serial.print(".");
  }

  Serial.println("connected. IP address: " + WiFi.localIP().toString());
  
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



void sendSensorState(int index, char* payload) {
  // publish the sensor message
  String topic = SensorTopic + JMRISensorNumber[index]; 
  publish(topic.c_str(), payload);
}




void moveServo(int dest) {
  // move the servo, degree by degree, from origin to destination
  int i;
  int origin = MyServo.read();
  Serial.println("moveServo(): Moving from " + String(origin) + " to " + String(dest));

  // increment or decrement one degree at a time
  
  if (dest > origin)
     for (i = origin; i <= dest; i++) {
        MyServo.write(i);
        delay(ServoDelay);
     }
  else      
     for (i = origin; i >= dest; i--) {
        MyServo.write(i);
        delay(ServoDelay);
     }
}



void closeTurnout() {
  // set both the snap switch outputs and the servo to the closed position
  Serial.println("Turning LED off.");
  digitalWrite(LedPin[A], LOW);     // turning off both outputs
  digitalWrite(LedPin[B], LOW);
  moveServo(ServoAngle[IDLE]);      // move to the middle
  CurrentIndex = IDLE;
  sendSensorState(A, "INACTIVE");   // send the sensor messages
  sendSensorState(B, "INACTIVE");
  // detaching the servo signal eliminates occasional chirping of the servo
  MyServo.detach();
}



void throwTurnout(int index) {
  // need to reattach the servo signal after closing detached it
  MyServo.attach(SERVO);
  // set the servo to the selected position and set the flag
  CurrentIndex = index;               // set the global position flag
  IdleMillis = millis() + CUT_TIME;   // set the time to return to IDLE
  moveServo(ServoAngle[index]);       // move to A or B end
  sendSensorState(index, "ACTIVE");   // show the turnout as active
}



void callback(char* topic, byte* payload, unsigned int length) {
  char* id;
  int outID;
  // check the incoming message for closed or thrown
  // we only subscribed to turnout messages, so it should be one or the other
  
  Serial.print("Message arrived [" + String(topic) + "] ");

  payload[length] = 0;     // terminate the byte array

  String msg = String((char *) payload);  // convert the byte array to a string
  Serial.println(msg);

  strtok(topic, "/");      // parse the first word from the incoming topic

  // parse until there are none left, outID will hold the ID number
  while (id = strtok(NULL, "/")) 
    outID = atoi(id);

  // see if the ID is ours

  for (int i = A; i <= B; i+=2) 
    if ((msg == "THROWN") && (outID == JMRITurnoutNumber[i]))   // thrown means cut
      throwTurnout(i);   
  
  if (msg == "CLOSED") 
    closeTurnout();
}



void subscribe(int index) {
  String topic = TurnoutTopic + String(JMRITurnoutNumber[index]);
  // publish an empty output message to clear any retained messages
  publish(topic.c_str(), "");   
  Serial.println("Clearing previously retained messages for topic: " + topic);

  Client.subscribe(topic.c_str());    // subscribe to the output messages
  Serial.println("Subscribed to : " + topic);
}



void reconnect() {
  // Loop until we're reconnected
  while (!Client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (Client.connect((ArduinoOTA.getHostname() + ":" 
      + String(JMRISensorNumber[A])).c_str())) {
      Serial.println("connected");
      subscribe(A);   // subscribe to both turnout commands
      subscribe(B);
    } else {
      Serial.print("failed, rc=" + String(Client.state()) + " wifi=" + WiFi.status());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void setup() {
  int state;
  int eeReady;
  
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Waiting " + String(StartupDelay) + " seconds before starting to reduce server pile-up.");
  delay(StartupDelay * 1000);

  Serial.println("Starting setup");

  // set up the output pins
  
  pinMode(LedPin[A], OUTPUT);   // D6    
  pinMode(LedPin[B], OUTPUT);   // D5
  pinMode(PULSE, OUTPUT);       // D7
  digitalWrite(PULSE, LOW);     // For standard turnouts, this would pulse the driver chip, leave low 

  Serial.println("Servo Idle Angle=" + String(ServoAngle[IDLE]) 
    + " Cut A Servo Angle=" + String(ServoAngle[A])
    + " Cut B Servo Angle=" + String(ServoAngle[B]));

  Serial.println("Attaching servo.");
  MyServo.attach(SERVO);            // D2
  MyServo.write(ServoAngle[IDLE]);  // an immediate write prevents the jerk to 90 and back
  closeTurnout();                   // follow up with this to set the outputs properly
  Serial.println("Attaching servo done.");

  setup_wifi();
  Client.setServer(MQTTIP, 1883);
  Client.setCallback(callback);
}



void loop() {
  int j;

  ArduinoOTA.handle();
 
  // confirm still connected to mqtt server
  
  if (!Client.connected())
    reconnect();
  
  Client.loop();  // not sure about what this does, but it is required

  // if a cut lever is currently pulled and the cut time has elapsed
  // return to idle (closed)
  
  if (CurrentIndex != IDLE) {
    if (millis() > IdleMillis)   // if the time is up
      closeTurnout();
    else
      digitalWrite(LedPin[CurrentIndex], (millis() % 400) / 200);
  }
    
    
  delay(10);
}
