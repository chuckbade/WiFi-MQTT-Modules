/*
  MQTT Turnout Node
  Chuck Bade 3/24/21
*/

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"
//#define MQTTIP" "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

// change the following two lines for your sensor/turnout configuration
const int JMRISensorNumber = 408;  // This is a JMRI number, i.e. MS400, must be unique
const int JMRITurnoutNumber = 76;  // This is a JMRI number, i.e. MT55

// Time between each degree of movement in milliseconds, 25 is nice and slow.
const int ServoDelay = 10;         // If you're not using a servo, set this to zero           
const int PulseTime = 100;         // Time of pulse sent to snap-type turnouts

// Set the following to TRUE if you want the turnout to be set to the previous state
// on startup.  WARNING: This is probably limited to 100,000 cylces, then no memory.
#define RESTORE_LAST_STATE false


/*
  /////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 Mini for operating a turnout on a model railroad, either the
  snap type, like Atlas or Sato, or a servo motor.

  It connects to the provided WiFi access point using ssid and password and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect
  using a blocking reconnect function.   
  
  The sensor (input) and turnout (output) numbers are set by the user.  The sensor (input) is
  set as JMRISensorNumber.  The sensor will be handled in JMRI as MS###, where ### is the sensor
  number.  The turnout number, defined by JRMITurnoutNumber will be handled in JMRI as MT###, 
  where ### is the turnout number.

  If the closed/thrown status is changed, either by the manual push button or by a message from
  JMRI, it will publish the latest state so JMRI will read it and set the state for the 
  corresponding sensor.  It publishes the message "ACTIVE" or "INACTIVE" using the topic 
  "/trains/track/sensor/###", where ### is the sensor number in JMRI. 
  
  Upon setup, the sketch subscribes to messages from the base station that are directed to the
  turnout number.  The topic for the turnout change is "/trains/track/turnout/###", where ### 
  is the turnout number in JMRI.  Using this topic, JMRI will publish the message "CLOSED" or 
  "THROWN" to set the state of the turnout.  
  
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
#include <EEPROM.h>

// pin numbers used
#define UPBTN 16   // D0 Up button for programming servo positions.
#define DNBTN 5    // D1 Down button for programming servo positions.
#define SERVO 4    // D2 PWM Servo output.
#define CLOSED 14  // D5 High when turnout is closed.  Input for driver.
#define THROWN 12  // D6 High when turnout is thrown.  Output to driver chip pins 12 and 19
#define PULSE 13   // D7 Pulsed when turnout is changed.  Output to driver chip pins 2 and 9
#define PUSHBTN 2  // D4 Manual push button for changing turnout from fascia board.
  
// constants for programming mode
#define PROGOFF 0
#define PROGCLOSED 1
#define PROGTHROWN 2
#define EE_READY 12345

const String SensTopic = "/trains/track/sensor/" + String(JMRISensorNumber);    // topic for publishing sensor data
const String OutTopic = "/trains/track/turnout/" + String(JMRITurnoutNumber);    // topic for incoming output commands

const int ServoClosedDefault = 30;            // Turnout closed position in degrees of rotation on the servo
const int ServoThrownDefault = 120;           // Turnout thrown position, max 180 degrees

WiFiClient EspClient;
PubSubClient Client(EspClient);
Servo MyServo;
int EEaddressReady = 0;
int EEaddressClosed = EEaddressReady + sizeof(int);
int EEaddressThrown = EEaddressClosed + sizeof(int);
int EEaddressLastState = EEaddressThrown + sizeof(int);
int ServoClosed;
int ServoThrown;
int ProgramMode = PROGOFF;


void publish(String topic, String payload) {
    Serial.println("Publish topic: " + topic + " message: " + payload);
    Client.publish(topic.c_str() , payload.c_str(), true);
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

  Serial.println("connected. IP address: " + WiFi.localIP().toString());
}



void savePositions() {
 // save the closed and thrown servo positions
  Serial.println("Saving positions, ServoClosed=" + String(ServoClosed)
    + " ServoThrown=" + String(ServoThrown));
  EEPROM.put(EEaddressClosed, ServoClosed);
  EEPROM.put(EEaddressThrown, ServoThrown);
  EEPROM.put(EEaddressReady, EE_READY);
  EEPROM.commit();
}



void saveState(int i) {
  if (RESTORE_LAST_STATE) { 
    // save the last state
    Serial.println("Saving state=" + String(i));
    EEPROM.put(EEaddressLastState, i);
    EEPROM.commit();
  }
}



void sendSensorState(char* payload) {
  // publish the sensor message
  publish(SensTopic.c_str(), payload);
}



void pulseOutput() {
  // for the sake of the snap type turnouts, momentarily pulse the driver chip
  Serial.println("Setting output pin " + String(PULSE) + " to HIGH");
  digitalWrite(PULSE, HIGH);
  delay(PulseTime);
  Serial.println("Setting output pin " + String(PULSE) + " to LOW");
  digitalWrite(PULSE, LOW);
}



void moveServo(int dest) {
  // move the servo, degree by degree, from origin to destination
  int i;
  int origin = MyServo.read();
  Serial.println("moveServo(): Moving from " + String(origin) + " to " + String(dest));
  
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
  Serial.println("Setting output pin " + String(CLOSED) + " to HIGH");
  digitalWrite(CLOSED, HIGH);
  Serial.println("Setting output pin " + String(THROWN) + " to LOW");
  digitalWrite(THROWN, LOW);
  pulseOutput();
  moveServo(ServoClosed);
  sendSensorState("INACTIVE");
  saveState(CLOSED);  
}



void throwTurnout() {
  // set both the snap switch outputs and the servo to the thrown position
  Serial.println("Setting output pin " + String(THROWN) + " to HIGH");
  digitalWrite(THROWN, HIGH);
  Serial.println("Setting output pin " + String(CLOSED) + " to LOW");
  digitalWrite(CLOSED, LOW);
  pulseOutput();
  moveServo(ServoThrown);
  sendSensorState("ACTIVE");  
  saveState(THROWN);
}



void toggleTurnout() {
  // for the push button, change the position of the turnout to the opposite direction
  if (digitalRead(CLOSED))
    throwTurnout();
  else
    closeTurnout();

  while(!digitalRead(PUSHBTN));
}



boolean buttonHeld(int btn) {
  // see if the button was held for an extended period
  for (int i = 0; !digitalRead(btn); i++) {  // wait for button release
    delay(700);
    ESP.wdtFeed();  // retrigger the watchdog timer, just in case  
    
    if (i > 4)                // if greater than 5 seconds
      return(true); 
  }  
    
  return(false);
}



void bumpServo(int offset) {
  int position = MyServo.read();
  // move the servo out and back a little as feedback for the user
  moveServo(position + offset);  // bump the servo for feedback
  moveServo(position);
}



void buttonPushed() {
  // check for the various programming modes, if none set, flip the turnout
  Serial.println("buttonPushed(): ProgramMode=" + String(ProgramMode));
  if (ProgramMode == PROGCLOSED) {
    ProgramMode = PROGTHROWN;
    throwTurnout();
    bumpServo(-10);  // bump the servo for feedback
    Serial.println("Entering programming mode: PROGTHROWN");
  }
  else if (ProgramMode == PROGTHROWN) {
    savePositions();
    ProgramMode = PROGOFF;
    bumpServo(-10);  // bump the servo for feedback
    Serial.println("Exiting programming mode: PROGOFF");
  }
  else if (buttonHeld(PUSHBTN)) {
    ProgramMode = PROGCLOSED;         // set programming mode
    closeTurnout();
    bumpServo(10);  // bump the servo for feedback
    Serial.println("Entering programming mode: PROGCLOSED");
  }
  else {  
    toggleTurnout();
  }

  buttonHeld(PUSHBTN);  // wait for button released
}



void progMoveServo(int offset) {
  int position = MyServo.read();
  // during programming mode, move the servo from position by the amount of offset
  moveServo(position + offset);
  delay(100);
  ESP.wdtFeed();  // retrigger the watchdog timer, just in case  
}



void checkProgBtns() {
  // There doesn't seem to be a pullup for D0 (UP), so I had to connect a diode from D1 to D0 
  // to pull D0 up, but when D0 is pushed, D1 (DOWN) goes low also, so we must check UP first
  // and ignore DOWN if it is pushed at the same time.
  
  if (!digitalRead(UPBTN)) {
    Serial.println("UP pushed");
    if (ProgramMode == PROGCLOSED) {
      ServoClosed++;
      progMoveServo(1);
    }
    else if (ProgramMode == PROGTHROWN) {
      ServoThrown++;
      progMoveServo(1);
    }
  }

  else if (!digitalRead(DNBTN)) {
    Serial.println("DOWN pushed");
    if (ProgramMode == PROGCLOSED) {
      ServoClosed--;
      progMoveServo(-1);
    }
    else if (ProgramMode == PROGTHROWN) {
      ServoThrown--;
      progMoveServo(-1);
    }
  }
}    

  

void callback(char* topic, byte* payload, unsigned int length) {
  // check the incoming message for closed or thrown
  // we only subscribed to turnout messages, so it should be one or the other
  
  Serial.print("Message arrived [" + String(topic) + "] ");

  // terminate the byte array
  payload[length] = 0;

  String msg = String((char *) payload);
  Serial.println(msg);
  
  if (msg == "CLOSED") 
    closeTurnout();
    
  if (msg == "THROWN") 
    throwTurnout();
}



void reconnect() {
  // Loop until we're reconnected
  while (!Client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (Client.connect(String(JMRISensorNumber).c_str())) {
      Serial.println("connected");
      // publish an empty output message to clear any retained messages
      publish(OutTopic.c_str(), "");   
      
      Client.subscribe(OutTopic.c_str());
      Serial.println("Subscribed to : " + String(OutTopic));
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
  Serial.println("Starting setup");

  pinMode(UPBTN, INPUT_PULLUP);  // D0
  pinMode(DNBTN, INPUT_PULLUP);  // D1
  pinMode(CLOSED, OUTPUT);       // D5
  pinMode(THROWN, OUTPUT);       // D6
  pinMode(PULSE, OUTPUT);        // D7
  pinMode(PUSHBTN, INPUT);       // D4
  
  EEPROM.begin(256);
  
  // There are four values saved to EEPROM, the closed and thrown servo positions
  // the ready (initialized) code, and the last state.

  EEPROM.get(EEaddressReady, eeReady); 
   
  if (eeReady == EE_READY) {
    Serial.println("Getting saved positions.");
    EEPROM.get(EEaddressClosed, ServoClosed);
    EEPROM.get(EEaddressThrown, ServoThrown);
  } else {
    Serial.println("Setting default positions.");
    ServoClosed = ServoClosedDefault;
    ServoThrown = ServoThrownDefault;
  }

  Serial.println("ServoClosed=" + String(ServoClosed) + " ServoThrown=" + String(ServoThrown));

  Serial.println("Attaching servo.");
  MyServo.attach(SERVO);         // D2
  MyServo.write(ServoClosed);  // an immediate write prevents the jerk to 90 and back
  closeTurnout();              // follow up with this to set the outputs properly
  Serial.println("Attaching servo done.");

  setup_wifi();
  Client.setServer(MQTTIP, 1883);
  Client.setCallback(callback);
  
  if (RESTORE_LAST_STATE) {
    EEPROM.get(EEaddressLastState, state);
  
    if (state == THROWN)
      throwTurnout();
  }    
}



void loop() {
  // confirm still connected to mqtt server
  
  if (!Client.connected())
    reconnect();
  
  Client.loop();  // not sure about what this does, but it is required

  // check the buttons
  
  if (!digitalRead(PUSHBTN))
    buttonPushed();

  if (ProgramMode)
    checkProgBtns();

  delay(10);
}
