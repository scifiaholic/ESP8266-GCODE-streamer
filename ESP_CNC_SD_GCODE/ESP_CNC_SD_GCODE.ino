/* Create a WiFi access point and provide a web server on it so show temperature. */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <WebSocketsServer.h>

const char* ssid = "RobotNetwork";
const char* password = "nuclearskynet";

unsigned long LastTime;
unsigned long currentTime;

uint8_t remote_ip;
uint8_t socketNumber;

#define DBG_OUTPUT_PORT Serial
#define CNC_OUTPUT_PORT Serial

ESP8266WebServer server(80);
String webPage = "<h1>You are connected... But nothing is here, sorry.</h1>";
String s = "SD Card info wasn't read...";

File gcodefile;
String gcodefilepath = "";
String gcodebuffer = "";
String gcode = "";

// Create a Websocket server
WebSocketsServer webSocket(81);

File root;

// state machine states
unsigned int state;
#define SEQUENCE_IDLE 0x00
#define GET_SAMPLE 0x10
#define GET_SAMPLE__WAITING 0x12

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t lenght) {

  switch (type) {
    case WStype_DISCONNECTED:
      //Reset the control for sending samples of ADC to idle to allow for web server to respond.
      DBG_OUTPUT_PORT.printf("[%u] Disconnected!\n", num);
      state = SEQUENCE_IDLE;
      break;
    case WStype_CONNECTED:
      {
        //Display client IP info that is connected in Serial monitor and set control to enable samples to be sent every two seconds (see analogsample() function)
        IPAddress ip = webSocket.remoteIP(num);
        DBG_OUTPUT_PORT.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        socketNumber = num;
        state = GET_SAMPLE;
      }
      break;

    case WStype_TEXT:
      DBG_OUTPUT_PORT.printf("[%u] get Text: %s\n", num, payload);
      gcodefilepath = (char*)payload;
      break;

    case WStype_ERROR:
      DBG_OUTPUT_PORT.printf("Error [%u] , %s\n", num, payload);
  }
}

String getContentType(String filename) {
  if (server.hasArg("download")) return "application/octet-stream";
  else if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  else if (filename.endsWith(".svg")) return "image/svg+xml";
  return "text/plain";
}

