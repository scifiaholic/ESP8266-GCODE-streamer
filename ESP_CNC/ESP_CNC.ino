/* Create a WiFi access point and provide a web server on it so show temperature. */

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>
#include <WebSocketsServer.h>

const char* ssid = "GHome";
const char* password = "#1Marriage08";

unsigned long LastTime;
unsigned long currentTime;

uint8_t remote_ip;
uint8_t socketNumber;

#define DBG_OUTPUT_PORT Serial

ESP8266WebServer server(80);
String webPage = "<h1>You are connected... But nothing is here, sorry.</h1>";

// Create a Websocket server
WebSocketsServer webSocket(81);

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
      if (payload[0] == '#')
      {
        DBG_OUTPUT_PORT.printf("[%u] get Text: %s\n", num, payload);
      }
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
    path += "index.html";
    state = SEQUENCE_IDLE;
  }
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  DBG_OUTPUT_PORT.println("PathFile: " + pathWithGz);
  if (SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)) {
    if (SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
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

  //First Check to see if we can access the webpage stored onboard in the built in file system (SPIFFS)
  bool ok = SPIFFS.begin();
  if (ok)
  {
    DBG_OUTPUT_PORT.println("File System online");
    bool exist = SPIFFS.exists("/index.html");

    if (exist)
    {
      DBG_OUTPUT_PORT.println("The main webpage exists!");
      File f = SPIFFS.open("/index.html", "r");
      if (!f)
      {
        DBG_OUTPUT_PORT.println("Some thing went wrong trying to open the file...");
      }
      else
      {
        //So our main webpage index.html exists and we can read it so lets serve it up!
        webPage = f.readString();
        server.onNotFound([]() {
          server.send(200, "text/html", webPage);
        });

        server.on("/", HTTP_GET, []() {
          handleFileRead("/");
        });

        server.on("/inline", []() {
          server.send(200, "text/plain", "this works as well");
        });

        server.begin();
      }
      f.close();
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
      webSocket.sendTXT(socketNumber, "FileListBox,1," + String(currentTime));
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

















