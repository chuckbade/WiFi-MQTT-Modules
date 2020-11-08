/*
  MQTT Turnout Node
  Chuck Bade 8/21/20
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Servo.h>
#include <EEPROM.h>

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

const char* MqttServer = "192.168.1.13";

// change the following two lines for your sensor/output configuration
const int JMRISensorNumber = 406;  // This is a JMRI number, i.e. DS400, must be unique
const int JMRITurnoutNumber = 72;  // This is a JMRI number, i.e. DT55

// Time between each degree of movement in milliseconds, 25 is nice and slow.
const int ServoDelay = 10;         // If you're not using a servo, set this to zero           
const int PulseTime = 100;         // Time of pulse sent to snap-type turnouts

// Set the following to TRUE if you want the turnout to be set to the previous state
// on startup.
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
*/

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

/*
 * The analog reading reads whatever is on the 5V pin, if there is a 182k resistor 
 * between 5V and A0.  The analogCalibrate variable provides a way of adjusting the 
 * reading for variance in resistor values.  Use a 1% resistor if possible.
 */
const float AnalogCalibrate = 208.2;  // adjusted for 182k 1% from 5V to A0

const String SensTopic = "/trains/track/sensor/" + String(JMRISensorNumber);    // topic for publishing sensor data
const String OutTopic = "/trains/track/turnout/" + String(JMRITurnoutNumber);    // topic for incoming output commands
const String AnalogTopic = "/trains/track/analog/" + String(JMRISensorNumber);  // topic for reporting supply voltage

const int ServoClosedDefault = 30;            // Turnout closed position in degrees of rotation on the servo
const int ServoThrownDefault = 120;           // Turnout thrown position, max 180 degrees

WiFiClient EspClient;
PubSubClient Client(EspClient);
Servo MyServo;
boolean AnalogSent;
int EEaddressClosed = 0;
int EEaddressThrown = EEaddressClosed + sizeof(int);
int EEaddressLastState = EEaddressThrown + sizeof(int);
int ServoClosed;
int ServoThrown;
int ProgramMode = PROGOFF;



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
  EEPROM.commit();
}



void saveState(int i) {
 // save the last state
  Serial.println("Saving state=" + String(i));
  EEPROM.put(EEaddressLastState, i);
  EEPROM.commit();
}



void sendSensorState(char* payload) {
  // publish the sensor message
  Serial.println("Publish topic: " + String(SensTopic) + " message: " + String(payload));
  Client.publish(SensTopic.c_str(), payload, true);
}



void pulseOutput() {
  // for the sake of the snap type turnouts, momentarily pulse the driver chip
  Serial.println("Setting output pin " + String(PULSE) + " to HIGH");
  digitalWrite(PULSE, HIGH);
  delay(PulseTime);
  Serial.println("Setting output pin " + String(PULSE) + " to LOW");
  digitalWrite(PULSE, LOW);
}