bool handleFileRead(String path) {
  DBG_OUTPUT_PORT.println("handleFileRead: " + path);
  if (path.endsWith("/"))
  {
    path += "indx.htm";
    state = SEQUENCE_IDLE;
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  DBG_OUTPUT_PORT.println("PathFile: " + pathWithGz);
  if (SD.exists(pathWithGz) || SD.exists(path)) {
    if (SD.exists(pathWithGz))
      path += ".gz";
    File file = SD.open(path, FILE_READ);
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void setup() {
  DBG_OUTPUT_PORT.begin(115200);
  WiFi.begin(ssid, password);

  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.print(ssid);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    DBG_OUTPUT_PORT.print(".");
  }
  DBG_OUTPUT_PORT.println("");
  DBG_OUTPUT_PORT.print("Connected to ");
  DBG_OUTPUT_PORT.println(ssid);
  DBG_OUTPUT_PORT.print("IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());

  LastTime = millis();

  //First Check to see if we can access the webpage stored onboard in the built in file system (SD)
  if (SD.begin(4))
  {
    DBG_OUTPUT_PORT.println("File System online");
    root = SD.open("/");
    s = printDirectory(root, 0);
    root.close();
    DBG_OUTPUT_PORT.println("SD was loaded");
    webPage = readfile("/indx.htm");

    server.onNotFound([]() {
      server.send(200, "text/html", webPage);
    });

    server.on("/inline", []() {
      server.send(200, "text/plain", "this works as well");
    });

    server.begin();
    
  }
  else
  {
    DBG_OUTPUT_PORT.println("No such file found.");
  }

  // start webSocket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

}

void loop() {
  webSocket.loop();

  if (state == SEQUENCE_IDLE);
  else if (state == GET_SAMPLE) state = GET_SAMPLE__WAITING;
  else if (state == GET_SAMPLE__WAITING)
  {
    currentTime = millis();
    if (currentTime - LastTime > 2000) {
      webSocket.sendTXT(socketNumber ,"mySelect|" + s);
      DBG_OUTPUT_PORT.print("SD has been loaded as: ");
      DBG_OUTPUT_PORT.println(s);
      state = SEQUENCE_IDLE;
      LastTime = currentTime;
    }
  }

  server.handleClient();
  HandleGRBL();
}

void HandleGRBL() {
  //serial communication watch for response from CNC
  if(gcodebuffer != "") {
    //we have some file contents, so lets pull the next line and send it out to the machine. Lines usually end with \r\n, check
    // for \r to get gcode, remove any lingering \n at the end of this section of code
    int index = gcodebuffer.indexOf("\r");
    
    if(index = -1) {
      //reached the end of the buffer, watch out for partial gcode, save what's left and prep the buffer to be filled again
      gcode = gcodebuffer.substring(0, gcodebuffer.length());
      gcodebuffer = "";
    }
    else if (index = 0){
      //if the lingering gcode from the last buffer ended but just didn't have the \r then send it. 
      //(of course this could just be an empty line, so don't send anything if the gcode was empty)
      if(gcode != "") {
        CNC_OUTPUT_PORT.println(gcode);
        gcode = "";
      }
      gcodebuffer.remove(0,index + 1);
    }
    else {
      //combine any partial gcode with that just found, send and clear, then remove that command from buffer
      gcode = gcode.concat(gcodebuffer.substring(0, index + 1));
      CNC_OUTPUT_PORT.println(gcode);
      gcode = "";
      gcodebuffer.remove(0,index + 1);
    }

    //make sure to remove the \n after we operated using the \r
    if(gcodebuffer[0] == "\n") {
      gcodebuffer.remove(0, 1);
    }
  }
  else if(!gcodefile) {
    //our buffer is empty but we do have a file, see if there is more to buffer
    if(gcodefile.available()){
      gcodebuffer = gcodefile.read();
    }
    else {
      //all done reading this file, close it and reset the filepath so we don't start reading it again.
      gcodefile.close();
      gcodefilepath = "";
    }
  }
  else if(gcodefilepath != "") {
    //no buffer and no file object, but we do have a filepath to open so lets open a file...
    if (SD.exists(gcodefilepath)) {
      gcodefile = SD.open(gcodefilepath, FILE_READ);
    }
    else {
      //filepath doesn't exist on the SD card, reset the filepath so we don't infinitely come to this conclusion again and again.
      gcodefilepath = "";
    }
  }
}

String printDirectory(File dir, int numTabs) {
  String s = "";
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      DBG_OUTPUT_PORT.println(s);
      return s;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      s += '\t';
      DBG_OUTPUT_PORT.print('\t');
    }
    s += entry.name();
    DBG_OUTPUT_PORT.print(entry.name());
    if (entry.isDirectory()) {
      s += "/\n";
      DBG_OUTPUT_PORT.println("/");
      s += printDirectory(entry, numTabs + 1);
    }
    entry.close();
    s += ",";
    DBG_OUTPUT_PORT.print(",");
  }
}

String readfile(String filepath){
  //This will only work if the entire file is about 1KB. 
  String filecontents = "";
  if (SD.exists(filepath))
  {
    DBG_OUTPUT_PORT.print(filepath);
    DBG_OUTPUT_PORT.println(" exists!");
    
    File f = SD.open(filepath, FILE_READ);
    if (!f)
    {
      DBG_OUTPUT_PORT.println("Something went wrong trying to open the file...");
    }
    else
    {
      filecontents = f.readString();
    }
    f.close();    
  }
  else
  {
    DBG_OUTPUT_PORT.print(filepath);
    DBG_OUTPUT_PORT.println(" not found, checking truncated names...");
  }
  return filecontents;
}













