/*
  DCC++ Distributed MQTT (DCCPPD) Turntable Node
  Chuck Bade 3/24/21
*/

#include <AccelStepper.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <EEPROM.h>
//#include <wdt.h>

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "YourNetwork"
//#define PASSWD "YourPassword"
//#define MQTTIP "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

const int JMRISensorNumber  = 600;  // JMRI number of first sensor, i.e. MS400, must be unique
const int JMRITurnoutNumber = 100;   // JMRI number of first ray (turnout), i.e. MT55

// Change the following three lines for your home sensor and motor configuration.
const int ToHome = -1;    // -1 = CCW
const int Away = 1;       // 1 = CW
const int SensorAtHome = HIGH;
const int MaximumSpeed = 4000;
const int MaxHomingSpeed = 600;
const int AccelerationFactor = 500;  // how quickly it accelerates to max speed
                               // 1 determines how fast the stepper moves while programming
const int ProgStepSize = 10;    // 1=real slow, 5=belt drive turntable, 10=staging yard
const int Ray0Offset = 0;
// Set the following to 9999999 if you want don't want the motor to take the shortest route, but 
// to return the opposite direction from whence it came.  Set to 3200 for direct drive turntable.
const int StepsPerRev = 9999999;  // 3200=direct drive, 19786 belt drive turntable  


/*
  ///////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 mini
  This sketch was written specifically for operating a turntable or transfer table on a 
  model railroad.
  It connects to the provided access point using ssid and password and gets its IP by DHCP.
  The sensor and output numbers are set by the user.
  It connects to an MQTT server ( using mqtt_server ) then, when the stepper motor is commanded
  to move to a ray, it publishes topic: "/trains/track/sensor/###", message: "THROWN" for the 
  selected ray and "CLOSED" for all the other rays, where ### is the selected ray.
  JMRI can command movement by sending topic: "/trains/track/turnout/###", message: "THROWN".  
  "CLOSED" commands are ignored.
  It will reconnect to the server if the connection is lost using a blocking reconnect function.   
  The sketch will also subscribe to messages from the base station and set the turnout when the 
  appropriate messages are received.  
 */


///////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

// pin numbers used
//      GPIO16         D0  unused, seems to interfere with restart
//      GPIO5          D1  unused
#define PUSHBTN2 4  // D2  - push button
#define PUSHBTN1 2  // D4  + push button, pulled up to 3.3v with LED1 and 1k resistor
#define STEP 14     // D5  Step pin on A4988 driver board
#define DIR 12      // D6  Direction pin on A4988 driver board
#define HOME 13     // D7
#define LED 15      // D8  Busy/Idle LED, with 12k pull down

#define BUSY LOW
#define IDLE HIGH
#define MAX_RAYS 32

// topics for publishing sensor data will be built in setup()
String SensorTopic[MAX_RAYS];  
String TurnoutTopic[MAX_RAYS];  

WiFiClient espClient;
PubSubClient client(espClient);
AccelStepper Stpr(1, STEP, DIR);  // type 1 = external driver with Step and Direction

int CurrentRay;
int LastRay = 0;
long RayPosition[MAX_RAYS];
boolean ProgRayMode;
boolean Shortcut = false;


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