void moveServo(int origin, int dest) {
  // move the servo, degree by degree, from origin to destination
  int i;
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
  moveServo(ServoThrown, ServoClosed);
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
  moveServo(ServoClosed, ServoThrown);
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



void bumpServo(int position, int offset) {
  // move the servo out and back a little as feedback for the user
  moveServo(position, position + offset);  // bump the servo for feedback
  moveServo(position + offset, position);
}



void buttonPushed() {
  // check for the various programming modes, if none set, flip the turnout
  Serial.println("buttonPushed(): ProgramMode=" + String(ProgramMode));
  if (ProgramMode == PROGCLOSED) {
    ProgramMode = PROGTHROWN;
    throwTurnout();
    bumpServo(ServoThrown, -10);  // bump the servo for feedback
    Serial.println("Entering programming mode: PROGTHROWN");
  }
  else if (ProgramMode == PROGTHROWN) {
    savePositions();
    ProgramMode = PROGOFF;
    bumpServo(ServoThrown, -10);  // bump the servo for feedback
    Serial.println("Exiting programming mode: PROGOFF");
  }
  else if (buttonHeld(PUSHBTN)) {
    ProgramMode = PROGCLOSED;         // set programming mode
    closeTurnout();
    bumpServo(ServoClosed, 10);  // bump the servo for feedback
    Serial.println("Entering programming mode: PROGCLOSED");
  }
  else {  
    toggleTurnout();
  }

  buttonHeld(PUSHBTN);  // wait for button released
}



void progMoveServo(int position, int offset) {
  // during programming mode, move the servo from position by the amount of offset
  moveServo(position, position + offset);
  delay(100);
  ESP.wdtFeed();  // retrigger the watchdog timer, just in case  
}



void checkProgBtns() {
  // There doesn't seem to be a pullup for D0 (UP), so I had to connect a diode from D1 to D0 
  // to pull D0 up, but when D0 is pushed, D1 (DOWN) goes low also, so we must check UP first
  // and ignore DOWN if it is pushed at the same time.
  
  if (!digitalRead(UPBTN)) {
    Serial.println("UP pushed");
    if (ProgramMode == PROGCLOSED)
      progMoveServo(ServoClosed++, 1);
    else if (ProgramMode == PROGTHROWN)
      progMoveServo(ServoThrown++, 1);
  }

  else if (!digitalRead(DNBTN)) {
    Serial.println("DOWN pushed");
    if (ProgramMode == PROGCLOSED)
      progMoveServo(ServoClosed--, -1);
    else if (ProgramMode == PROGTHROWN)
      progMoveServo(ServoThrown--, -1);
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
  
  Serial.begin(115200);
  Serial.println("Starting setup");
  EEPROM.begin(256);
  
  AnalogSent = true;

  MyServo.attach(SERVO);         // D2

  pinMode(UPBTN, INPUT_PULLUP);  // D0
  pinMode(DNBTN, INPUT_PULLUP);  // D1
  pinMode(CLOSED, OUTPUT);       // D5
  pinMode(THROWN, OUTPUT);       // D6
  pinMode(PULSE, OUTPUT);        // D7
  pinMode(PUSHBTN, INPUT);       // D4

  // There are three values saved to EEPROM, the closed and thrown servo positions
  // and the last state.
  // Get the saved positions
  EEPROM.get(EEaddressClosed, ServoClosed);
  EEPROM.get(EEaddressThrown, ServoThrown);
   
  if (ServoClosed == -1) {
    Serial.println("Setting default positions.");
    ServoClosed = ServoClosedDefault;
    ServoThrown = ServoThrownDefault;
  }

  Serial.println("ServoClosed=" + String(ServoClosed) + " ServoThrown=" + String(ServoThrown));

  
  Serial.println("Analog voltage=" + String(analogRead(A0) / AnalogCalibrate));
  setup_wifi();
  Client.setServer(MqttServer, 1883);
  Client.setCallback(callback);
  
  if (RESTORE_LAST_STATE) {
    EEPROM.get(EEaddressLastState, state);
  
    if (state == CLOSED)
      closeTurnout();
    else
      throwTurnout();
  } 
//  else
  //  closeTurnout();
    
}



void sendAnalog() {
  // This requires a 182k resistor between 5V and A0
  String payload = String(analogRead(A0) / AnalogCalibrate);
  Serial.println("Publish topic: " + String(AnalogTopic) + " message: " + payload);
  Client.publish(AnalogTopic.c_str(), payload.c_str(), true);
}



void loop() {
  long analogTime;
  
  // confirm still connected to mqtt server
  
  if (!Client.connected())
    reconnect();
  
  Client.loop();  // not sure about what this does, but it is required

  // check the buttons
  
  if (!digitalRead(PUSHBTN))
    buttonPushed();

  if (ProgramMode)
    checkProgBtns();

  // if 60 seconds has passed, send the analog value
  
  analogTime = millis() % 60000;  // send the analog value every minute 
  
  if ((!AnalogSent) && (analogTime < 1000)) { 
    sendAnalog();
    AnalogSent = true; 
  }
  
  if (analogTime > 1000) // wait just a bit to make sure we don't retrigger
    AnalogSent = false;
    
  delay(10);
}
