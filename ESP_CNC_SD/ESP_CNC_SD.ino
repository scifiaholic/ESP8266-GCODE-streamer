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

ESP8266WebServer server(80);
String webPage = "<h1>You are connected... But nothing is here, sorry.</h1>";
String s = "SD Card info wasn't read...";

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
    
    if (SD.exists("/indx.htm"))
    {
      DBG_OUTPUT_PORT.print("/indx.htm");
      DBG_OUTPUT_PORT.println(" exists!");
      
      File f = SD.open("/indx.htm", FILE_READ);
      if (!f)
      {
        DBG_OUTPUT_PORT.println("Something went wrong trying to open the file...");
      }
      else
      {
        webPage = f.readString();

        server.onNotFound([]() {
          server.send(200, "text/html", webPage);
        });
      
        server.on("/inline", []() {
          server.send(200, "text/plain", "this works as well");
        });
      
        server.begin();
        DBG_OUTPUT_PORT.println("Server has begun...");

      }
      f.close();    
    }
    else
    {
      DBG_OUTPUT_PORT.print("/indx.htm");
      DBG_OUTPUT_PORT.println(" not found.");
    }    
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
      webSocket.sendTXT(socketNumber, "mySelect|" + s);
      webSocket.sendTXT(socketNumber, "ConsoleBox|SD has been loaded as: " + s + "\n");
      state = SEQUENCE_IDLE;
      LastTime = currentTime;
    }
  }

  server.handleClient();
  HandleGRBL();
}

void HandleGRBL() {
  //serial communication watch for response from CNC
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