void setupWifi() {
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



void sendEvents(int ray) {
  for (int i = 0; i <= LastRay; i++) {
    if (i == ray) 
      publish(SensorTopic[i], "ACTIVE");
    else
      publish(SensorTopic[i], "INACTIVE");        
  }
}



// 3200 steps per revolution
// S = Source
// D = Destination
// If it is shorter to go in the opposite direction, across zero, set the
// destination back or forward by 3200 to force it the other direction.
// After moving to the calculated destination, the position will need to 
// be set to the original destination value.
//
// Algorithm:
// if (S < D) and (3200 - D + S) < (D - S)
//   let Dcalc = D - 3200
// if (D < S) and (3200 - S + D) < (S - D)
//   let Dcalc = D + 3200
//  
// Source   Destination  Calculated Destination
//  1000       3000            -200
//  3000       1000            4200
 
void moveToRay(int rayNum) {
  int i;
  
  if (rayNum == CurrentRay)
    Serial.println("moveToRay, Already at ray " + String(CurrentRay));
  else if (rayNum > LastRay) {   
    Serial.println("moveToRay, Ray " + String(CurrentRay) + " not programmed");
  }
  else {
    long srce = RayPosition[CurrentRay];
    long dest = RayPosition[rayNum];
    Serial.println("moveToRay, From: ray " + String(CurrentRay) + " pos " + String(srce)
      + "  To: ray " + String(rayNum) + " pos " + String(dest));

    // see comment block above this function
    if ((srce < dest) && ((StepsPerRev - dest + srce) < (dest - srce)))
      dest = dest - StepsPerRev;
    else if ((dest < srce) && ((StepsPerRev - srce + dest) < (srce - dest)))
      dest = dest + StepsPerRev;

    CurrentRay = rayNum;
    Stpr.moveTo(dest);

    if (dest != RayPosition[CurrentRay]) {
      Serial.println("moveToRay, shortcut taken, dest=" + String(dest));
      Shortcut = true;
    }

    sendEvents(CurrentRay);
  }
}



long moveHome() {
  int i;
  Serial.print("moveHome() Start...");
  Stpr.setMaxSpeed(MaxHomingSpeed);    // maximum speed after full acceleration

  if (digitalRead(HOME) == SensorAtHome) {  // if the home flag is already set
    Serial.print("Sensor blocked...");
    //Stpr.move(100 * Away);  // a sufficiently large number to make sure the flag is cleared
    Stpr.move(StepsPerRev * Away);  // a sufficiently large number to make sure the flag is cleared

    // move away from the home flag until cleared
    while (digitalRead(HOME) == SensorAtHome) {
      Stpr.run();
      ESP.wdtFeed();  // retrigger the watchdog timer, just in case
    }

    Serial.print("Sensor cleared...");
    Stpr.stop();
  }

  Serial.print("Going home...");
  Stpr.move(StepsPerRev * ToHome);  // move up to one full revolution

  while (digitalRead(HOME) != SensorAtHome) {
    Stpr.run();
    ESP.wdtFeed();  // retrigger the watchdog timer, just in case
  }

  Serial.print("Saving home...");
  int homeLoc = Stpr.currentPosition();   // get position where home sensor triggered
  Stpr.stop();  // stop after deceleration 
  
  while(Stpr.run())
    ESP.wdtFeed();  // retrigger the watchdog timer, just in case  

  Serial.print("Moving to saved...");
  Stpr.moveTo(homeLoc);  // go back to position saved as home
  
  while(Stpr.run())
    ESP.wdtFeed();  // retrigger the watchdog timer, just in case  

  Stpr.setCurrentPosition(0);
  Serial.println("Done.");
  return(homeLoc);
}



boolean buttonHeld(int btn) {
  for (int i = 0; !digitalRead(btn); i++) {  // wait for button release
    delay(700);
    ESP.wdtFeed();  // retrigger the watchdog timer, just in case  
    
    if (i > 4)                // if greater than 5 seconds
      return(true); 
  }  
    
  return(false);
}



void clearAllRays() {
  for (int i = 0; i <= LastRay; i++) {
    int eeAddress = i * sizeof(long);
    Serial.println("Clearing Ray " + String(i) + " eeAddress=" + String(eeAddress));
    EEPROM.put(eeAddress, -1);
  }

  EEPROM.commit();
  LastRay = 0;
  CurrentRay = 0;
}



void button1Pushed() {
  if (ProgRayMode) {
    Stpr.move(ProgStepSize);
  } else {
    if (buttonHeld(PUSHBTN1)) {
      if (!digitalRead(PUSHBTN2)) {  // if both buttons are pressed
        clearAllRays();              // delete all rays
        digitalWrite(LED, BUSY);    // set the LED to BUSY
        moveHome();
        return;        
      }
      
      ProgRayMode = true;         // set programming mode
      Serial.println("Entering programming mode.");
      return;
    }  
 
    // move to next ray
    if (CurrentRay == LastRay) {
      Serial.println("Already at last ray " + String(LastRay));
      moveToRay(0);  // if this is the last ray, the next ray would normally be 0
    }
    else
      moveToRay(CurrentRay + 1);
  }
}



void saveRay(int newPosition) {
  int eeAddress = CurrentRay * sizeof(long);
  Serial.println("Saving Position, CurrentRay=" + String(CurrentRay) + " NewPosition=" 
    + String(newPosition) + " eeAddress=" + String(eeAddress));
  RayPosition[CurrentRay] = newPosition;
  EEPROM.put(eeAddress, newPosition);
  EEPROM.put(eeAddress + sizeof(long), -1);  // clear next ee value 
  EEPROM.commit();
}



void button2Pushed() {
  if (ProgRayMode) {
    long newPosition = Stpr.currentPosition();

    if (newPosition != RayPosition[CurrentRay]) {  // save the new ray position
      //LastRay++;
      CurrentRay++;
      LastRay = CurrentRay;
      saveRay(newPosition);
      subscribe(TurnoutTopic[CurrentRay]);
      digitalWrite(LED, BUSY);    // set the LED to BUSY
    }
    
    if (buttonHeld(PUSHBTN2)) {
      ProgRayMode = false;
      Serial.println("Exiting programming mode.");
      digitalWrite(LED, BUSY);    // set the LED to BUSY
    }

    buttonHeld(PUSHBTN2);
    digitalWrite(LED, IDLE);    // set the LED to IDLE
  } 
  else {
     buttonHeld(PUSHBTN2);

    // move to previous ray
    if (CurrentRay == 0) {
      Serial.println("Already at ray 0");
      moveToRay(LastRay);  // if this is the first ray, the previous ray would normally be the last ray
    }
    else
      moveToRay(CurrentRay - 1);
  }
}



void callback(char* topic, byte* payload, unsigned int length) {
  // check the incoming message for closed or thrown
  // we only subscribed to turnout messages, so it should be one or the other

  char* id;
  int outID;
  int selectedAddr;
  
  Serial.print("Message arrived [" + String(topic) + "] ");

  // terminate the byte array
  payload[length] = 0;
  
  String msg = String((char *) payload);
  Serial.println(msg);

  // parse the first word from the incoming topic
  strtok(topic, "/");

  // parse until there are none left, outID will hold the ID number
  while (id = strtok(NULL, "/")) 
    outID = atoi(id);

  // see if the ID is ours

  for (int i = 0; i <= LastRay; i++) {
    selectedAddr = JMRITurnoutNumber + i;
    
    if ((msg == "THROWN") && (outID == selectedAddr)) {      // thrown means select this ray
      Serial.println("Output ID " + String(outID) + " matched; moving to ray " + String(i));
      moveToRay(i);   
    }
  }
}



void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // Attempt to connect
    if (client.connect(String(JMRISensorNumber).c_str())) {
      Serial.println("connected");
        for (int i = 0; i <= LastRay; i++) 
          subscribe(TurnoutTopic[i]);
    } else {
      Serial.print("failed, rc=" + String(client.state()) + " wifi=" + WiFi.status());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}



void getRayPositions() {
  // get the ray positions, starting at 1
  int eeAddress = 0;
  int position;

  for (LastRay = 1; LastRay < MAX_RAYS; LastRay++) {
    eeAddress = LastRay * sizeof(long);
    EEPROM.get(eeAddress, position);

    if (position < 1) {
      LastRay--;
      break;
    }

    RayPosition[LastRay] = position;
    
    Serial.println("eeAddress=" + String(eeAddress) + " LastRay=" + String(LastRay) 
      + " value=" + String(RayPosition[LastRay]));
  }
  Serial.println("LastRay=" + String(LastRay));
}



void testRotationCount() {
  long homeLoc;
  long sum = 0;
  float avg;
      
  Serial.println("testRotationCount() Start...");
  Stpr.setAcceleration(AccelerationFactor);  // acceleration factor, library default is 50
  Stpr.setMaxSpeed(MaxHomingSpeed);    // maximum speed after full acceleration
  
  for (int n = 0;;n++) {  // this runs forever
    homeLoc = abs(moveHome());
    Stpr.move(700 * ToHome);  // move up to one full revolution
    
    while(Stpr.run())
      ESP.wdtFeed();  // retrigger the watchdog timer, just in case

    if (n > 0) {
        sum += homeLoc;
        avg = (float) sum / n;
        Serial.println("n=" + String(n) + " count=" + String(homeLoc)
          + " average=" + String(avg));
    }
  }
}



void setup() {
  Serial.begin(115200);
  Serial.println("Starting setup");
  EEPROM.begin(256);
  ESP.wdtDisable();
  wdt_disable();

  pinMode(STEP, OUTPUT);
  pinMode(DIR, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(PUSHBTN1, INPUT);
  pinMode(PUSHBTN2, INPUT_PULLUP);
  pinMode(HOME, INPUT_PULLUP);

  if (!digitalRead(PUSHBTN2))
    testRotationCount();

  setupWifi();
  client.setServer(MQTTIP, 1883);
  client.setCallback(callback);

  for (int i = 0; i < MAX_RAYS; i++) {
    // topics for publishing sensor data and getting turnout commands
    SensorTopic[i] = "/trains/track/sensor/" + String(JMRISensorNumber + i);  
    TurnoutTopic[i] = "/trains/track/turnout/" + String(JMRITurnoutNumber + i);  
  }
  
  RayPosition[0] = Ray0Offset;

  getRayPositions();
  
  // get the ray positions
  int eeAddress = 0;
  int position;

  while (LastRay < MAX_RAYS) {
    EEPROM.get(eeAddress, position);

    if (position < 1)
      break;

    RayPosition[++LastRay] = position;
    
    Serial.println("eeAddress=" + String(eeAddress) + " LastRay=" + String(LastRay) 
      + " value=" + String(RayPosition[LastRay]));
   
    eeAddress += sizeof(long);
  }

  Stpr.setAcceleration(AccelerationFactor);  // acceleration factor, library default is 50

  moveHome();
  Stpr.setMaxSpeed(MaximumSpeed);    // maximum speed
  CurrentRay = 0;
  ProgRayMode = false;
  moveToRay(0);
  digitalWrite(LED, IDLE);
}

    

void loop() {
  // confirm still connected to mqtt server
  if (!client.connected())
    reconnect();
  
  client.loop();
 
  if (!digitalRead(PUSHBTN1))
    button1Pushed();

  if (!digitalRead(PUSHBTN2))
    button2Pushed();

  // Move the motor
  if (Stpr.run()) {
    digitalWrite(LED, BUSY);    // set the LED to BUSY
  } else {
    if (Shortcut) {
      Serial.println("loop, stopped on shortcut. position=" + String(Stpr.currentPosition()));
      Stpr.setCurrentPosition(RayPosition[CurrentRay]);
      Serial.println("loop, stopped on shortcut. corrected position=" + String(Stpr.currentPosition()));
      Shortcut = false;
    }
      
    digitalWrite(LED, IDLE);    // set the LED to IDLE
  }
  //delay(10);
}
