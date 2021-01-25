/*
  MQTT Monitor
  Chuck Bade 01/24/21
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Adafruit_ST7735.h> // Hardware-specific library for ST7735
#include <EEPROM.h>

// Update and uncomment these with values suitable for your network or use an include file.
// Place the file in C:\Users\<name>\Documents\Arduino\libraries\Personal\
//#define MYSSID "mySSID"
//#define PASSWD "myPassword"
//#define MQTTIP "192.168.1.13"

#ifndef MYSSID
#include <SSIDPASSWD.h>
#endif

// change the following line to provide a unique number for the MQTT connection.  It should
// not conflict with any sensor on your layout.
const int JMRISensorNumber = 438;  // Just used for establishing a connection to the server.

/*
  /////////// DON'T CHANGE ANYTHING BELOW THIS LINE /////////////

  Written for Wemos (or clone) D1 Mini for monitoring MQTT activity.

  It connects to the provided WiFi access point using ssid and password and gets its IP address 
  by DHCP.

  It connects to an MQTT server somewhere on the network, using JMRISensorNumber for an ID.  
  Each node connecting to the MQTT broker needs a unique ID, therefore JMRISensorNumber must be
  unique.  If the connection to the MQTT broker is lost, the sketch will attempt to reconnect
  using a blocking reconnect function.   
  
  The D1 Mini has 8 pins that will work for I/O but this app doesn't use any of them.
   
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

#define LCD_CS        15  // D8/GPIO15
#define LCD_RST       12  // D6/GPIO12
#define LCD_DC        16  // D0/GPIO16
#define POT_A         4   // D2/GPIO4  rotary encoder pin labelled CLK
#define POT_B         5   // D1/GPIO5  rotary encoder pin labelled DT
#define POT_SW        2   // D4/GPIO2  rotary encoder pin labelled SW
#define NAVY      0x0008

// Pot Mode Definitions
enum {
  MODEXX,  // null/idle
  MODEMC,  // Message/Clear   
  MODEMX,  // MC Selected
  MODEST,  // Sensor/Turnout   
  MODE50,  // Number by 50    
  MODE01,  // Number by 1
  MODEAI,  // Active/Inactive  
  MODETC,  // Thrown/Closed   
  MODEYN,  // Send Yes/No
  MODEYX,  // Send selected    
  MODECL,  // Clear cancel/start
  MODECX,  // Clear finish     
  MODEIH,  // IP 192.168.?.x  
  MODEIL,  // IP 192.168.x.?
  MODEIY,  // IP save y/n
  MODEIX,  // store IP
};

// instantiate classes
Adafruit_ST7735 Lcd = Adafruit_ST7735(LCD_CS, LCD_DC, LCD_RST);
WiFiClient EspClient;
PubSubClient Client(EspClient);
void dummyFunctionToForcePrototypePlacement() {}

// my menu class
class MyMenu {
  private:
    String values[3];
    int numItems;
    int minInt;
    int maxInt;
    int stepSize;

  public:
    String modeStr;
    int yOffset;
    String name;
    int index;
    String item;

  MyMenu(char* mode) {  // null menu for idle and submit functions
    modeStr = mode;
  }
  
  // this menu is for selecting from a short list of strings
  MyMenu(char* mode, int y, char* nam, int num, char* item1, char* item2, char* item3) {
    modeStr = mode;
    yOffset = y;
    numItems = num;
    maxInt = num - 1;
    minInt = 0;
    stepSize = 1;
    name = nam;
    values[0] = item1;
    values[1] = item2;
    values[2] = item3;
    index = 0;
    item = values[index]; 
  }

  // this menu is for choosing a number between min and max
  MyMenu(char* mode, int y, char* nam, int min, int max, int start, int step) {
    modeStr = mode;
    yOffset = y;
    numItems = 0;
    minInt = min;
    maxInt = max;
    stepSize = step;
    index = start;  
    item = String(index); 
    name = nam;
  }

  void set(int newValue) {  // this method sets the index of the value 
    index = newValue;

    if (numItems == 0)  // if this is a numeric selection menu
      item = String(index);
    else
      item = values[index]; 
  }
  
  void add(int incDec) {  // this method adds or subtracts within limit min and max
    index += (incDec * stepSize);
    if (index < minInt)
      index = minInt;
    if (index > maxInt)
      index = maxInt;

    set(index);
  }
};


// define global variables
String TextList[] = {" ", " ", " ", " ", " ", " ", " ", " "};
const String SubTopic = "#";
const String PubTopic = "/trains/track/";

//     Text Selection:  Mode  Y    Label,   Qty, Item1,     Item2,      Item3
//  Integer Selection:  Mode  Y    Label,   Min, Max,       Initial,    Step
MyMenu Menu[] = {MyMenu("XX"),
                 MyMenu("MC", 40,  "Menu",    3, "MESSAGE", "CLEAR",    "SERVER"),
                 MyMenu("MX"),
                 MyMenu("ST", 40,  "Type",    2, "sensor",  "turnout",  " "),
                 MyMenu("50", 60,  "Addr",    0, 5000,      0,          50),
                 MyMenu("01", 60,  "Addr",    0, 5000,      0,          1),
                 MyMenu("AI", 80,  "Mesg",    3, "ACTIVE",  "INACTIVE", "(none)"),
                 MyMenu("TC", 80,  "Mesg",    3, "THROWN",  "CLOSED",   "(none)"),
                 MyMenu("YN", 100, "Send",    2, "NO",      "YES",      " "),
                 MyMenu("YX"),
                 MyMenu("CL", 110, "Clear",   2, "CANCEL",  "START",    " "),
                 MyMenu("CX"),
                 MyMenu("IH", 71,  "IP High", 1, 255,       1,          1),
                 MyMenu("IL", 91,  "IP Low",  1, 255,       13,         1),
                 MyMenu("IY", 111, "Save",    2, "NO",      "YES",      " "),
                 MyMenu("IX")};

// Global Variables
int Mode = MODEXX;
boolean ModeChanged = false;
boolean PotChanged = false;
int NumCallbacks = 0;
boolean Called = false; 
int LoopNum = 0;
String MsgList;
String MqttIp = MQTTIP;


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



// Add a line to the message list
void addText(String txt) {
  int i; 
  Lcd.setCursor(2, 0);

  // move everything up one line in the array
  for (i = 0; i < 7; i++) {
    TextList[i] =  TextList[i + 1];
    printText(TextList[i]);
  }

  // add the new line at the bottom
  TextList[7] = txt;
  printText(txt);
}



// Redraw the message list
void redrawText() {
  int i; 

  Lcd.fillScreen(NAVY);
  Lcd.setCursor(0, 0);

  for (i = 0; i < 8; i++) 
    printText(TextList[i]);
}


// Clear the screen and redraw the message list
void clearText() {
  int i; 

  for (i = 0; i < 8; i++) 
    TextList[i].remove(0);
  
  redrawText();
}


// Print a message line left justified
void printText(String txt) {
  if (Mode == MODEXX)
    Lcd.printf("%-14s\n", txt.c_str());
}


// This gets called when we receive an MQTT message
void callback(char* topic, byte* payload, unsigned int length) {
  char* msg;
  char* tok[9];
  char typeFlag;
  int outID;
  int i;

  Called = true;
  NumCallbacks++;
  
  // copy the byte string to a char string
  msg = (char *) payload;
  msg[length] = 0;
  Serial.println("[" + String(topic) + "] " + String(msg) );

  // parse the first word from the incoming topic
  tok[0] = strtok(topic, "/");
  
  // parse until there are none left
  for (i = 1; tok[i] = strtok(NULL, "/"); i++);

  // get the ID and the type flag from the parsed data
  // the type flag is the upper cased first letter of the last 
  // word in the topic, i.e. T=turnout S=sensor A=analog
  outID = atoi(tok[i - 1]);
  typeFlag = toUpperCase(tok[i - 2][0]); 
  
  addText(String(typeFlag) + String(outID) + " " + String(msg));
  MsgList.concat("/" + String(typeFlag) + String(outID) + " " + String(msg));
}


// Called when we need to reconnect to the MQTT server
boolean reconnect() {
  Serial.print("Attempting MQTT connection to " + MqttIp + "...");

  // Attempt to connect
  Client.setServer(MqttIp.c_str(), 1883);
  Client.setCallback(callback);
    
  if (Client.connect(String(JMRISensorNumber).c_str())) {
    Serial.println("connected");
    Client.subscribe(SubTopic.c_str());
    Serial.println("Subscribed to : " + SubTopic);
    return(true);
  } else {
    Serial.println("failed, rc=" + String(Client.state()) + " wifi=" + WiFi.status());
    clearScreen();
    Lcd.setCursor(0, 60);
    Lcd.println("MQTT server\n   not found.");
    delay(1500);
    return(false);
  }
}


// Called one time when the sketch starts
void setup() {
  int ipLow;
  int ipHigh;
  
  Serial.begin(115200);
  Serial.println("Starting setup");
  EEPROM.begin(256);


  pinMode(POT_A, INPUT_PULLUP);
  pinMode(POT_B, INPUT_PULLUP);
  pinMode(POT_SW, INPUT_PULLUP);
                    
  // Use this initializer if using a 1.8" LCD screen:
  Lcd.initR(INITR_BLACKTAB);      // Init ST7735S chip, black tab
  Lcd.setRotation(3);   // 0=normal 1=90 2=180 3=270
  Lcd.fillScreen(NAVY);  // clear the screen
  Serial.println(F("LCD initialized"));

  Lcd.setTextWrap(false);
  Lcd.setCursor(45, 10);
  //Lcd.setTextColor(ST77XX_YELLOW, NAVY);
  Lcd.setTextColor(ST77XX_YELLOW, NAVY);
  Lcd.setTextSize(3);
  Lcd.println("MQTT\n Monitor");
  Lcd.setTextSize(1);
  Lcd.println("    (c) 2021 Chuck Bade\n");
  Lcd.setTextSize(2);
  Lcd.println("Connecting to");
  Lcd.print("server.");
  
  setup_wifi();
  
  EEPROM.get(0, ipLow);
  EEPROM.get(sizeof(int), ipHigh);
  Serial.println("ipLow=" + String(ipLow) + " ipHigh=" + String(ipHigh));

  if ((ipLow < 1) || (ipLow > 255)) 
    ipLow = 1;
  if ((ipHigh < 1) || (ipHigh > 255))
    ipHigh = 1;

  Menu[MODEIH].set(ipHigh);
  Menu[MODEIL].set(ipLow);
  MqttIp = "192.168." + String(ipHigh) + "." + String(ipLow);
  Serial.println("\nsetup(): MqttIp from eeprom=" + MqttIp);
  
  Lcd.println(" Done.");
  delay(500);
  Lcd.fillScreen(NAVY);
  Lcd.setCursor(0, 0);

  attachInterrupt(digitalPinToInterrupt(POT_SW), isrPotSw, FALLING); // interrupt on switch falling
}


// Interrupt Service Routine, come here when the pot is pushed
ICACHE_RAM_ATTR void isrPotSw() { 
  noInterrupts();
  while(!digitalRead(POT_SW));   // wait for switch release
  ModeChanged = true;
  Mode++;
  interrupts();
}


// Changes the menu mode up or down depending on the rotation of the pot.
void displayPot(int incDec) {
  Menu[Mode].add(incDec);
  PotChanged = true;
  Serial.println("displayPot(): Mode=" + Menu[Mode].modeStr + " incDec=" + String(incDec) 
    + " item=" + String(Menu[Mode].item));
}


// Interrupt Service Routine, come here when the pot is turned clockwise
ICACHE_RAM_ATTR void isrPotA() { 
  noInterrupts();
  while(!digitalRead(POT_A));   
  while(!digitalRead(POT_B));
  displayPot(+1);
  interrupts();
}


// Interrupt Service Routine, come here when the pot is turned CCW
ICACHE_RAM_ATTR void isrPotB() { 
  noInterrupts();
  while(!digitalRead(POT_B)); 
  while(!digitalRead(POT_A));
  displayPot(-1);
  interrupts();
}


// Display the appropriate menu line for the mode and item selected
void menuLine(int mode) {
  String value;
  
  if (Mode == mode)
    Lcd.setTextColor(NAVY, ST77XX_YELLOW);

  value = Menu[mode].item;

  if (mode == MODE50)
    value += " " + String(char(240)) + "50";

  Lcd.setCursor(0, Menu[mode].yOffset);
  Lcd.printf("%s=%-9s\n", Menu[mode].name.c_str(), value.c_str()); 
  Lcd.setTextColor(ST77XX_YELLOW, NAVY);
}



// This gets called continuously while the sketch is running
void loop() {
  // confirm still connected to mqtt server
  do {
    Called = false;  
    Client.loop();  // Called will be true after a callback is triggered
  } while (Called);
  
  if (!Client.connected() && (Mode != MODEIH) 
    && (Mode != MODEIL) && (Mode != MODEIY) && (Mode != MODEIX)) {
    if (!reconnect()) {
      Serial.println("loop(): reconnect failed.");
      Mode = MODEIH;
      ModeChanged = true;
      attachPots();
    }
  }
  
  if (ModeChanged || PotChanged) { 
    Serial.println("loop(): loop=" + String(LoopNum) + " Mode=" + Menu[Mode].modeStr
      + " ModeChanged=" + String(ModeChanged));
    actionSwitch();

    LoopNum++;
    PotChanged = false;
  }

  delay(10);
}


// Set up the interrupts for the pot turning
void attachPots() {
  attachInterrupt(digitalPinToInterrupt(POT_A), isrPotA, FALLING); // interrupt on pin A falling
  attachInterrupt(digitalPinToInterrupt(POT_B), isrPotB, FALLING); // interrupt on pin B falling
}

// Disable the pot interrupts
void detachPots() {
  detachInterrupt(digitalPinToInterrupt(POT_A));  // turn off interrupts
  detachInterrupt(digitalPinToInterrupt(POT_B));
}


// Fill the screen with Navy Blue, set the font to yellow, size 2
void clearScreen() {
  Lcd.fillScreen(NAVY);
  Lcd.setTextColor(ST77XX_YELLOW, NAVY);
  Lcd.setTextSize(2);
}


void actionSwitch() {
  char topic[80];
  String msg;
  char type;
  int addr;
  int aiOrTc;
   
  if (Menu[MODEST].item == "sensor")
    aiOrTc = MODEAI;
  else
    aiOrTc = MODETC;

  switch(Mode) {
    case MODEXX:
      detachPots();
      clearScreen();
      redrawText();
      ModeChanged = false;
      break;

    case MODEMC:
      if (ModeChanged) {
        clearScreen();
        Lcd.setCursor(0, 15);
        Lcd.printf("%12s\n", "MAIN MENU:");
        attachPots();
        ModeChanged = false;
      }

      menuLine(MODEMC);
      break;

    case MODEMX:
      if (Menu[MODEMC].item == "MESSAGE")
        Mode = MODEST;
      else if (Menu[MODEMC].item == "CLEAR")
        Mode = MODECL;
      else if (Menu[MODEMC].item == "SERVER")
        Mode = MODEIH;
      break;

    case MODEST:
      if (ModeChanged) {
        clearScreen();
        Lcd.setCursor(0, 15);
        Lcd.printf("%-14s\n", "MESSAGE SETUP");
        attachPots();
        PotChanged = true;
        ModeChanged = false;
      }
   
      menuLine(MODEST);
      menuLine(MODE01);
      menuLine(aiOrTc);
      break;

    case MODE50:
      if (ModeChanged) {
        Menu[MODE50].set(Menu[MODE01].index);   // copy previous value from the 01 menu
        ModeChanged = false;
      } 
  
      menuLine(MODEST);
      menuLine(MODE50);
      menuLine(aiOrTc);
      break;

    case MODE01:
      if (ModeChanged) {
        Menu[MODE01].set(Menu[MODE50].index);   // copy the value from the 50 menu
        ModeChanged = false;
      }

      menuLine(MODE01);
      menuLine(aiOrTc);
      break;

    case MODEAI:
      if (ModeChanged) {
        if (Menu[MODEST].item == "turnout")
          Mode = MODETC;
        else  
          ModeChanged = false;
      }

      menuLine(MODE01);
      menuLine(MODEAI);
      break;

    case MODETC:
      if (ModeChanged) {
        if (Menu[MODEST].item == "sensor")
          Mode = MODEYN;
        else
          ModeChanged = false;
      }

      menuLine(MODE01);
      menuLine(MODETC);
      break;

    case MODEYN:
      ModeChanged = false;
      menuLine(aiOrTc);
      menuLine(MODEYN);
      break;

    case MODEYX:
      if (ModeChanged) {
        if (Menu[MODEYN].item == "YES") {
          msg = Menu[aiOrTc].item;
          sprintf(topic, "%s%s/%s", PubTopic.c_str()
            , Menu[MODEST].item.c_str(), Menu[MODE01].item.c_str());
          Serial.println("Publishing message " + String(topic) + " " + msg);  
          Client.publish(topic, msg.c_str(), true);
        }

        Mode = MODEXX;
      }
      break;
      
    case MODECL:
      if (ModeChanged) {
        Serial.println("NumCallbacks=" + String(NumCallbacks));
        clearScreen();
        Lcd.setCursor(0, 5);
        Lcd.println("Retained");
        Lcd.println("Messages=" + String(NumCallbacks));
        Lcd.setTextSize(0);
        //           xxxxxxxxxxxxxxxxxxxxxxxxxxx  size 0 line
        Lcd.println("\nThis function provides a");
        Lcd.println("way to clear retained");
        Lcd.println("messages that can cause");
        Lcd.println("performance issues for the");
        Lcd.println("clients on the network.");
        Lcd.println("This deletes all messages.");
        Lcd.println("Free memory: " + String(ESP.getFreeHeap(),DEC) + " bytes");
        Lcd.setTextSize(2);
        ModeChanged = false;
      } 

      menuLine(MODECL);
      break;

    case 999: //MODECY:
      if (ModeChanged) {
        if (Menu[MODECL].item == "START") {
          clearScreen();
          Lcd.setCursor(0, 5);
          Lcd.println("Retained");
          Lcd.println("Messages=" + String(NumCallbacks));
          Lcd.println("\nDo you want");
          Lcd.println("to clear them");
          Lcd.println("all now?");
          MsgList.remove(0);
          Serial.println("len=" + String(MsgList.length()) + " MsgList=" + MsgList);
          Client.disconnect();   // force a re-subscribe to get a new list of retained messages

          // MsgList should fill up again
          ModeChanged = false;
        } 
        else
          Mode = MODEXX;
      }

//      menuLine(MODECY);
      break;
      
    case MODECX:
      int i;
      
      if (ModeChanged) { 
        if (Menu[MODECL].item == "START") {
          clearScreen();
          Lcd.setCursor(0, 5);
          Lcd.println("\nClearing...");

          while ((i = MsgList.lastIndexOf("/")) != -1) {
            String text = MsgList.substring(i + 1);
            Serial.print("[" + text + "]");
            MsgList.remove(i);
            sscanf(text.c_str(), "%c%i", &type, &addr);
      
            if (type == 'S')
              sprintf(topic, "%ssensor/%d", PubTopic.c_str(), addr);
            else if (type == 'T')  
              sprintf(topic, "%sturnout/%d", PubTopic.c_str(), addr);
            else if (type == 'A')  
              sprintf(topic, "%sanalog/%d", PubTopic.c_str(), addr);
      
            Serial.println(" Publishing empty message to topic " + String(topic));  
            // publish an empty output message to clear retained messages
            Client.publish(topic, "", true);
          }   

          Lcd.println("\nDone.");
          delay(1000);
          clearText();
          Client.disconnect();   // force a re-subscribe to get a new list of retained messages
       }
       Mode = MODEXX;
     }
     break;

    case MODEIH:
      int iph, ipl;
      if (ModeChanged) {
        Serial.println("MqttIp=" + MqttIp);
        sscanf(MqttIp.c_str(), "%*d.%*d.%d.%d", &iph, &ipl);  // parse out the low and high values
        Menu[MODEIH].set(iph);  // set the menu items
        Menu[MODEIL].set(ipl);
       
        clearScreen();
        Lcd.setCursor(0, 5);
        Lcd.println("MQTT Server:");
        Lcd.setTextSize(0);
        Lcd.println("\nThis function provides a");
        Lcd.println("way to change the address");
        Lcd.println("of the MQTT Server.");
        Lcd.println("\nIP=" + MqttIp);
        Lcd.setTextSize(2);
        ModeChanged = false;
      } 

      menuLine(MODEIH);
      menuLine(MODEIL);
      break;

    case MODEIL:
      if (ModeChanged) {
        ModeChanged = false;
      }

      menuLine(MODEIH);
      menuLine(MODEIL);
      break;
      
    case MODEIY:
      if (ModeChanged) {
        ModeChanged = false;
      }

      menuLine(MODEIH);
      menuLine(MODEIL);
      menuLine(MODEIY);
      break;
      
    case MODEIX:
      if (ModeChanged) { 
        clearScreen();

        if (Menu[MODEIY].item == "YES") {
          Lcd.setCursor(0, 5);
          Lcd.println("\nSaving...");
          Serial.println("Saving server IP"); 
          EEPROM.put(0, Menu[MODEIL].item.toInt());
          EEPROM.put(sizeof(int), Menu[MODEIH].item.toInt()); 
          EEPROM.commit();
          MqttIp = "192.168." + Menu[MODEIH].item + "." + Menu[MODEIL].item;
          Serial.println("New MqttIp=" + MqttIp);
          Lcd.println("\nDone.");
          delay(1000);
          Client.disconnect();   // force a re-subscribe to get a new list of retained messages
       }
       Mode = MODEXX;
     }
  }
}
