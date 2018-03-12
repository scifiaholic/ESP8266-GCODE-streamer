#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <WiFiManager.h>
#include <WebSocketsServer.h>
#include <SoftwareSerial.h>
#include "SSD1306.h"

SoftwareSerial SWSerial(D2, D1, false, 256);

String command = ""; //buffer to hold incoming Websocket packet,
bool commandEnd = false; //End of websocket command word
int okcount = 0;
String gcodestr = "";
int gcodecount = 0;

WiFiManager wifiManager;

unsigned long LastTime;
unsigned long currentTime;

uint8_t remote_ip;
uint8_t socketNumber;

//#define //DBG_OUTPUT_PORT Serial

// Initialize the OLED display using Wire library
SSD1306  display(0x3c, D3, D4);
String displayArr[7];

void displayUpdate(String Arr[7]){
  display.clear();
  
  display.setFont(ArialMT_Plain_16);
  display.drawString(0,0,Arr[0]);

  display.setFont(ArialMT_Plain_10);
  display.drawString(0,13,Arr[1]);
  display.drawString(0,21,Arr[2]);
  display.drawString(0,29,Arr[3]);
  display.drawString(0,37,Arr[4]);
  display.drawString(0,45,Arr[5]);
  display.drawString(0,53,Arr[6]);
  display.display();
}

ESP8266WebServer server(80);
String webPage = "<h1>You are connected... But nothing is here, sorry.</h1>";
String s = "SD Card info wasn't read...";

// Create a Websocket server
WebSocketsServer webSocket(81);

File root;
File gcode;

// state machine states
unsigned int state;
#define SEQUENCE_IDLE 0x00
#define GET_SAMPLE 0x10
#define GET_SAMPLE__WAITING 0x12

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {
  String payloadstr = (char*)payload;
  switch (type) {
    case WStype_DISCONNECTED:
      //Reset the control to allow for web server to respond.
      state = SEQUENCE_IDLE;

      displayArr[6] = "Client Disconnected";
      displayUpdate(displayArr);
      
      break;
    case WStype_CONNECTED:
      {
        //Display client IP info that is connected in Serial monitor and set control to enable samples to be sent every two seconds (see analogsample() function)
        IPAddress ip = webSocket.remoteIP(num);
        printmsg(String(num) + "Client IP:" + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]));
        socketNumber = num;
        state = GET_SAMPLE;

        displayArr[6] = "Client IP:" + String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        displayUpdate(displayArr);
      }
      break;

    case WStype_TEXT:
      printmsg(String(num) + " get Text: " + payloadstr + "\n");
      if (SD.exists(payloadstr)) {
        gcode = SD.open(payloadstr, FILE_READ);
        gcodecount = 0;
        displayArr[6] = "Run:" + payloadstr;
        displayUpdate(displayArr);
        sendgcode();
      }
      else {
        displayArr[6] = "Err:" + payloadstr;
        displayUpdate(displayArr);        
      }
      break;

    case WStype_ERROR:
      printmsg("Error " + String(num) + payloadstr + " \n");
      break;
  }
}

String printDirectory(File dir, int numTabs) {
  String s = "";
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      printmsg(s + "\n");
      return s;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      s += '\t';
      printmsg("\t");
    }
    s += entry.name();
    printmsg(entry.name());
    if (entry.isDirectory()) {
      s += "/\n";
      printmsg("/");
      s += printDirectory(entry, numTabs + 1);
    }
    entry.close();
    s += ",";
    printmsg(",");
  }
}

void sendgcode() {
  if(gcode.available()) {
    gcodestr = gcode.readStringUntil('\n');
    printmsg("sending gcode line" + String(gcodecount) + ": " + gcodestr + "\n");
    SWSerial.println(gcodestr);
    gcodecount++;
  }
  else {
    printmsg("File End\n");
    gcode.close();
  }
}

void setup() {
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  
  SWSerial.begin(115200);
  
  WiFi.begin();
  
  displayArr[0] = "Connecting...";
  displayArr[1] = "If this fails,";
  displayArr[2] = "connect to SSID: ";
  displayArr[3] = "ESP CNC Help";
  displayUpdate(displayArr);
  
  wifiManager.autoConnect("ESP CNC Help");
  
  displayArr[1] = "ssid:" + String(WiFi.SSID());
  displayUpdate(displayArr);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  displayArr[0] = "IP" + WiFi.localIP().toString();
  displayUpdate(displayArr);
  
  LastTime = millis();

  //First Check to see if we can access the webpage stored onboard in the built in file system (SD)
  if (SD.begin(4)) {
    displayArr[2] = "File system Online";
    displayUpdate(displayArr);

    root = SD.open("/");
    s = printDirectory(root, 0);
    root.close();
    
    displayArr[3] = "SD was loaded";
    displayUpdate(displayArr);
    
    if (SD.exists("/indx.htm")) {
      displayArr[4] = "/indx.htm exists";
      displayUpdate(displayArr);
      
      File f = SD.open("/indx.htm", FILE_READ);
      if (!f) {
        displayArr[5] = "couldn't read /indx.htm";
        displayUpdate(displayArr);
      }
      else {
        webPage = f.readString();

        server.onNotFound([]() {
          server.send(200, "text/html", webPage);
        });
      
        server.on("/inline", []() {
          server.send(200, "text/plain", "this works as well");
        });
      
        server.begin();

        displayArr[5] = "webPage ready";
        displayUpdate(displayArr);

      }
      f.close();    
    }
    else {
      displayArr[4] = "/indx.htm not found";
      displayUpdate(displayArr);
    }    
  }
  else {
    displayArr[3] = "Couldn't open SD";
    displayUpdate(displayArr);
  }

  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

}

void loop() {
  webSocket.loop();

  if (state == SEQUENCE_IDLE);
  else if (state == GET_SAMPLE) state = GET_SAMPLE__WAITING;
  else if (state == GET_SAMPLE__WAITING) {
    currentTime = millis();
    if (currentTime - LastTime > 2000) {
      webSocket.sendTXT(socketNumber, "mySelect|" + s);
      printmsg("SD has been loaded as: " + s + "\n");
      state = SEQUENCE_IDLE;
      LastTime = currentTime;
    }
  }

  server.handleClient();
  HandleGRBL();
}

void printmsg(String msg) {
  webSocket.sendTXT(socketNumber, "ConsoleBox|" + msg);
}

void HandleGRBL() {
  //serial communication watch for response from CNC
  if (SWSerial.available()) {
    char tempChar = (char)SWSerial.read();
    if (tempChar == '\n') {
      commandEnd = true;
      printmsg("found newline, received: " + command + "\n");
    }
    else {
      command = command + tempChar;
    }
  }

  if (commandEnd) {
    printmsg("Parsing: " + command + "\n");
    
    if(command.indexOf("ok") != -1) {
      okcount++;
      printmsg("counting oks: " + String(okcount) + "\n");
    }

    if(okcount == 2) {
      okcount = 0;
      if(!gcode) {
        sendgcode();
      }
    }

    command = "";
    commandEnd = false;
  }
}


