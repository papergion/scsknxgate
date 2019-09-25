//
//-----------------------work in progress: riadattamento a knx <----------------
//---------------------------------------: riverifica su scs   <----------------
//
// i comandi dimmer alza e abbassa mandano i cmd seriali ma che non fanno niente (errore gate)
//           {A7}{6D}{33}{31}
// dopo tanti abbassa il dimmer va in off - quando si alza alexa manda ON e BRI ma BRI viene perso
//      per UART occupata...
//           {A7}{79}{01}{0B}{33}{81}
//           UART in use...
//----------------------------------------------------------------------------------
#define _FW_NAME     "SCSKNXGATE"
#define _FW_VERSION  "VER_4.346"
#define _ESP_CORE    "esp8266-2.5.2"  //  flash 36%   ram 43%
//----------------------------------------------------------------------------------
//
//               ---- attenzione - porta http: 8080 <--se alexaParam=y------------------------

#define KNX
//#define SCS
//#define DEBUG

//#define NO_ALEXA_MQTT
#define USE_TCPSERVER
#define TCP_PORT 5045

#define USE_OTA
//#define COMMIT_RARE

/*
  scsknxgate - gateway between KNXgate/SCSgate and ethernet application   UDP / HTTP / MQTT

  V 4.2 - use TCP for download and update device tab
  V 4.1 - use OTA for wifi firmware update
  V 4.0 - use fauxmo for direct connection with echo dot devices
  V 3.7 - homeassistant mode parametrico - scrittura UART diretta senza TX queue
  V 3.6 - gestione log mqtt
  V 3.3 - gestione tapparelle percentuale - SOLO SCS
  V 3.2 - MQTT scs & knx
  V 2.0 - risponde anche a comandi HTTP, tipo:
                      http://192.168.2.230/gate.htm?type=12&from=01&to=31&cmd=01&resp=y

          la porta di ricezione e' quella prevista in configurazione
          la pagina di comando si chiama gate.htm
          i parametri possibili sono  type=   12 (comando, byte 4 del telegramma) default 12
                                      from=   NN  mio indirizzo hex - default 01 hex
                                      to=     NN  indirizzo hex del device destinatario
                                      cmd=    NN  hex comando da mandare
                                      resp=   y     "y" request to send a response, otherwise no response

          e' possibile utilizzare una finestra di aiuto con
                      http://192.168.2.230/request

          se si usa l'opzione resp=y attiva il log breve e per ogni telegramma di ritorno (l=4) effettua una chiamata al client:
          http://<ipaddress>:8080/json.htm?type=command&param=udevices&script=scsgate_json.lua&type=<byte[3]>&from=<byte[2]>&to=<byte[1]>&cmd=<byte[4]>
          è parametrizzabile la parte:
                            :8080/json.htm?type=command&param=udevices&script=scsgate_json.lua
                            (esempio conforme a DOMOTICZ e script  SCSGAtE_JSON.LUA

  V 1.6 - reset/restart se non aggancia il router con partenza normale (senza jumper) - OBBLIGATORIO IL JUMPER PER PROGRAMMARE *********************
  V 1.5 - aggiunta parametrizzazione numero porta udp da usare (default 52056)
  V 1.4 - ottimizzazione tempi di attesa
  V 1.3 - in modalita'  di connessione client NON attiva il server http per migliorare i tempi di risposta

  // --------------------------------------------------------------------------------------------------------------------------------------------------
  chiamate http:
    server.on ( "/", handleScan );                  // elenco reti wifi <- solo in modalita AP
    server.on ( "/", handleRoot );                  // hello <- solo in modalità Wifi CLIENT
    server.on ("/status", handleStatus);            // status display
    server.on ("/setting", handleSetting);          // setup wifi client
    server.on ("/reset", handleReset);              // reset app  ?device= <esp>|<pic>
    server.on ("/request", handleRequest);          // mappa richiesta comandi scs
    server.on ("/gate.htm", handleGate);            // esecuzione comando scs
    server.on ("/gate", handleGate);                // esecuzione comando scs
    server.on ("/callback", handleCallback);        // richiesta setup callback http
    server.on ("/backsetting", handleBackSetting);  // setup callbackhttp
    server.on ("/mqttconfig", handleMqttConfig);    // richiesta setup mqtt
    server.on ("/mqttcfg", handleMqttCFG);          // setup mqtt
    server.on ("/mqttdevices", handleMqttDevices);  // inizio processo di censimento automatico dei devices scs
                                                    // ?request=clear prepare start  stop  term  query resend
    server.on ("/devicename", handleDeviceName);    // processo di rinominazione dei devices per alexa
    server.onNotFound ( handleNotFound );
  // --------------------------------------------------------------------------------------------------------------------------------------------------
*/

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
//#include <ESP8266httpUpdate.h>

#include <ESP8266mDNS.h>
#include "PubSubClient.h"
#include <WiFiUdp.h>
//#include <fs.h>
#include <ArduinoOTA.h>

#include <EEPROM.h>

extern "C" {
#include "user_interface.h"
}

#ifdef USE_TCPSERVER
  WiFiServer tcpserver(5045);
  WiFiClient tcpclient;
#endif

// =======================================================================================================================
  typedef union _WORD_VAL
  {
    int  Val;
    char v[2];
    struct
    {
        char LB;
        char HB;
    } byte;
  } WORD_VAL;
// =======================================================================================================================
#define WEB_CLIENT
#define HTTP_PORT 80
unsigned int http_port = 80;
// =======================================================================================================================
#ifdef KNX
#define _MODO "KNX"
#define _modo "knx"
#define ID_MY "interfaccia konnex"
#endif

#ifdef SCS
#define _MODO "SCS"
#define _modo "scs"
#define ID_MY "interfaccia scs"
#endif
// =======================================================================================================================

#define MYPFX  _modo

#define DEVICEREQUEST 'D'

#define SUBSCRIBE1 MYPFX "/+/set/+"
#define SUBSCRIBE2 MYPFX "/+/setlevel/+"
#define SUBSCRIBE3 MYPFX "/+/setposition/+"

#define NEW_SWITCH_TOPIC "homeassistant/switch/";
#define NEW_LIGHT_TOPIC  "homeassistant/light/";
#define NEW_COVER_TOPIC  "homeassistant/cover/";
#define NEW_CONFIG_TOPIC "/config";
#define NEW_COVERPCT_TOPIC  "homeassistant/cover/";
#define NEW_SENSOR_TOPIC  "homeassistant/sensor/";

#define NEW_DEVICE_NAME  "{\"name\": \""

#define     SWITCH_SET   MYPFX "/switch/set/"
#define NEW_SWITCH_SET   "\",\"command_topic\": \"" SWITCH_SET
#define     SWITCH_STATE MYPFX "/switch/state/"
#define NEW_SWITCH_STATE "\",\"state_topic\": \"" SWITCH_STATE

#define NEW_LIGHT_SET    NEW_SWITCH_SET
#define NEW_LIGHT_STATE  NEW_SWITCH_STATE

#define     BRIGHT_SET   MYPFX "/switch/setlevel/"
#define NEW_BRIGHT_SET   "\",\"brightness_command_topic\": \"" BRIGHT_SET
#define     BRIGHT_STATE MYPFX "/switch/value/"
#define NEW_BRIGHT_STATE "\",\"brightness_state_topic\": \"" BRIGHT_STATE

#define     COVER_SET    MYPFX "/cover/set/"
#define NEW_COVER_SET    "\",\"command_topic\": \"" COVER_SET
#define     COVER_STATE  MYPFX "/cover/state/"
#define NEW_COVER_STATE  "\",\"state_topic\": \"" COVER_STATE

#define     COVERPCT_SET    MYPFX "/cover/setposition/"
#define NEW_COVERPCT_SET    "\",\"set_position_topic\": \"" COVERPCT_SET
#define     COVERPCT_STATE  MYPFX "/cover/value/"
#define NEW_COVERPCT_STATE  "\",\"position_topic\": \"" COVERPCT_STATE

#define     SENSOR_TEMP_STATE MYPFX "/sensor/temp/state/"
#define NEW_SENSOR_TEMP_STATE "\",\"state_topic\": \"" SENSOR_TEMP_STATE
#define NEW_SENSOR_TEMP_UNIT "\",\"unit_of_measurement\": \"°C"

#define     SENSOR_HUMI_STATE MYPFX "/sensor/humi/state/"
#define NEW_SENSOR_HUMI_STATE "\",\"state_topic\": \"" SENSOR_HUMI_STATE
#define NEW_SENSOR_HUMI_UNIT "\",\"unit_of_measurement\": \"%"

#define     SENSOR_PRES_STATE MYPFX "/sensor/pres/state/"
#define NEW_SENSOR_PRES_STATE "\",\"state_topic\": \"" SENSOR_PRES_STATE
#define NEW_SENSOR_PRES_UNIT "\",\"unit_of_measurement\": \"mBar"

#define NEW_DEVICE_END   "\"}";
// =======================================================================================================================
#include "fauxmoESP.h"
fauxmoESP fauxmo;
unsigned char id_interfaccia_scs_knx = 0;
unsigned char id_fauxmo = 0;
unsigned char ArduinoOTAflag = 0;
#ifdef BLINKLED
char ledCtr = 0;
#endif
// =======================================================================================================================
char mqtt_server[32];
char mqtt_user[32];
char mqtt_password[20];
char mqtt_log;
char mqtt_persistence = 0;
char mqtt_port[6];
char mqtt_retry = 0;
char mqtt_retrylimit = 0;
char mqttSectorLine = 0;    // knx sector/line
char domoticMode;    // d=as domoticz      h=as homeassistant
char alexaParam = 0;

WiFiClient espClient;

PubSubClient client(espClient);

unsigned char mqttopen = 0;

long lastMsg = 0;
long lastRecu = 0;

#ifdef SCS
char prevDevice;
#endif
#ifdef KNX
word prevDevice;
#endif

char prevAction;
char internal;
unsigned char devIx = 0;
unsigned char devCtr = 0;
char uartSemaphor = 0;

// =======================================================================================================================

#define INNERWAIT   90  // inner loop delay
#define OUTERWAIT  120  // outer loop delay

long prevTime = 0;

#ifdef DEBUG
int  counter;
#endif

char picfwVersion[16];

ESP8266WebServer server(HTTP_PORT);

HTTPClient httpClient;

bool testWifi(void);
void setupAP(void);
void launchWeb(int webtype);
void createWebServer(int webtype);

const char* ssid = "ESP_" _MODO "GATE";
const char* passphrase = _modo "gate1";

IPAddress local_ip(0, 0, 0, 0);
IPAddress router_ip(0, 0, 0, 0);
IPAddress udp_remote_ip;
IPAddress tcp_remote_ip;
char connectionType;  // 1=AP    0=router
unsigned int udp_remote_port;
unsigned int tcp_remote_port;
char   webon = 0;
char   forceAP = 0;
String wifiSignals;
String content;

char   httpCallback[128];
int    statusCode;
char   udpopen = 0;
char   tcpopen = 0;
char   tcpuart = 0;     // 0=ritorno seriale su UDP   1=ritorno seriale su TCP    2=debug messg su TCP
String httpResp = "";   // resp= nessuna risposta      resp=i conferma immediata       resp=a risposta "get" con caratteri SCSGATE      resp=y  entrambe

char udpBuffer[255];
char requestBuffer[36];
unsigned char requestLen = 0;
char asynchBuffer[16];
unsigned char asynchLen = 0;

char serObuffer[2,16];
char serOlen[2] = {0,0};
char bufSemaphor = -1;


char replyBuffer[255];
unsigned char replyLen;
unsigned char firstTime = 0;

unsigned int udpLocalPort = 52056;
WiFiUDP udpConnection;



// -----------------------------------------------------------------------------------------------------------------------------------------------
#define MAXEEPROM 4096  // 4096 is the MAX eeprom size in esp8266 

#define E_SSID   0         // ssid wifi  max 32
#define E_PASSW  32        // passw wifi max 32
#define E_IPADDR 100       // my ipaddr  max 16
#define E_ROUTIP 120       // router ip  max 16
#define E_PORT   140       // udp port   max 6
#define E_CALLBACK    150  // http callb max 100

#define E_MQTT_BROKER 250  // mqtt_server   max 32
#define E_MQTT_PORT   285  // mqtt_port     max 6
#define E_DOMOTICMODE 291  // domotic mode  max 1
#define E_MQTT_USER   295  // mqtt_user     max 32
#define E_MQTT_PSWD   330  // mqtt_password max 32
#define E_MQTT_LOG    362  // mqtt_log      max 1
#define E_MQTT_PERSISTENCE    363  // mqtt_persistence      max 1
#define E_ALEXA       364  // alexa interface max 1

#define E_MQTT_TABDEVICES 384 // mqtt_device max 168 ->  552
//
// per SCS indirizzo relativo (a E_MQTT_TABDEVICES) corrisponde a ID domoticz  e corrisponde a ID scs
// per KNX indirizzo relativo (a E_MQTT_TABDEVICES) va shiftato a SX (*2) e sottratto 1
//                                                  corrisponde cosi a ID domoticz e base KNX (dispari) -> LB
//                               E_MQTT_TABDEVICES [0]   contiene      ID domoticz e base KNX         ---> HB
//                               E_MQTT_TABDEVICES [indirizzo] contiene devtype
//                                                                      1 = "switch"
//                                                                      3 = "dimmer"
//                                                                      4 = "dimmer knx"
//                                                                      8 = "cover"
//                                                                      9 = "coverpct"

char alexa_BUS_id[168]; // pointer: id alexa   -   contenuto: device address reale (scs o knx)

#define E_ALEXA_DESC_DEVICE 560 // device description max 168 x E_ALEXA_DESC_LEN char 
#define E_ALEXA_DESC_LEN     20 // device description max length - top 

// -----------------------------------------------------------------------------------------------------------------------------------------------
bool testWifi(void) {
  int c = 0;
#ifdef DEBUG
  Serial.println("Waiting for Wifi to connect");
#endif
  while ( c < 22 ) {
    if (WiFi.status() == WL_CONNECTED)
    {
#ifdef DEBUG
      Serial.println("Connected!");
#endif
      return true;
    }
    delay(500);
#ifdef DEBUG
    Serial.print(WiFi.status());
#endif
    c++;
  }
#ifdef DEBUG
  Serial.println("");
  Serial.println("Connect timed out, opening AP");
#endif
  return false;
}

// -----------------------------------------------------------------------------------------------------------------------------------------------
void launchWeb(int webtype)
{ // webtype=1 : AP       webtype=0 : WIFI connected to router
  connectionType = webtype;
#ifdef DEBUG
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("SoftAP IP: ");
  Serial.println(WiFi.softAPIP());
#endif


#ifndef WEB_CLIENT
  if (webtype == 1)
#endif
  {
    createWebServer(webtype);
    // Start the server
    server.begin(http_port);
    webon = 1;
#ifdef DEBUG
    Serial.println("Server started");
#endif
  }

  /*
    // Add service to MDNS-SD (andrebbe dopo il server.begin)
    MDNS.addService("http", "tcp", 80);
  */

}

// -----------------------------------------------------------------------------------------------------------------------------------------------
void setupAP(void)
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();
#ifdef DEBUG
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*");
      delay(10);
    }
  }
  Serial.println("");
#endif
  wifiSignals = "<ol>";
  for (int i = 0; i < n; ++i)
  {
    // Print SSID and RSSI for each network found
    wifiSignals += "<li>";
    wifiSignals += WiFi.SSID(i);
    wifiSignals += " (";
    wifiSignals += WiFi.RSSI(i);
    wifiSignals += ")";
    wifiSignals += (WiFi.encryptionType(i) == ENC_TYPE_NONE) ? " " : "*";
    wifiSignals += "</li>";
  }
  wifiSignals += "</ol>";
  delay(100);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, passphrase, 8);
#ifdef DEBUG
  Serial.println("softap");
#endif
  launchWeb(1);
#ifdef DEBUG
  Serial.println("open UDP port");
#endif
  udpopen = udpConnection.begin(udpLocalPort);
#ifdef DEBUG
  if (udpopen == 1)
    Serial.println ( "UDP server started" );
  else
    Serial.println ( "UDP open ERROR" );
#endif

}

// =============================================================================================
void handleNotFound() {
  String message = "File non trovato\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  EEPROM.commit();
}


// =============================================================================================
char aConvert(String aData)
{
  char str[4];
  char *ptr;
  long ret;
  aData.toCharArray(str, 4);
  str[2] = 0;
  ret = strtoul(str, &ptr, 16);
  return (char) ret;
}
// =============================================================================================
word wConvert(String aData)
{
  char str[6];
  char *ptr;
  long ret;
  aData.toCharArray(str, 5);
  str[4] = 0;
  ret = strtoul(str, &ptr, 16);
  return (word) ret;
}
// =============================================================================================
void handleRequest()
{
  IPAddress ip = WiFi.softAPIP();

  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION;
  content += "<p>";
  content += "</p><form method='get' action='gate'>";
#ifdef SCS
  content += "<label>Req Type: </label><input name='type' maxlength=2 value='12' style='width:25px'>";
#endif
  content += "<label>From: </label><input name='from' maxlength=2 value='01' style='width:25px'>";
#ifdef SCS
  content += "<label>to:</label><input name='to' maxlength=2 style='width:25px'>";
#endif
#ifdef KNX
  content += "<label>to:</label><input name='to' maxlength=4 style='width:50px'>";
#endif
  content += "<label>command</label><input name='cmd' maxlength=2 style='width:25px'>\
    <label>reply</label><input name='resp' maxlength=1 value='y' style='width:15px'>\
    <input type='submit'></form>";

  content += "</html>";
  server.send(200, "text/html", content);
}
// =============================================================================================
void setFirst(void)
{
  uartSemaphor = 1;
  requestBuffer[requestLen++] = '@';
  requestBuffer[requestLen++] = 0x15; // evita memo in eeprom

  requestBuffer[requestLen++] = '@';
  requestBuffer[requestLen++] = 'M';
  requestBuffer[requestLen++] = 'X';

  requestBuffer[requestLen++] = '@';

  requestBuffer[requestLen++] = '@';
  requestBuffer[requestLen++] = 'Y';
  requestBuffer[requestLen++] = '1';

  requestBuffer[requestLen++] = '@';
  requestBuffer[requestLen++] = 'F';
#ifdef SCS
  requestBuffer[requestLen++] = '3';
#endif
#ifdef KNX
  requestBuffer[requestLen++] = '2';
#endif
  requestBuffer[requestLen++] = '§';
  requestBuffer[requestLen++] = 'l';
  uartSemaphor = 0;

  firstTime = 1;
  udp_remote_ip = {0, 0, 0, 0}; // kill udp connection
}
// =============================================================================================
void handleGate() //  ipaddress/request?type=x&from=xx&to=yy&cmd=zz&resp=y
//          i parametri possibili sono  type=   12 (comando, byte 4 del telegramma) default 12 (SOLO SCS)
//                                      from=   NN  mio indirizzo hex - default 01 hex
//                                      to=     NN  indirizzo hex del device destinatario (KNX NNNN)
//                                      cmd=    NN  hex comando da mandare
//                                      resp=   y     "y" request to send a response, otherwise no response
{

  //    if(!server.authenticate(www_username, www_password))
  //        return server.requestAuthentication();

  String from;
  char  cfrom;
  String to;
#ifdef SCS
  String type;
  char  ctype;
  char  cto;
#endif
#ifdef KNX
  word  cto;
#endif
  String cmd;
  char  ccmd;


  WiFiClient client = server.client();
  //    if ((client.available()) && (client.connected()))
  if (client.connected())
  {
#ifdef DEBUG
    Serial.println("");
    Serial.print("New client:");
    Serial.print(client.remoteIP());
    Serial.print(" port:");
    Serial.println(client.remotePort());
#endif
    tcp_remote_ip = client.remoteIP();
    tcp_remote_port = client.remotePort();
  }

#ifdef SCS
  if (server.hasArg("type"))
  {
    type = server.arg("type");
    ctype = aConvert(type);
    if (ctype == 0) ctype = 0x12;
  }
  else
    ctype = 0x12;
#endif

  if (server.hasArg("from"))
  {
    from = server.arg("from");
    cfrom = aConvert(from);
  }
  else
    cfrom = 0x00;
  if (server.hasArg("to"))
  {
    to = server.arg("to");
#ifdef SCS
    cto = aConvert(to);
#endif
#ifdef KNX
    cto = wConvert(to);
#endif
  }
  else
    cto = 0x00;
  if (server.hasArg("cmd"))
  {
    cmd = server.arg("cmd");
    ccmd = aConvert(cmd);
  }
  else
    ccmd = 0x01;


  if (server.hasArg("resp"))
  {
    httpResp = server.arg("resp");
  }
  else
    httpResp = "";


#ifdef DEBUG
  Serial.println(" ");
#ifdef SCS
  Serial.print(" t=");
  if (server.hasArg("type"))
  {
    Serial.print(type);
    Serial.print(":");
  }
  Serial.println(ctype, HEX);
#endif

  Serial.print(" f=");
  if (server.hasArg("from"))
  {
    Serial.print(from);
    Serial.print(":");
  }
  Serial.println(cfrom, HEX);

  Serial.print(" a=");
  if (server.hasArg("to"))
  {
    Serial.print(to);
    Serial.print(":");
  }
  Serial.println(cto, HEX);

  Serial.print(" c=");
  if (server.hasArg("cmd"))
  {
    Serial.print(cmd);
    Serial.print(":");
  }
  Serial.println(ccmd, HEX);
#endif
  {
    if (firstTime == 0)
      setFirst();

    // tapparelle percentuale
    if (cfrom == 0xFE) // <================COVERPCT==================
    {
      uartSemaphor = 1;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'u';
      requestBuffer[requestLen++] = cto;
      requestBuffer[requestLen++] = ccmd;
      uartSemaphor = 0;
    }
    else
    {
      uartSemaphor = 1;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'y';

#ifdef SCS
      requestBuffer[requestLen++] = cto;
      requestBuffer[requestLen++] = cfrom;
      requestBuffer[requestLen++] = ctype;
      requestBuffer[requestLen++] = ccmd;
#endif

#ifdef KNX
      requestBuffer[requestLen++] = cfrom;
      char a1 = highByte(cto);
      char a2 = lowByte(cto);
      requestBuffer[requestLen++] = a1;
      requestBuffer[requestLen++] = a2;
      requestBuffer[requestLen++] = ccmd;
      uartSemaphor = 0;
#endif
    }
    if ((httpResp == "y") || (httpResp == "i"))
    {
      content = "{\"status\":\"OK ";
      content += "\"}";
      statusCode = 200;
      server.send(statusCode, "text/html", content);
    }
  }
}
// =============================================================================================
void handleMqttConfig()
{
  IPAddress ip = WiFi.softAPIP();
  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION;
  content += "<p>";
  content += "</p><form method='get' action='mqttcfg'>\
    <label>MQTT broker: </label><input name='broker' maxlength=32 value='";
  content += String(mqtt_server);
  content += "' style='width:130px'>\
    <label>port: </label><input name='port' maxlength=5 value='";
  content += String(mqtt_port);
  content += "' style='width:50px'> ";

  content += "<label>user:</label><input name='user' maxlength=32 value='";
  content += String(mqtt_user);
  content += "' style='width:160px'>\
    <label>password: </label><input name='pswd' maxlength=20 value='";
  content += String(mqtt_password);
  content += "' style='width:160px'>\
    <label>domotic(h):</label><input name='dom' maxlength=1 value='";
  content += String(domoticMode);
  content += "' style='width:20px'>\
    <label>log(y):</label><input name='log' maxlength=1 value='";
  content += String(mqtt_log);
  content += "' style='width:20px'>\
    <label>persistence(y):</label><input name='persistence' maxlength=1 value='";
  if (mqtt_persistence == 1)
    content += "y";
  else
    content += "n";
  content += "' style='width:20px'>\
    <label>alexa(y):</label><input name='alexa' maxlength=1 value='";
  if ( alexaParam == 'y')
    content += "y";
  else
    content += "n";

  content += "' style='width:20px'><input type='submit'></form> </html>";
  server.send(200, "text/html", content);
}

// =============================================================================================
void handleMqttCFG()
{
  IPAddress ip = WiFi.softAPIP();

  int i;
  String broker = server.arg("broker");
  String port = server.arg("port");
  String user = server.arg("user");
  String pswd = server.arg("pswd");
  String log  = server.arg("log");
  String dom  = server.arg("dom");
  String pers  = server.arg("persistence");
  String alex  = server.arg("alexa");
  if (dom[0] == 'H') dom[0] = 'h';
  if (log[0] == 'Y') log[0] = 'y';
  if (pers[0] == 'Y') pers[0] = 'y';
  if (alex[0] == 'Y') alex[0] = 'y';
  
#ifdef NO_ALEXA_MQTT
  if ((broker.length() > 0) && (alex[0] == 'y'))
  {
    content = "{\"ERROR\":\"ALEXA option invalid in MQTT mode\"}";
    statusCode = 300;
  }
  else
#endif
  {
    broker.toCharArray(mqtt_server, sizeof(mqtt_server));
    port.toCharArray(mqtt_port, sizeof(mqtt_port));
    user.toCharArray(mqtt_user, sizeof(mqtt_user));
    pswd.toCharArray(mqtt_password, sizeof(mqtt_password));

    if (dom.length() == 0) dom = "d";
    domoticMode = dom.charAt(0);
    if (log.length() == 0) log = "n";
    mqtt_log = log.charAt(0);
    if (pers.length() == 0) pers = "n";
    if (pers.charAt(0) == 'y')
      mqtt_persistence = 1;
    else
      mqtt_persistence = 0;

    unsigned long last = millis();
    char uBuf[16];
    WriteEEP(broker, E_MQTT_BROKER);
    WriteEEP(port, E_MQTT_PORT);
    WriteEEP(dom[0], E_DOMOTICMODE);
    WriteEEP(user, E_MQTT_USER);
    WriteEEP(pswd, E_MQTT_PSWD);
    WriteEEP(log[0], E_MQTT_LOG);
    WriteEEP(pers[0], E_MQTT_PERSISTENCE);
    WriteEEP(alex[0], E_ALEXA);
    sprintf(uBuf, "in %d mS", (millis() - last)); 

    content = "{\"Success\":\"saved to eeprom ";
    content += uBuf; 
#ifdef COMMIT_RARE
    content += " - NOT COMMITTED ";
#else
    EEPROM.commit();
#endif
    content += " ... please use .../reset?device=esp    to boot into new mqtt broker\"}";
    statusCode = 200;
  }

  server.send(statusCode, "application/json", content);
}
// =============================================================================================
void handleMqttDevices()  // inizio processo di censimento automatico dei devices scs
{
  String request = server.arg("request");

  uartSemaphor = 1;
  if (firstTime == 0) setFirst();  // DEVONO essere attivi @MX  e @l

  if (request == "clear")
  {
    content = "{\"status\":\"OK - tab cleared...\"}";
    statusCode = 200;
    for (int i = 0; i < 168; ++i)
    {
      EEPROM.write(i + E_MQTT_TABDEVICES, 0);
      EEPROM.write((int) i * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE, 0);
    }
    EEPROM.commit();
    requestBuffer[requestLen++] = '§';
    requestBuffer[requestLen++] = 'U';
    requestBuffer[requestLen++] = '9';
  }
  else

    if (request == "prepare")
    {
      content = "{\"status\":\"OK - proceed\" ";
      if (mqttopen == 3)
        content += ", \"mqtt\":\"ready\"}";
      else
        content += ", \"mqtt\":\"CLOSED\"}";

      statusCode = 200;
      setFirst();  // DEVONO essere attivi @MX  e @l
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = DEVICEREQUEST;
      requestBuffer[requestLen++] = 0xFF;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'U';
      requestBuffer[requestLen++] = '1';
    }


    else if (request == "start")
    {
      content = "{\"status\":\"OK - started\" ";
      if (mqttopen == 3)
        content += ", \"mqtt\":\"ready\"}";
      else
        content += ", \"mqtt\":\"CLOSED\"}";
      statusCode = 200;

      devIx = 1;
      devCtr = 0;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'U';
      requestBuffer[requestLen++] = '2'; // fine censimento
      immediateSend();
      delay(400);       // wait 400mS due to pic eeprom write

      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = DEVICEREQUEST;
#ifdef SCS
      requestBuffer[requestLen++] = devIx;
#endif
#ifdef KNX
      requestBuffer[requestLen++] = 0;  // parte da zero per leggere sector/line
#endif
    }


  /*
     else
     if (request == "term")
     {
       content = "{\"status\":\"OK - terminated...\"}";
       statusCode = 200;
       devIx = 0;
       EEPROM.commit();
       requestBuffer[requestLen++] = '§';
       requestBuffer[requestLen++] = 'U';
       requestBuffer[requestLen++] = '3';
       immediateSend();
       delay(200); // wait 100mS due to eeprom write
     }
  */


    else if (request == "stop")
    {
      content = "{\"status\":\"OK - stopped...\"}";
      statusCode = 200;
      devIx = 0;
      EEPROM.commit();
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'U';
      requestBuffer[requestLen++] = '3';
      immediateSend();
      delay(200); // wait 100mS due to eeprom write
    }
    else if (request == "resend")
    {
      if (mqttopen == 3)
      {
        content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION;
        content += "<p><ol>";
        statusCode = 200;

        char device;
        char devtype;
        char nomeDevice[6];
        char sectorline = EEPROM.read(E_MQTT_TABDEVICES);

        for (char devx = 1; devx < 168; devx++)
        {
          devtype = EEPROM.read(devx + E_MQTT_TABDEVICES);
          if ((devtype > 0) && (devtype < 32))
          {
#ifdef SCS
            device = MQTTnewdevice(devtype, devx);
            sprintf(nomeDevice, "%02X  ", device);  // to
#endif
#ifdef KNX
            device = MQTTnewdevice(devtype, sectorline, devx);
            sprintf(nomeDevice, "%02X%02X  ", sectorline, device);  // to
#endif
            content += "<li>";
            content += nomeDevice;
            if (devtype == 1)
              content += "switch";
#ifdef SCS
            else if (devtype == 3)
#endif
#ifdef KNX
            else if (devtype == 4)
#endif
              content += "dimmer";
            else if (devtype == 8)
              content += "cover";
            else if (devtype == 9)
              content += "coverpct";

            content += "</li>";
          }
        }
        content += "</ol>";
        content += "</form></html>";
      }
      else
      {
        content = "{\"status\":\"KO - MQTT service not available...\"}";
        statusCode = 400;
      }
    }


    else
    { // query
      if (devIx == 0)
      {
        content = "<!DOCTYPE HTML>\r\n<html>OK - list of discovered devices:";
        content += "<p><ol>";
        statusCode = 200;

        char device;
        char devtype;
        char nomeDevice[6];
//        char sectorline = EEPROM.read(E_MQTT_TABDEVICES);

        for (char devx = 1; devx < 168; devx++)
        {
          devtype = EEPROM.read(devx + E_MQTT_TABDEVICES);
          if ((devtype > 0) && (devtype < 32))
          {
#ifdef SCS
            sprintf(nomeDevice, "%02X ", devx);  // to
#endif
#ifdef KNX
            device = DeviceOfIx(devx, (char *) nomeDevice);
#endif
            content += "<li>";
            content += nomeDevice;
            if (devtype == 1)
              content += "switch";
#ifdef SCS
            else if (devtype == 3)
#endif
#ifdef KNX
            else if (devtype == 4)
#endif
              content += "dimmer";
            else if (devtype == 8)
              content += "cover";
            else if (devtype == 9)
            {
              content += "coverpct";
              requestBuffer[requestLen++] = '§';
              requestBuffer[requestLen++] = 'U';
              requestBuffer[requestLen++] = '6';
              requestBuffer[requestLen++] = device;
              immediateSend();
              char m = immediateReceive('[');
              if (m > 8)
              {
                char hBuffer[32];
                sprintf(hBuffer, " %02X%02X %02X%02X %02X %02X%02X %02X", replyBuffer[1], replyBuffer[2], replyBuffer[3], replyBuffer[4], replyBuffer[5], replyBuffer[6], replyBuffer[7], replyBuffer[8]);
                content += hBuffer;
              }
              else
              {
                //              content += " ?parms? ";
                char hBuffer[24];
                sprintf(hBuffer, " (%d) ", m);
                content += hBuffer;
                char n = 1;
                while (n < m)
                {
                  sprintf(hBuffer, "%02X ", replyBuffer[n]);
                  content += hBuffer;
                  n++;
                }
              }
            }

            content += "  ";
            String edesc = "";
            edesc = ReadStream(&edesc[0], (int)E_ALEXA_DESC_DEVICE + (devx * E_ALEXA_DESC_LEN), E_ALEXA_DESC_LEN, 2); // tipo=0 binary array   1:ascii array   2:ascii string
            content += edesc;
            content += "</li>";
          }
        }
        content += "</ol>";
        content += "</form></html>";
      }
      else
      {
        content = "{\"status\":\"waiting query 0x";
        char hBuffer[4];
        sprintf(hBuffer, "%02X", devIx);
        content += hBuffer;
        content += " \"}";
        statusCode = 200;
      }
    }
  uartSemaphor = 0;

  server.send(statusCode, "text/html", content);
}



// =============================================================================================
void handleDeviceName()  // denominazione devices scoperti - per alexa
{
  // denominazione manuale
  char sectorline = EEPROM.read(E_MQTT_TABDEVICES);
  char device  = 0;
  char devtype = 0;
  char deviceX = 0;
  char nomeDevice[6];
  char hBuffer[12];
  char *ch;
  WORD_VAL maxp;
  
  String AlexaDescr = "";
  String TypeDescr = "";
  String EndMsg = "";

  nomeDevice[0] = 0;
  hBuffer[0] = 0;

  if (firstTime == 0) setFirst();  // DEVONO essere attivi @MX  e @l

  if (server.hasArg("busid"))
  {
    String busid = server.arg("busid");
    if ((busid != "") 
#ifdef SCS
    &&  (busid.length() > 1))
#endif
#ifdef KNX
    &&  (busid.length() > 3))
#endif
    {
      deviceX = ixOfDevice(&busid[0]);
      device = DeviceOfIx(deviceX);
      
      if ((deviceX > 0) && (deviceX < 168))
      {
        if (server.hasArg("devname"))
        {
          String edesc = ReadStream(&edesc[0], (int)E_ALEXA_DESC_DEVICE + (deviceX * E_ALEXA_DESC_LEN), E_ALEXA_DESC_LEN, 2); // tipo=0 binary array   1:ascii array   2:ascii string
          AlexaDescr = server.arg("devname");
          if (AlexaDescr != edesc)
          {
            if (alexaParam == 'y' )
            {
              fauxmo.renameDevice(&edesc[0], &AlexaDescr[0]);
            }
             WriteEEP(AlexaDescr, (int) deviceX * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE, E_ALEXA_DESC_LEN);
#ifdef COMMIT_RARE
            EndMsg = "<li>UPDATED !!!  - NOT COMMITTED</li>";
#else
            EndMsg = "<li>UPDATED !!!</li>";
#endif
          }
        }
        if (server.hasArg("maxpos"))
        {
          String maxpos = server.arg("maxpos");
          maxp.Val = (int)strtoul(&maxpos[0], &ch, 10);
        }
        else
          maxp.Val = 0;
          
        if (server.hasArg("type"))
        {
          String stype = server.arg("type");
          char type = stype[0] - '0';
          devtype = EEPROM.read(deviceX + E_MQTT_TABDEVICES);
          if ((type > 0) && (type < 32) && (type != devtype))
          {
            WriteEEP(type, deviceX + E_MQTT_TABDEVICES);
            devtype = type;
#ifdef COMMIT_RARE
            EndMsg = "<li>UPDATED !!!  - NOT COMMITTED</li>";
#else
            EndMsg = "<li>UPDATED !!!</li>";
#endif
          }
        }

#ifndef COMMIT_RARE
        if (EndMsg != "")
            EEPROM.commit();
#endif
        AlexaDescr = "";
        if (devtype == 9)
        {
          requestBuffer[requestLen++] = '§';
          requestBuffer[requestLen++] = 'U';
          requestBuffer[requestLen++] = '8';
          requestBuffer[requestLen++] = device;     // device id
          requestBuffer[requestLen++] = devtype;    // device type
          requestBuffer[requestLen++] = maxp.byte.HB;    // max position H
          requestBuffer[requestLen++] = maxp.byte.LB;    // max position L
          immediateSend();
          devtype = 0;
          immediateReceive('k');
        } // devtype == 9
      } // ((deviceX > 0) && (devAddress < 168))
    } // (server.hasArg("busid"))
  } // (busid != "")
  
  
  // -------------- end of update -------------------
  
  do
  {
    deviceX++;
    devtype = EEPROM.read(deviceX + E_MQTT_TABDEVICES);
  }   while (((devtype <= 0) || (devtype > 31)) && (deviceX != 168));
  if (deviceX >= 168)  devtype=0;

  if ((devtype > 0) && (devtype < 32))
  {
    device = DeviceOfIx(deviceX, nomeDevice);
    AlexaDescr = ReadStream(&AlexaDescr[0], (int) deviceX * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE, E_ALEXA_DESC_LEN, 2); // tipo=0 binary array   1:ascii array   2:ascii string
    if (devtype == 1)
      TypeDescr = "1 switch";
    else if (devtype == 2)
      TypeDescr = "2 ";
    else if (devtype == 3)
      TypeDescr = "3 dimmer";
    else if (devtype == 4)
      TypeDescr = "4 dimmer";
    else if (devtype == 8)
      TypeDescr = "8 cover";
    else if (devtype == 9)
    {
      TypeDescr = "9 coverpct";
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'U';
      requestBuffer[requestLen++] = '6';
      requestBuffer[requestLen++] = device;
      immediateSend();
      char m = immediateReceive('[');
      //    replyBuffer[1], replyBuffer[2]  -  maxposition H-L
      if (m > 8)
      {
        int c = replyBuffer[2] | replyBuffer[1] << 8;
        sprintf(hBuffer, "%d", c);
      }
      else
        sprintf(hBuffer, "nul");
    }
  }

  content = "<!DOCTYPE HTML>\r\n<html>Discovered device:";
  content += "<p>";
  content += "</p><form method='get' action='devicename'>";
#ifdef SCS
  content += "<label>SCS address: </label><input name='busid' maxlength=2 value='";
#endif
#ifdef KNX
  content += "<label>KNX address: </label><input name='busid' maxlength=4 value='";
#endif
  content += String(nomeDevice);
  content += "' style='width:40px'>\
       <label>  type: </label><input name='type' maxlength=10 value='";
  content += TypeDescr;
  content += "' style='width:80px'> ";

  content += "<label> max position (0,1 sec):</label><input name='maxpos' maxlength=6 value='";
  content += hBuffer;
  content += "' style='width:45px'>";

  content += "<label> name:</label><input name='devname' maxlength=20 value='";
  content += AlexaDescr;
  content += "' style='width:200px'>";

  content += "<label> alexa ref.:</label><input name='alexaind' maxlength=3 value='";

//  content += ...............    da fauxmo.addDevice
  unsigned char idalex = 0;
  char found = 0;
  char sAlex[4];
  while ((idalex <= id_fauxmo) && (found == 0))
  {
    if (alexa_BUS_id[idalex] == device)
    {
      found = 1;
      sprintf(sAlex, "%d", idalex);  // to
    }
    else
      idalex++;
  }
  if (found)
    content += sAlex;
  else
    content += "no";
  
  content += "' style='width:40px'>";

  content += "<input type='submit'>";
  content += EndMsg;

  if ((devtype <= 0) || (devtype > 31))
  {
    content += "<li>END OF discovered device";
#ifdef COMMIT_RARE
    content += " - changes COMMITTED";
#endif
    content += "</li>";
    device = 0;
    deviceX = 0;
    EEPROM.commit();
  }
  content += "</form> </html>";

  server.send(200, "text/html", content);
}


// =============================================================================================
void handleScan()
{
  IPAddress ip = WiFi.softAPIP();
  String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION " at ";
  content += ipStr;
  content += "<p>";
  content += wifiSignals;
  content += "</p><form method='get' action='setting'><label>SSID: </label><input name='ssid' length=32>\
    <label>PSW: </label><input name='pass' length=64><label>IP address:<input name='ip' length=16></label>\
    <label>Gateway IP<input name='rip' length=16></label></label><label>UDP port<input name='uport' length=5></label>\
    <input type='submit'></form>";
  content += "</html>";
  server.send(200, "text/html", content);
  wifiSignals = "";
}
// =============================================================================================
void handleSetting()
{
  String qsid = server.arg("ssid");
  String qpass = server.arg("pass");
  String qip = server.arg("ip");
  String qrip = server.arg("rip");
  String qport = server.arg("uport");

  local_ip.fromString(qip);
  router_ip.fromString(qrip);
  udpLocalPort = qport.toInt();
  if (udpLocalPort == 0)
  {
    udpLocalPort = 52056;
    qport = "52056";
  }

  WriteEEP(qsid, E_SSID);
  WriteEEP(qpass, E_PASSW);
  WriteEEP(qip, E_IPADDR);
  WriteEEP(qrip, E_ROUTIP);
  WriteEEP(qport, E_PORT);
  
  EEPROM.commit();

  content = "{\"Success\":\"saved to eeprom... reset to boot into new wifi\"}";
  statusCode = 200;

  server.send(statusCode, "application/json", content);
}
// =============================================================================================
void handleRoot()
{
  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION " at ";
  content += WiFi.localIP().toString();
  content += "</html>";
  server.send(200, "text/html", content);
}
// =============================================================================================
void handleClear()
{
  content = "<!DOCTYPE HTML>\r\n<html>";
  content += "<p>Clearing the EEPROM - reboot.</p></html>";
  server.send(200, "text/html", content);
  for (int i = 0; i < 1024; ++i) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  WiFi.disconnect();
}

// =============================================================================================
void handleCallback()
{
  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION "<p>";
  content += "</p><form method='get' action='backsetting'><label>Callback request: </label><input name='callback' length=128 maxlength=128 value='";
  content += httpCallback;
  content += "' style='width:800px'> <input type='submit'></form>";
  content += "</html>";
  server.send(200, "text/html", content);
}
// =============================================================================================
void handleBackSetting()
{
  String httpback = server.arg("callback");
  httpback.toCharArray(httpCallback, sizeof(httpCallback));

  WriteEEP(httpCallback, E_CALLBACK);
//  WriteStream(httpCallback, E_CALLBACK, 100);
  EEPROM.commit();

  content = "{\"Status\":\"OK\"}";
  statusCode = 200;

  server.send(statusCode, "application/json", content);
}
// =============================================================================================
void WriteLog(const char *msgLog)
{
  char Log[250];
  snprintf_P(Log, sizeof(Log), "%lu - %s", millis(), msgLog);
  client.publish(_MODO "LOG", Log, 0);
}
void WriteLog(String msgLog)
{
  char Log[250];
  snprintf_P(Log, sizeof(Log), "%lu - %s", millis(), msgLog.c_str());
  client.publish(_MODO "LOG", Log, 0);
}
// =============================================================================================
void WriteError(const char *msgLog)
{
  char Log[250];
  snprintf_P(Log, sizeof(Log), "%lu - %s", millis(), msgLog);
  client.publish(_MODO "ERROR", Log, 0);
}
void WriteError(String msgLog)
{
  char Log[250];
  snprintf_P(Log, sizeof(Log), "%lu - %s", millis(), msgLog.c_str());
  client.publish(_MODO "ERROR", Log, 0);
}
// =============================================================================================
char reconnect()
{
  {
    char rc;
    if ((mqtt_user[0] != 0x00) && (mqtt_password[0] != 0x00))
    {
      rc = client.connect("ESP" _modo "gate" _FW_VERSION, mqtt_user, mqtt_password);
    }
    else
    {
      rc = client.connect("ESP" _modo "gate" _FW_VERSION);
    }

    if (rc)
      return 2;
    else
    {
      client.disconnect();
      return 1;
    }
  }
}

// ===============================================================================================
// MQTT callback function  - comando proveniente da desktop o da pulsante fisico
// ===============================================================================================
void MqttCallback(char* topic, byte* payload, unsigned int length)
// =============================================================================================================
// ================================ messaggio MQTT in arrivo (comando web) =====================================
// =============================================================================================================
{
  int i = 0;
  char dev[5];
  char *ch;
  unsigned char devtype = 0;
  char packetBuffer[255];

#ifdef SCS
  unsigned char device = 0xFF;
#endif
#ifdef KNX
  word device = 0xFF;
#endif

  unsigned char reply = 0;
  unsigned char command = 0xFF;
  String rtopic;




  // create character buffer with ending null terminator (string)
  for (i = 0; i < length; i++) {
    packetBuffer[i] = payload[i];
  }
  packetBuffer[i] = '\0';

  String payloads = String(packetBuffer);
  String topicString = String(topic);

  if (firstTime == 0) setFirst();

  // --------------------------------------- SWITCHES -------------------------------------------
  if (topicString.substring(0, sizeof(SWITCH_SET) - 1) == SWITCH_SET)
  {
    devtype = 1; // switch
    rtopic = SWITCH_STATE;
    reply = 1;
    dev[0] = *(topic + sizeof(SWITCH_SET) - 1);
    dev[1] = *(topic + sizeof(SWITCH_SET));
    
#ifdef SCS
    dev[2] = 0;
    device = (char)strtoul(dev, &ch, 16);
    if (payloads.substring(0, 2) == "ON")
      command = 0x00;
    else if (payloads.substring(0, 3) == "OFF")
      command = 0x01;
#endif
#ifdef KNX
    dev[2] = *(topic + sizeof(SWITCH_SET) + 1);
    dev[3] = *(topic + sizeof(SWITCH_SET) + 2);
    dev[4] = 0;
    device = (word)strtoul(dev, &ch, 16);
    if (payloads.substring(0, 2) == "ON")
      command = 0x81;
    else if (payloads.substring(0, 3) == "OFF")
      command = 0x80;
#endif
  }
  // ----------------------------------------------------------------------------------------------
  else



#ifdef SCS //  per ora dimmer solo SCS
    // --------------------------------------- LIGHTS DIMM ------------------------------------------
    if (topicString.substring(0, sizeof(BRIGHT_SET) - 1) == BRIGHT_SET)
    {
#ifdef DEBUG
      rtopic = BRIGHT_STATE;
      Serial.println("dimmer");
#endif
      dev[0] = *(topic + sizeof(BRIGHT_SET) - 1);
      dev[1] = *(topic + sizeof(BRIGHT_SET));
      dev[2] = 0;
      device = (char)strtoul(dev, &ch, 16);
      devtype = 3; // light
      if (payloads.substring(0, 2) == "ON")  // trasformare da % a 1D-9D <--------------------------------------------------------------
        command = 0x00;  // on/up
      else if (payloads.substring(0, 3) == "OFF")
        command = 0x01; // off/down
      else
      {
        int pct = atoi(packetBuffer);    // percentuale
        // --------------- percentuale da 1 a 255 (home assistant) -----------
        if (domoticMode == 'h')   // h=as homeassistant
        { // percentuale 0-255
          pct *= 100;                  // da 0 a 25500
          pct /= 255;                  // da 0 a 100
        }
        // --------------- percentuale da 1 a 100 (domoticz) -----------------
        // percentuale 0-100
        pct += 5;                    // arrotondamento
        pct /= 10;                   // 0-10
        if (pct > 9) pct = 9;
        if (pct == 0) pct = 1;		    // 1-9
        pct *= 16;                   // hex high nibble
        pct += 0x0D;                 // hex low  nibble
        command = (unsigned char) pct;

#ifdef DEBUG
        Serial.println(" payload " + content);
        //     char hBuffer[4];
        //     sprintf(hBuffer, "%02X", pct);
        // ricalcolare payloads per debug
        char action = pct;
        action >>= 4;
        action *= 10;  // percentuale 10-90
        char actionc[4];
        sprintf(actionc, "%02u", action);
        payloads = String(actionc);
#endif
      }
    }
  // ----------------------------------------------------------------------------------------------
    else
#endif //  per ora dimmer solo SCS





      // --------------------------------------- COVERPCT ------------------------------------------------
      if (topicString.substring(0, sizeof(COVERPCT_SET) - 1) == COVERPCT_SET)
      {
#ifdef DEBUG
        Serial.println("cover%");
#endif
        rtopic = COVERPCT_STATE;
        //    reply = 1;  // TEST
        dev[0] = *(topic + sizeof(COVERPCT_SET) - 1);
        dev[1] = *(topic + sizeof(COVERPCT_SET));
#ifdef SCS
        dev[2] = 0;
        device = (word)strtoul(dev, &ch, 16);
#endif
#ifdef KNX
        dev[2] = *(topic + sizeof(COVERPCT_SET) + 1);
        dev[3] = *(topic + sizeof(COVERPCT_SET) + 2);
        dev[4] = 0;
        device = (word)strtoul(dev, &ch, 16);

#endif

        devtype = 9; // coverpct

        if (payloads.substring(0, 4) == "STOP")
          command = 0;
        else if (payloads.substring(0, 2) == "ON")  // discesa - chiudi
          command = 2;
        else if (payloads.substring(0, 5) == "CLOSE")  // discesa - chiudi
        {
          command = 2;
        }
        else if (payloads.substring(0, 3) == "OFF") // salita - apri
          command = 1;
        else if (payloads.substring(0, 4) == "OPEN") // salita - apri
        {
          command = 1;
        }
        else
        {
          int pct = atoi(packetBuffer);    // percentuale
          command = (unsigned char) pct;
        }
      }
  // ----------------------------------------------------------------------------------------------
      else





        // --------------------------------------- COVER ------------------------------------------------
        if (topicString.substring(0, sizeof(COVER_SET) - 1) == COVER_SET)
        {
#ifdef DEBUG
          Serial.println("cover");
#endif
          rtopic = COVER_STATE;
          reply = 1;
          devtype = 8; // cover
          dev[0] = *(topic + sizeof(COVER_SET) - 1);
          dev[1] = *(topic + sizeof(COVER_SET));

#ifdef SCS
          dev[2] = 0;
          device = (char)strtoul(dev, &ch, 16);
          if (payloads.substring(0, 2) == "ON")   // discesa - chiudi
            command = 0x09;
          else if (payloads.substring(0, 5) == "CLOSE")  // discesa - chiudi
          {
            command = 0x09;
          }
          else if (payloads.substring(0, 3) == "OFF") // salita - apri
            command = 0x08;
          else if (payloads.substring(0, 4) == "OPEN") // salita - apri
          {
            command = 0x08;
          }
          else if (payloads.substring(0, 4) == "STOP")
            command = 0x0A;
#endif
#ifdef KNX
          dev[2] = *(topic + sizeof(COVER_SET) + 1);
          dev[3] = *(topic + sizeof(COVER_SET) + 2);
          dev[4] = 0;
          device = (word)strtoul(dev, &ch, 16);

          if (payloads.substring(0, 4) == "STOP")
          {
            command = 0x80;
          }
          else
          {
            device++;   // indirizzo pari

            if (payloads.substring(0, 2) == "ON")   // discesa - chiudi
              command = 0x81;
            else if (payloads.substring(0, 5) == "CLOSE")  // discesa - chiudi
            {
              command = 0x81;
            }
            else if (payloads.substring(0, 3) == "OFF") // salita - apri
              command = 0x80;
            else if (payloads.substring(0, 4) == "OPEN") // salita - apri
            {
              command = 0x80;
            }
          }
#endif
        }
  // ----------------------------------------------------------------------------------------------
        else if (mqtt_log == 'y')
        {
          char log[250];
          sprintf(log, "UNKNOWN topic pct-len: %d paylen: %d", (sizeof(COVERPCT_SET) - 1), length);
          WriteLog(log);
        }
  // ----------------------------------------------------------------------------------------------


  if (reply == 1)
  {
    if (domoticMode == 'h') // home assistant
    {
      if (payloads.substring(0, 5) == "CLOSE")   // discesa - chiudi
        payloads = "closed";
      else if (payloads.substring(0, 4) == "OPEN")  // salita - apri
        payloads = "open";
    }

    rtopic += dev;
    const char* cContent = payloads.c_str();
    const char* cTopic = rtopic.c_str();
    client.publish(cTopic, cContent, mqtt_persistence);
  }




  // as gate.htm   handleGate -     firsttime ?

  if (command != 0xFF)
  {
    if (uartSemaphor == 1)
    {
      char log[80];
      sprintf(log, "UART buffer collision ERROR !");
      WriteError(log);
    }

    if (devtype == 9)	// <====================COVERPCT=========================
    {
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'u';
#ifdef SCS
      requestBuffer[requestLen++] = device;		// to   device
#endif
#ifdef KNX
      requestBuffer[requestLen++] = lowByte(device); // to   device
#endif
      requestBuffer[requestLen++] = command;    // command (%)
    }
    else
    {
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'y';

#ifdef SCS
      requestBuffer[requestLen++] = device; // to   device
      requestBuffer[requestLen++] = 0x00;   // from device
      requestBuffer[requestLen++] = 0x12;   // type:command
      requestBuffer[requestLen++] = command;// command
#endif

#ifdef KNX
      requestBuffer[requestLen++] = 0x01;             // from device
      char a1 = highByte(device);
      char a2 = lowByte(device);
      requestBuffer[requestLen++] = a1; // to   linesector
      requestBuffer[requestLen++] = a2;  // to   device
      requestBuffer[requestLen++] = command;          // command
#endif
    }
  }
}

// =============================================================================================
void handleReset()
{
  // ?device= <esp>|<pic>|<mqtt>
#ifdef COMMIT_RARE
  EEPROM.commit();
#endif

  String param = server.arg("device");

  if (param == "mqtt")
  {

    if (!client.connected())
      client.disconnect();
    mqttopen = 0;

    mqtt_retry = 0;
    if ((mqtt_server[0] >= '0') && (mqtt_server[0] <= '9'))
    {
      mqttopen = 1;
      client.setServer(mqtt_server, atoi(mqtt_port));    // Configure MQTT connexion
      client.setCallback(MqttCallback);                  // callback function to execute when a MQTT message
      content = "{\"status\":\"OK\"}";
      statusCode = 200;
    }
    else
    {
      content = "{\"status\":\"ERROR - missing broker IPaddress\"}";
      statusCode = 200;
    }
    server.send(statusCode, "text/html", content);
  }
  else if (param == "esp")
  {
    ESP.restart();
  }
  else if (param == "pic")
  {
    uartSemaphor = 1;
    requestBuffer[requestLen++] = '@';
    requestBuffer[requestLen++] = '|';
    requestBuffer[requestLen++] = '|';
    content = "{\"status\":\"OK\"}";
    uartSemaphor = 0;
    statusCode = 200;
    server.send(statusCode, "text/html", content);
  }
  else
  {
    content = "{\"invreq  use device=esp  or  device=pic\"}";
    statusCode = 200;
    server.send(statusCode, "text/html", content);
  }
}
// =============================================================================================



// =============================================================================================
void handleStatus()
{
  char temp[38];
  content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP_" _MODO "GATE " _FW_VERSION;
  content += " at ";
  
  content += WiFi.localIP().toString();
  content += "<p><ol>";

  if (connectionType == 1) // web server AP
  {
    content += "<li>";
    content += "Working as AP ";
    content += "</li>";
  }
  else
  {
    content += "<li>";
    content += "System frequency (Mhz): ";
    unsigned char frq = ESP.getCpuFreqMHz(); // returns the CPU frequency in MHz as an unsigned 8-bit integer
    content += String(frq);
    content += "</li>";

    content += "<li>";
    content += "Wifi connection ssid: ";
    String esid;
    esid = ReadStream(&esid[0], E_SSID, 32, 2);  // tipo=0 binary array   1:ascii array   2:ascii string
    content += esid;
    content += "</li>";

    content += "<li>";
    content += "Router IP: ";
    content += WiFi.gatewayIP().toString();
    content += "</li>";
  }

  content += "<li>";
  if ((udpopen == 1) && (udpLocalPort > 0))
  {
    content += "UDP hearing on port ";
    content += String(udpLocalPort);
    if (udp_remote_ip)
    {
      content += " connected ip: ";
      content += udp_remote_ip.toString();
      content += " port ";
      content += String(udp_remote_port);
    }
    else
      content += " now unconnected";
  }
  else
    content += "UDP is closed ";
  content += "</li>";
  
#ifdef USE_TCPSERVER
    content += "<li>";
    if (tcpopen)
    {
      content += "TCP hearing on port ";
      content += String(TCP_PORT);
      
      if (tcpclient && tcpclient.connected())
      {
          content += " connected ip: ";
          content += tcpclient.remoteIP().toString();
      }
    }
    else
      content += "TCP not opened ";

    content += "</li>";
#endif
    
  if (ArduinoOTAflag > 0)
  {
    content += "<li>OTA update ready</li>";  
  }
  content += "<li>";
  content += "Http callback setup = ";
  if (httpCallback[0] == ':')
    content += "YES";
  else
    content += "NO";
  content += "</li>";

  content += "<li>";
  if (mqttopen == 0)
    content += "MQTT connection is CLOSED ";
  else if (mqttopen == 1)
    content += "MQTT connection is WAITING ";
  else if (mqttopen == 2)
    content += "MQTT connection TRY to CONNECT ";
  else if (mqttopen == 3)
    content += "MQTT connection is OPEN ";

  content += "</li>";

  if (alexaParam  == 'y')
  {
    content += "<li>fauxmo ECHO DOT direct connection</li>";
    sprintf(temp, "<li>  %d devices added</li>", id_fauxmo + 1);
    content += temp;
    sprintf(temp, "<li>  %d devices discovered</li>", fauxmo.discovered());
    content += temp;
  }
  else
  {
    content += "<li>MQTT broker is: ";
    content += mqtt_server;
    content += "   port: ";
    content += mqtt_port;
  }
  content += "</li>";

  if (mqttopen > 0)
  {
    content += "<li>";
    if (domoticMode == 'd')
      content += "mqtt model: DOMOTICZ";
    else if (domoticMode == 'h')
      content += "mqtt model: HOMEASSISTANT";
    else
      content += "mqtt model: GENERIC";
    content += "</li>";
  }
  if ((mqttopen > 0) || (alexaParam == 'y'))
  {
    content += "<li>known devices in eeprom: ";
    char devtype;
    char devNr = 0;
    char nomeDevice[6];
    char device;
    char sectorline = EEPROM.read(E_MQTT_TABDEVICES);
    for (char devx = 1; devx < 168; devx++)
    {
      devtype = EEPROM.read(devx + E_MQTT_TABDEVICES);
      if ((devtype > 0) && (devtype < 32))
      {
        devNr++;
        DeviceOfIx(devx, nomeDevice);
        content += nomeDevice;
        content += " ";
      }
    }
    content += "</li>";
  }

  content += "<li>";
  content += "ESP mode: " _MODO;
  content += "</li>";

  sprintf(temp, "<li>Free heap: %d bytes</li>", ESP.getFreeHeap());
  content += temp;

  content += "<li>";
  content += "PIC fw version: ";
  content += picfwVersion;
  content += "</li>";

  content += "<li>";
  content += "</li>";

  content += "<li>";
  content += "END of report ";
  content += "</li>";

  content += "</ol>";
  content += "</form></html>";
  server.send(200, "text/html", content);
}
// =============================================================================================


// -----------------------------------------------------------------------------------------------------------------------------------------------
void createWebServer(int webtype)
// -----------------------------------------------------------------------------------------------------------------------------------------------
{
  if ( webtype == 1 ) // server web AP
  {
    server.on ( "/", handleScan );                  // elenco reti wifi <- solo in modalita AP
  } else
    // --------------------------------------------------------------------------------------
    if (webtype == 0)		// server web connesso a gateway
    {
      server.on ( "/", handleRoot );
      server.on ( "/scan", handleScan );                  // elenco reti wifi <- solo in modalita AP
    }
  server.on ("/setting", handleSetting);          // setup wifi client
  server.on ("/reset", handleReset);              // reset app
  server.on ("/status", handleStatus);            // status app
  server.on ("/request", handleRequest);          // mappa richiesta comandi scs
  server.on ("/gate.htm", handleGate);            // esecuzione comando scs
  server.on ("/gate", handleGate);                // esecuzione comando scs
  server.on ("/callback", handleCallback);        // richiesta setup callback http
  server.on ("/backsetting", handleBackSetting);  // setup callbackhttp
  server.on ("/mqttconfig", handleMqttConfig);    // richiesta setup mqtt
  server.on ("/mqttcfg", handleMqttCFG);          // setup mqtt
  server.on ("/mqttdevices", handleMqttDevices);  // inizio processo di censimento automatico dei devices scs
  server.on ("/devicename", handleDeviceName);    // processo di rinominazione dei devices per alexa
  server.onNotFound ( handleNotFound );
}
// -----------------------------------------------------------------------------------------------------------------------------------------------

/*
// -----------------------------------------------------------------------------------------------------------------------------------------------
String ReadEEP(int eeaddress) 
{
  unsigned char a = 1;
  unsigned char i;
  String s = "";

  while (a)
  {
    a = EEPROM.read(eeaddress++);
    if (i++ > 128) a = 0;
    if ((a < 0x20) || (a > 0x7F))   a = 0;   // only ASCII
    if (a)  s += char(a);
  }
  return s;
}
// -----------------------------------------------------------------------------------------------------------------------------------------------
char ReadEEP(int eeaddress) 
{
    return = EEPROM.read(eeaddress);
}
*/
// -----------------------------------------------------------------------------------------------------------------------------------------------
void WriteEEP(char stream, int eeaddress)
{
   EEPROM.write(eeaddress, stream);
}

// -----------------------------------------------------------------------------------------------------------------------------------------------
void WriteEEP(char * stream, int eeaddress, int len)
{
  unsigned char eof = 0;
  while ((len) && (eof == 0))
  {
    EEPROM.write(eeaddress, *stream);
    eeaddress++;
    if (*stream == 0)
      eof = 1;
    stream++;
    len--;
  }
}
// -----------------------------------------------------------------------------------------------------------------------------------------------
void WriteEEP(String stream, int eeaddress)
{
  unsigned char eof = 0;
  char xs = 0;
  while (eof == 0)
  {
    EEPROM.write(eeaddress, stream[xs]);
    eeaddress++;
    if (stream[xs] == 0)
      eof = 1;
    xs++;
  }
}
// -----------------------------------------------------------------------------------------------------------------------------------------------
void WriteEEP(String stream, int eeaddress, int maxlen)
{
  unsigned char eof = 0;
  char xs = 0;
  while ((eof == 0) && (xs < maxlen))
  {
    EEPROM.write(eeaddress, stream[xs]);
    eeaddress++;
    if (stream[xs] == 0)
      eof = 1;
    xs++;
  }
}
// -----------------------------------------------------------------------------------------------------------------------------------------------
String ReadStream(char * stream, int eeaddress, int len, unsigned char tipo) // tipo=0 binary array   1:ascii array   2:ascii string
{
  unsigned char a;
  unsigned char i;
  String s = "";

  while (len)
  {
    a = EEPROM.read(eeaddress++);
    if (tipo > 0)
    {
      //          if (a == 0xFF) a = 0;
      if ((a < 0x20) || (a > 0x7F))   a = 0;   // only ASCII
      if (a == 0) len = 1;
    }
    if ((tipo == 1) || (tipo == 0))
    {
      *stream = a;
      stream++;
    }
    if (tipo == 2) 
    {
      if (a == 0) len = 1;
      else   s += char(a);
    }
      
    len--;
    i++;
  }
  if (tipo == 2)
    return s;
}

// -----------------------------------------------------------------------------------------------------------------------------------------------
#ifdef KNX
char  MQTTnewdevice(char devtype, char sectorline, char devname)  // KNX
{
  char edesc[25];
  char addrdevice[5];
  ReadStream(edesc, (int)E_ALEXA_DESC_DEVICE + (devname * E_ALEXA_DESC_LEN), E_ALEXA_DESC_LEN, 1); // tipo=0 binary array   1:ascii array   2:ascii string
  DeviceOfIx(sectorline, devname, addrdevice);  
  if (mqttopen == 3)
  {
    if ((edesc[0] == 0) || (edesc[0] == 0xFF))
      MQTTnewdiscover(devtype, addrdevice, addrdevice);
    else
      MQTTnewdiscover(devtype, addrdevice, edesc);
  }
  return devname;
}
#endif
// -----------------------------------------------------------------------------------------------------------------------------------------------
#ifdef SCS
char  MQTTnewdevice(char devtype, char devname)   // SCS
{
  char edesc[25];
  char addrdevice[3];
  ReadStream(edesc, (int)E_ALEXA_DESC_DEVICE + (devname * E_ALEXA_DESC_LEN), E_ALEXA_DESC_LEN, 1); // tipo=0 binary array   1:ascii array   2:ascii string

  DeviceOfIx(devname, addrdevice);  
  if (mqttopen == 3)
  {
    if ((edesc[0] == 0) || (edesc[0] == 0xFF))
      MQTTnewdiscover(devtype, addrdevice, addrdevice);
    else
      MQTTnewdiscover(devtype, addrdevice, edesc);
  }
  return devname;
}
#endif
// -----------------------------------------------------------------------------------------------------------------------------------------------
char  MQTTnewdiscover(char devtype, char * addrDevice, char * nomeDevice)
{
  char rc = 0;
  String topic;
  String payload;
  if (devtype == 0x01)  // D=define new device SWITCH <<<-----------------------------------------------
  {
    rc = 1;
    topic   = NEW_SWITCH_TOPIC;
    topic  += addrDevice;
    topic  += NEW_CONFIG_TOPIC;

    payload = NEW_DEVICE_NAME;
    payload += nomeDevice;
    payload += NEW_SWITCH_SET;
    payload += addrDevice;
    payload += NEW_SWITCH_STATE;
    payload += addrDevice;
    payload += NEW_DEVICE_END;
  }
  else if ((devtype == 0x03) || (devtype == 0x04)) // D=define new device DIMMER <<<-----------------------------------------------
  {
    rc = 1;
    topic   = NEW_LIGHT_TOPIC;
    topic  += addrDevice;
    topic  += NEW_CONFIG_TOPIC;

    payload = NEW_DEVICE_NAME;
    payload += nomeDevice;
    payload += NEW_LIGHT_SET;
    payload += addrDevice;
    payload += NEW_LIGHT_STATE;
    payload += addrDevice;
    payload += NEW_BRIGHT_SET;
    payload += addrDevice;
    payload += NEW_BRIGHT_STATE;
    payload += addrDevice;
    payload += NEW_DEVICE_END;
  }
  else if (devtype == 0x08) // D=define new device COVER <<<-----------------------------------------------
  {
    rc = 1;
    topic   = NEW_COVER_TOPIC;
    topic  += addrDevice;
    topic  += NEW_CONFIG_TOPIC;

    payload = NEW_DEVICE_NAME;
    payload += nomeDevice;
    payload += NEW_COVER_SET;
    payload += addrDevice;
    payload += NEW_COVER_STATE;
    payload += addrDevice;
    payload += NEW_DEVICE_END;
  }
  else if (devtype == 0x09) // D=define new device COVER PCT<<<-----------------------------------------------
  {
    rc = 1;
    topic   = NEW_COVERPCT_TOPIC;
    topic  += addrDevice;
    topic  += NEW_CONFIG_TOPIC;

    payload  = NEW_DEVICE_NAME;
    payload += nomeDevice;
    payload += NEW_COVER_SET;
    payload += addrDevice;
    payload += NEW_COVERPCT_SET;
    payload += addrDevice;
    ///      payload += NEW_COVER_STATE;  // cover percentuale, meglio non trattare lo stato
    ///      payload += addrDevice;       //                    ma solo la posizione
    payload += NEW_COVERPCT_STATE;
    payload += addrDevice;
    payload += NEW_DEVICE_END;
  }
  if (rc == 1)
  {
    const char* cPayload = payload.c_str();
    const char* cTopic = topic.c_str();
    client.publish(cTopic, cPayload, false);
  }
  return rc;
}

// =====================================================================================================
// ===============================SCRITTURA UART IMMEDIATA FUORI CICLO==================================
void immediateSend(void)
{
  if (requestLen > 0)
  {
    // =========================== send control char and data over serial =============================
    String log = "tx: ";
    char logCh[4];
    uartSemaphor = 1;
    int s = 0;
    while (s < requestLen)
    {
      delayMicroseconds(OUTERWAIT);
#ifndef DEBUG
      while (((USS(0) >> USTXC) & 0xff) > 0)     { // aspetta il buffer sia completamente vuoto
        delay(0);
      }

      delayMicroseconds(OUTERWAIT);
      USF(0) = requestBuffer[s];  // scrittura seriale
      delayMicroseconds(OUTERWAIT);

      while (((USS(0) >> USTXC) & 0xff) > 0)     { // aspetta il buffer sia completamente vuoto
        delay(0);
      }
#endif
#ifdef DEBUG
      sprintf(logCh, "%02X ", requestBuffer[s]);
      log += logCh;
#else
      if (mqtt_log == 'y')
      {
        sprintf(logCh, "%02X ", requestBuffer[s]);
        log += logCh;
      }
#endif
      delayMicroseconds(OUTERWAIT);
      s++;
    }
#ifdef DEBUG
    Serial.println("\r\n" + log);
#else
    if (mqtt_log == 'y') WriteLog(log);
#endif
    requestLen = 0;
    uartSemaphor = 0;
  }
}








// =====================================================================================================
// ================================LETTURA UART IMMEDIATA FUORI CICLO===================================
char immediateReceive(char firstChar)
{
  replyLen = 0;
  int wait = 0;
  
  while ((!Serial.available() ) && (wait < OUTERWAIT * 100))
  {
    delayMicroseconds(10); // 120000 uS (120mS) timeout
    wait++;
  }

  if (Serial.available() )
  {
    String log = "rx: ";
    char logCh[4];
    while (Serial.available() && (replyLen < 255))
    {
      while (Serial.available() && (replyLen < 255))
      {
        replyBuffer[replyLen] = Serial.read();        // receive from serial USB
        if (mqtt_log == 'y')
        {
          sprintf(logCh, "%02X ", replyBuffer[replyLen]);
          log += logCh;
        }
        
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
        if (tcpuart == 2)  
        {
          sprintf(logCh, "%02X ", replyBuffer[replyLen]);
          log += logCh;
        }
  #endif
#endif
        
        if ((replyLen != 0) || (firstChar == 0) || (replyBuffer[0] == firstChar))
          replyLen++;
        delayMicroseconds(INNERWAIT);
      }
//      delayMicroseconds(OUTERWAIT);
      
      wait = 0;
      while ((!Serial.available() ) && (wait < OUTERWAIT / 5))
      {
        delayMicroseconds(5);
        wait++;
      }
    }
    if ((mqtt_log == 'y') && (replyLen > 0)) WriteLog(log);
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
    if ((tcpuart == 2) && (tcpclient) && (tcpclient.connected())) 
    {
      tcpclient.write((char*)&log[0], log.length());
      tcpclient.flush(); 
    }
  #endif
#endif
  }
  replyBuffer[replyLen] = 0;
  return replyLen;
}
// =====================================================================================================
// =====================================================================================================
#ifdef DEBUG
void manualInput(char prefix)
{
  switch (prefix)
  {
    case 'O':
      fauxmo.setState(1, 1, 128);
      Serial.println("device 1: ON, 128");
      break;
    case 'o':
      fauxmo.setState(1, 0, 100);
      Serial.println("device 1: OFF, 100");
      break;
    case '1':  // switch
    case '3': // dimmer
    case '8': // cover
    case '9': // coverpct
      EEPROM.write(0x11 + E_MQTT_TABDEVICES, prefix - '0'); // cambia devicetype di scs 0x11
      EEPROM.commit();
      break;
  }
}
#endif
// =====================================================================================================
// =====================================================================================================
// -----------------------------------------------------------------------------------------------------------------------------------------------
void setup() {

  //  system_update_cpu_freq(160);
  //  unsigned char frq = ESP.getCpuFreqMHz(); // returns the CPU frequency in MHz as an unsigned 8-bit integer

  wifi_station_set_hostname( _modo "gate" );

//  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(2, INPUT);
  pinMode(0, OUTPUT);
  // aspetta 15 secondi perche' con l'assorbimento iniziale di corrente esp8266 fa disconnettere l'adattatore seriale
  int pauses = 0;
  {
    while (pauses < 150) // 15 secondi wait
    {
      pauses++;
      delay(100);            // wait 100ms
    }
  }

  Serial.begin(115200, SERIAL_8N2);
  while (!Serial) {
    ;
  }
  Serial.flush();

  EEPROM.begin(MAXEEPROM);

  digitalWrite(0, LOW); // A0 OUTPUT BASSO
  delay(10);

#ifdef DEBUG
  Serial.println();
  Serial.println();
  Serial.println("Startup");

  Serial.println("Press  A for start as AP,  E for eeprom init,  W for eeprom test,  S for eeprom speed");
  Serial.setTimeout(5000);
  char serin[4];
  Serial.readBytes(serin, 1);
  Serial.setTimeout(1000);

  if (serin[0] == 'E')
  {
    int we = 0;
    while (we < 1024)
    {
      EEPROM.write(we, 0);
      we++;
    }
    EEPROM.commit();
    Serial.println("eeprom init OK");
    forceAP = 1;
    Serial.println("Forced AP mode");
  }
  if (serin[0] == 'W')
  {
    String stst = "Test di scrittura EEPROM - address = 3930";
    Serial.println(stst);
    char rtst[50];
    WriteEEP(stst, 3930); 
    EEPROM.commit();
    ReadStream(rtst, 3930, 50, 1);  // tipo=0 binary   1:ascii    2:string
    if (rtst[0] == stst[0])
      Serial.println(rtst);
    else
    {
      Serial.println("FAILED ");
      Serial.printf(" %02X %02X %02X %02X ...", rtst[0], rtst[1], rtst[2], rtst[3]);
    }
  }
  if ((serin[0] == 'A') || (serin[0] == 'a'))
  {
    forceAP = 1;
    Serial.println("Forced AP mode");
  }

  if (serin[0] == 'S')
  {
    // test eeprom speed with readstream (EEPROM.read loop)
    unsigned long last = millis();
    int nt = 0;
    char tst;
    while (nt < 1024)
    {
      ReadStream((char *) &tst, nt, 1, 1);  // tipo=0 binary   1:ascii
      nt++;
    }
    Serial.printf("\r\n- eeprom read 1k: %d mS", (millis() - last)); // 48mS


    // test eeprom read-write with readstream-writestream
    last = millis();
    nt = 0;
    while (nt < 1024)
    {
      ReadStream((char *) &tst, nt, 1, 1);  // tipo=0 binary   1:ascii
      WriteEEP(tst, nt);
//    EEPROM.write(nt, tst);
      nt++;
    }
    Serial.printf("\r\n- eeprom write 1k: %d mS", (millis() - last));

    last = millis();
    EEPROM.commit();
    Serial.printf("\r\n- eeprom commit 1k: %d mS\r\n", (millis() - last));
  }


  // read eeprom for ssid and pass
  Serial.print("Read EEPROM ssid: ");
#endif



  String esid;
  esid = ReadStream(&esid[0], E_SSID, 32, 2);  // tipo=0 binary array   1:ascii array   2:ascii string
  if ((esid[0] == 0) || (esid[0] == 0xFF))
  {
    forceAP = 1;
#ifdef DEBUG
    Serial.println("- AP mode");
#endif
  }
#ifdef DEBUG
  Serial.println(esid);
  Serial.print("Read EEPROM pass: ");
#endif

  String epass = "";
  epass = ReadStream(&epass[0], E_PASSW, 32, 2);  // tipo=0 binary array   1:ascii array   2:ascii string
#ifdef DEBUG
  Serial.println(epass);
#endif

  ReadStream(httpCallback, E_CALLBACK, sizeof(httpCallback), 1);  // tipo=0 binary array   1:ascii array   2:ascii string
#ifdef DEBUG
  Serial.print("callback=");
  Serial.println(httpCallback);
#endif

  ReadStream(mqtt_server, E_MQTT_BROKER, sizeof(mqtt_server), 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("broker=");
  Serial.println(mqtt_server);
#endif

  ReadStream(mqtt_port, E_MQTT_PORT, sizeof(mqtt_port), 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("port=");
  Serial.println(mqtt_port);
#endif

  ReadStream((char *) &domoticMode, E_DOMOTICMODE, 1, 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("domoticMode=");
  Serial.println(domoticMode);
#endif

  ReadStream(mqtt_user, E_MQTT_USER, sizeof(mqtt_user), 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("user=");
  Serial.println(mqtt_user);
#endif

  ReadStream(mqtt_password, E_MQTT_PSWD, sizeof(mqtt_password), 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("password=");
  Serial.println(mqtt_password);
#endif

  ReadStream((char *) &mqtt_log, E_MQTT_LOG, 1, 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("log=");
  Serial.println(mqtt_log);
#endif

  ReadStream((char *) &alexaParam, E_ALEXA, 1, 1);  // tipo=0 binary   1:ascii
#ifdef DEBUG
  Serial.print("alexa=");
  Serial.println(alexaParam);
  if (alexaParam == 'y')
    Serial.println("HTTP_PORT = 8080");
#endif
  if (alexaParam == 'y')
  {
    http_port = 8080;  // 8080
  }

  ReadStream((char *) &mqtt_persistence, E_MQTT_PERSISTENCE, 1, 1);  // tipo=0 binary   1:ascii
  if (mqtt_persistence == 'y')
    mqtt_persistence = 1;
  else
    mqtt_persistence = 0;

#ifdef DEBUG
  Serial.print("pers=");
  Serial.println(mqtt_persistence);
#endif


  Serial.write('@');    // set led lamps
  Serial.write(0xF0);   // set led lamps low-freq (client mode)
  delay(10);            // wait 10ms
  Serial.flush();
  Serial.write('@');    // set led lamps
  Serial.write('q');    // query PIC fw version
  delay(50);            // wait 50ms
  char sl = 0;
  if (Serial.available() )
  {
    while (Serial.available() && (sl < 15))
    {
      picfwVersion[sl++] = Serial.read();        // receive from serial USB
      //    delayMicroseconds(INNERWAIT);
    }
    picfwVersion[0] = '>';
    picfwVersion[sl] = 0;
  }









#ifdef DEBUG
  Serial.write("@MA");  // set SCSgate/KNXgate ascii mode
#else
  Serial.write("@MX");  // set SCSgate/KNXgate hex mode
#endif
  delay(50);            // wait 50ms

  if (Serial.find("k") == 0)
  {
#ifdef DEBUG
    //  Serial.print("Gate error ");
#endif
    //  ledError(1);
  }

  Serial.write("@b");   // clear SCSgate/KNXgate buffer

  Serial.setTimeout(10); // timeout is 10mS
  delay(50);           // wait 50ms
  Serial.flush();


  String qip = ReadStream(&qip[0], E_IPADDR, 16, 2);  // tipo=0 binary   1:ascii   2=string
  local_ip.fromString(qip);

#ifdef DEBUG
  Serial.println("");
  Serial.print("local ip=");
  Serial.println(local_ip);
#endif

  String qrip = ReadStream(&qrip[0], E_ROUTIP, 16, 2);  // tipo=0 binary   1:ascii  2=string
  router_ip.fromString(qrip);

#ifdef DEBUG
  Serial.print("router ip=");
  Serial.println(router_ip);
#endif

  String qport = ReadStream(&qport[0], E_PORT, 6, 2);  // tipo=0 binary   1:ascii   2=string
  udpLocalPort = qport.toInt();
  if ((udpLocalPort == 0) || (udpLocalPort == 0xFFFF))
  {
    udpLocalPort = 52056;
  }
#ifdef DEBUG
  Serial.print("local port=");
  Serial.println(udpLocalPort);
#endif



#ifdef DEBUG
  if (digitalRead(2) == 0)
    Serial.println("Pin 2 force AP mode - ignored...");
#endif

#ifdef DEBUG
  if (( esid.length() > 1 ) && (forceAP == 0)) // dati in eeprom, jumper ignorato, for
#else
  if (( esid.length() > 1 ) && (digitalRead(2) == 1) && (forceAP == 0)) // dati in eeprom, jumper assente, forzatura non digitata
#endif
  {                                                         // connessione al router
    WiFi.begin(esid.c_str(), epass.c_str());

    if ((local_ip[0] > 0) && (local_ip[0] < 255))
    {
#ifdef DEBUG
      Serial.println(" ");
      Serial.println("static IP");
#endif
      WiFi.config(local_ip, router_ip, IPAddress(255, 255, 255, 0));
    }
#ifdef DEBUG
    else
    {
      Serial.println(" ");
      Serial.println("dynamic IP");
    }
#endif

    if (testWifi())   // wifi connesso
    {
      WiFi.mode(WIFI_STA);
      //      WiFi.disconnect();
      launchWeb(0);

#ifdef DEBUG
      Serial.println("Ready");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
#endif

      // ==================================== TCP server ====================================================
#ifdef USE_TCPSERVER
      tcpserver.begin();
      tcpopen = 1;
#endif 

      // ==================================== alexa - fauxmo ================================================

      if (alexaParam == 'y' )
      {

        // By default, fauxmoESP creates it's own webserver on the defined port
        // The TCP port must be 80 for gen3 devices (default is 1901)
        // This has to be done before the call to enable()

        fauxmo.createServer(true); // not needed, this is the default value
        fauxmo.setPort(80); // This is required for gen3 devices

        // You have to call enable(true) once you have a WiFi connection
        // You can enable or disable the library at any moment
        // Disabling it will prevent the devices from being discovered and switched

        fauxmo.enable(true);
#ifdef DEBUG
        Serial.println("- fauxmo enabled");
        Serial.printf("[START] Free heap: %d bytes\r\n", ESP.getFreeHeap());
#endif
        // fauxmo ALEXA add devices <=============================================================================================

        id_interfaccia_scs_knx = fauxmo.addDevice(ID_MY, 0);
        
        char devtype;
        char devx;
        for (devx = 1; devx < 168; devx++)
        {
          devtype = EEPROM.read(devx + E_MQTT_TABDEVICES);
          if ((devtype > 0) && (devtype < 32))
          {
            String edesc = "";
            edesc = ReadStream(&edesc[0], (int)E_ALEXA_DESC_DEVICE + (devx * E_ALEXA_DESC_LEN), E_ALEXA_DESC_LEN, 2); // tipo=0 binary array   1:ascii array   2:ascii string
            
            if (edesc != "")
            {
              if (devtype == 9)
                id_fauxmo = fauxmo.addDevice(&edesc[0], 1);
              else
                id_fauxmo = fauxmo.addDevice(&edesc[0], 128);

//            alexa_BUS_id[id_fauxmo] = devx;  // index: device id alexa max 168 devices    data: ID bus pointer
              alexa_BUS_id[id_fauxmo] = DeviceOfIx(devx);  // index: device id alexa max 168 devices    data: ID bus reale
            }
          }
        }
        // ----------------------------------------------------------------------------------------------------
        // ----------------------------------------------------------------------------------------------------
        // ----------------------------------------------------------------------------------------------------


#ifdef DEBUG
        Serial.printf("[START] %d fauxmo devices added\r\n", id_fauxmo + 1);
        Serial.printf("[START] Free heap: %d bytes\r\n", ESP.getFreeHeap());
#endif




        // fauxmo ALEXA callback <=============================================================================================
#ifdef DEBUG_FAUXMO_TCP         
        fauxmo.onSetState([](unsigned char alexa_id, const char * device_name, char alexacommand, unsigned char value, const char * body, const char * response)
#else
        fauxmo.onSetState([](unsigned char alexa_id, const char * device_name, char alexacommand, unsigned char value)
#endif
        { // callback start

          // Callback when a command from Alexa is received.
          // You can use alexa_id or device_name to choose the element to perform an action onto (relay, LED,...)
          //// old // State is a bool (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
          // command is a char (1=ON 2=OFF 3=bright equal  4=bright+   5=bright- )
          // value is a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
          // Just remember not to delay too much here, this is a callback, exit as soon as possible.
          // If you have to do something more involved here set a flag and process it in your main loop.

#ifdef DEBUG
          Serial.printf("\r\n- Device #%d (%s) command: %d value: %d ", alexa_id, device_name, alexacommand, value);
#endif

#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
          if ((tcpuart == 2) && (tcpclient) && (tcpclient.connected())) 
          {
            char tcpbuffer[55];
            int  buflen;
            sprintf(tcpbuffer,"\r\n- Device #%d (%s) command: %d value: %d       ", alexa_id, device_name, alexacommand, value);
            tcpclient.write(tcpbuffer, 47);
            tcpclient.write("\r\n  req: ", 9);
            tcpclient.write(body, strlen(body));
            tcpclient.write("\r\n  res: ", 9);
            tcpclient.write(response, strlen(response));
            if (uartSemaphor == 1)
              tcpclient.write("\r\nUART in use... ", 16);
            tcpclient.flush(); 
          }
  #endif
#endif

          // Checking for alexa_id is simpler if you are certain about the order they are loaded and it does not change.
          // Otherwise comparing the device_name is safer.

          //      if (strcmp(device_name, ID_MY)==0)
          
          if (alexa_id == id_interfaccia_scs_knx)
          {
#ifdef DEBUG
            Serial.println(ID_MY);
#endif
            if (alexacommand == 1) // accendi <----------------
              system_update_cpu_freq(160);
            else if (alexacommand == 2) // spegni <----------------
              system_update_cpu_freq(80);
          }
          else // an scs/knx device
          {
            char device = alexa_BUS_id[alexa_id];
            char devix = ixOfDevice(device);
            char devtype = EEPROM.read(devix + E_MQTT_TABDEVICES);
            
#ifdef DEBUG
            Serial.printf("\r\n-       device " _MODO " %02X - type: %02X ", device, devtype);
#endif

            // If your device state is changed by any other means (MQTT, physical button,...)
            // you can instruct the library to report the new state to Alexa on next request:
            // fauxmo.setState(ID_YELLOW, true, 255);

///         if (uartSemaphor == 0)
            {
              if (firstTime == 0) setFirst();  // DEVONO essere attivi @MX  e @l
              unsigned char command;
              char linesector;
              int pct;
              char stato = fauxmo.getState(alexa_id);
              switch (devtype)
              {
                // --------------------------------------- SWITCHES -------------------------------------------
                case 1:
                  if (alexacommand == 1) // accendi <----------------
                  {
#ifdef SCS
                    command = 0;
#endif
#ifdef KNX
                    command = 0x81;
#endif
                    fauxmo.setState(alexa_id, stato | 1, 128);
                  }
                  else if (alexacommand == 2) // spegni  <----------------
                  {
#ifdef SCS
                    command = 1;
#endif
#ifdef KNX
                    command = 0x80;
#endif
                    fauxmo.setState(alexa_id, stato & 0xFE, 128);
                  }
                  else
                    break;

                  asynchBuffer[asynchLen++] = '§';
                  asynchBuffer[asynchLen++] = 'y';

#ifdef KNX
// comando §y<source><linesector><destaddress><command>
                  linesector = EEPROM.read(E_MQTT_TABDEVICES);
                  asynchBuffer[asynchLen++] = 0x01;   // from device
                  asynchBuffer[asynchLen++] = linesector; // to   device line-sector
                  asynchBuffer[asynchLen++] = device; // to   device address
#endif

#ifdef SCS
// comando §y<destaddress><source><type><command>
                  asynchBuffer[asynchLen++] = device; // to   device
                  asynchBuffer[asynchLen++] = 0x00;   // from device
                  asynchBuffer[asynchLen++] = 0x12;   // type:command
#endif

                  asynchBuffer[asynchLen++] = command;// command
                  break;


                case 3:
#ifdef SCS //  per ora dimmer solo SCS
                  // --------------------------------------- LIGHTS DIMM ------------------------------------------
                  if (alexacommand == 1) // accendi  <----------------
                  {
                    command = 0;
                    fauxmo.setState(alexa_id, stato | 1, 0);
                  }
                  else if (alexacommand == 2) // spegni <----------------
                  {
                    command = 1;
                    fauxmo.setState(alexa_id, stato & 0xFE, 0);
                  }
                  else if ((alexacommand == 3) || (alexacommand == 4) || (alexacommand == 5)) // cambia il valore  <----------------
                  {
                    fauxmo.setState(alexa_id, 1, value);

                    // trasformare da % a 1D-9D <--------------------------------------------------------------
                    pct = value;    // percentuale da 0 a 255
                    pct *= 100;                  // da 0 a 25500
                    pct /= 255;                  // da 0 a 100
                    pct += 5;                    // arrotondamento
                    pct /= 10;                   // 0-10
                    if (pct > 9) pct = 9;
                    if (pct == 0) pct = 1;	   // 1-9
                    pct *= 16;                   // hex high nibble
                    pct += 0x0D;                 // hex low  nibble
                    command = (unsigned char) pct;
                  }
                  else
                    break;

                  asynchBuffer[asynchLen++] = '§';
                  asynchBuffer[asynchLen++] = 'y';
                  asynchBuffer[asynchLen++] = device; // to   device
                  asynchBuffer[asynchLen++] = 0x00;   // from device
                  asynchBuffer[asynchLen++] = 0x12;   // type:command
                  asynchBuffer[asynchLen++] = command;// command

#endif
                  break;



                case 4:
#ifdef KNX //  dimmer  KNX
                  // --------------------------------------- LIGHTS DIMM ------------------------------------------
                  fauxmo.setState(alexa_id, stato, value);  // coverpct - lo stato deve sempre essere ON
                  pct = value;
                  pct *= 100;
                  pct /= 255;

                  if ((alexacommand == 2)  // spegni  <----------
                  ||  (alexacommand == 1)) // accendi <----------
                  {
                    command = (alexacommand & 1) | 0x80;
                    asynchBuffer[asynchLen++] = '§';
                    asynchBuffer[asynchLen++] = 'y';
                  // comando §y<source><linesector><destaddress><command>
                    linesector = EEPROM.read(E_MQTT_TABDEVICES);
                    asynchBuffer[asynchLen++] = 0x01;   // from device
                    asynchBuffer[asynchLen++] = linesector; // to   device line-sector
                    asynchBuffer[asynchLen++] = device; // to   device address                    
                    asynchBuffer[asynchLen++] = command;// command
                  }
                  else if ((alexacommand == 4)  // alza  <----------------
                       ||  (alexacommand == 5)) // abbassa  <----------------
                  {
                    asynchBuffer[asynchLen++] = '§';
                    asynchBuffer[asynchLen++] = 'm';
                    asynchBuffer[asynchLen++] = device;		// to   device
                    asynchBuffer[asynchLen++] = pct;        // command (%)
                  }
#endif
                  break;

                case 8:
                  // --------------------------------------- COVER ---------------------------------------------------
                  if ((alexacommand == 2) || (alexacommand == 1)) // spegni / ferma  <--oppure accendi------
                  {
#ifdef SCS
                    command = 0x0A;
#endif
#ifdef KNX
                    command = 0x80;
#endif
                    fauxmo.setState(alexa_id, stato | 0xC1, value); // 0xc1: dara' errore ma almeno evita il blocco
                  }
                  else if (alexacommand == 4) // alza  <----------------
                  {
#ifdef SCS
                    command = 0x08;
#endif
#ifdef KNX
                    command = 0x80;
                    device++;
#endif
                    fauxmo.setState(alexa_id, stato | 0xC0, value); // 0xc0: dopo aver inviato lo stato setta value a 128
                  }
                  else if (alexacommand == 5) // abbassa  <----------------
                  {
#ifdef SCS
                    command = 0x09;
#endif
#ifdef KNX
                    command = 0x81;
                    device++;
#endif
                    fauxmo.setState(alexa_id, stato | 0xC0, value); // 0xc0: dopo aver inviato lo stato setta value a 128
                  }
                  else
                    break;

                  asynchBuffer[asynchLen++] = '§';
                  asynchBuffer[asynchLen++] = 'y';

#ifdef KNX
// comando §y<source><linesector><destaddress><command>
                  linesector = EEPROM.read(E_MQTT_TABDEVICES);
                  asynchBuffer[asynchLen++] = 0x01;   // from device
                  asynchBuffer[asynchLen++] = linesector; // to   device line-sector
                  asynchBuffer[asynchLen++] = device; // to   device address
#endif
#ifdef SCS
// comando §y<destaddress><source><type><command>
                  asynchBuffer[asynchLen++] = device; // to   device
                  asynchBuffer[asynchLen++] = 0x00;   // from device
                  asynchBuffer[asynchLen++] = 0x12;   // type:command
#endif

                  asynchBuffer[asynchLen++] = command;// command
                  break;

                case 9:
                  // --------------------------------------- COVERPCT alexa------------------------------------------------
                  fauxmo.setState(alexa_id, 1, value);  // coverpct - lo stato deve sempre essere ON
                  pct = value;
                  pct *= 100;
                  pct /= 255;

                  if (alexacommand == 2) // spegni / ferma  <----------------
                  {
                    command = 0;
                    if (value < 5)
                      fauxmo.setState(alexa_id, 0, value);
                  }
                  else if (alexacommand == 1) // accendi (SU) <----------
                  {
                    command = 1;
                  }
                  else if (alexacommand == 4) // alza  <----------------
                  {
                    command = 1;
                    command = pct;
                  }
                  else if (alexacommand == 5) // abbassa  <----------------
                  {
                    command = 2;
                    command = pct;
                  }
                  else // (alexacommand == 3) // non cambia  <----------------
                    break;


                  asynchBuffer[asynchLen++] = '§';
                  asynchBuffer[asynchLen++] = 'u';
                  asynchBuffer[asynchLen++] = device;		// to   device
                  asynchBuffer[asynchLen++] = command;    // command (%)
                  break;
              } // end switch

            } // end uartsemaphor
          } // end else   an scs device
        });



      }  // end if alexa
      else
      {
#ifdef DEBUG
        Serial.println("open UDP port");
#endif
        udpopen = udpConnection.begin(udpLocalPort);
#ifdef DEBUG
        if (udpopen == 1)
          Serial.println ( "UDP server started" );
        else
          Serial.println ( "UDP open ERROR" );
#endif
      }
      Serial.write('@');   // set led lamps
      Serial.write(0xF1);  // set led lamps std-freq (client mode)



      // =================================================================================================

      if ((mqtt_server[0] >= '0') && (mqtt_server[0] <= '9'))
      {
#if defined DEBUG
        Serial.print("MQTT set server ");
        Serial.println(mqtt_server);
#endif
        mqttopen = 1;
        client.setServer(mqtt_server, atoi(mqtt_port));    // Configure MQTT connexion
        client.setCallback(MqttCallback);           // callback function to execute when a MQTT message
      }
      // =================================================================================================
      if ((alexaParam == 'y') && (firstTime == 0))
        setFirst();  // DEVONO essere attivi @MX  e @l
    }
    else
    {
      // CONNESSIONE WIFI FALLITA . REBOOT
      ESP.restart();
    }
  } //  (( esid.length() > 1 ) && (digitalRead(2) == 1) && (forceAP == 0)) // connessione al router
  else
  {
    // access point mode
    setupAP();
    Serial.write('@');   // set led lamps
    Serial.write(0xF2);  // set led lamps high-freq (AP mode)
  }



      // ==================================== OTA startup ================================================
#ifdef USE_OTA
      // Port defaults to 8266
      // ArduinoOTA.setPort(8266);

      // Hostname defaults to esp8266-[ChipID]
      // ArduinoOTA.setHostname("myesp8266");

      // No authentication by default
      // ArduinoOTA.setPassword("admin");

      // Password can be set with it's md5 value as well
      // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
      // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

      ArduinoOTA.onStart([]() {

#ifdef DEBUG
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()

        Serial.println("Start updating " + type);
#endif

        requestBuffer[requestLen++] = '@';
        requestBuffer[requestLen++] = '|'; // reset PIC
        requestBuffer[requestLen++] = '|';
        immediateSend();

        ArduinoOTAflag = 1;
#ifdef COMMIT_RARE
        EEPROM.commit();
#endif
      });

      ArduinoOTA.onEnd([]() {
#ifdef DEBUG
        Serial.println("\nOTA update END");
        Serial.println("");
#endif
      });

      ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
#ifdef DEBUG
        /*    static unsigned int prev_progress;
            if (progress != prev_progress)
            {
              prev_progress = progress;
              Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
            }
        */
        Serial.printf(".");
#endif
      });

      ArduinoOTA.onError([](ota_error_t error) {
#ifdef DEBUG
        ArduinoOTAflag = 0;
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");
#endif
      });

      ArduinoOTAflag = 1;
      ArduinoOTA.begin();
#ifdef DEBUG
      Serial.println("OTA begin OK");
#endif
#endif


  
}
// -----------------------------------------------------------------------------------------------------------------------------------------------
String tcpJarg(char * mybuffer, char * argument)
{
  String val;

  char* p1 = strstr(mybuffer, argument); // cerca l'argomento
  if (p1)
  {
//  char* p2 = strstr(p1, ":");          // cerca successivo :
    char* p2 = strchr(p1, ':');          // cerca successivo :
    if (p2)
    {
//    char* p3 = strstr(p2, "\"");       // cerca successivo "
      char* p3 = strchr(p2, '\"');       // cerca successivo "
      if (p3)
      {
        p3++;
        char l = 0;
        while ((*p3 != '\"') && (l < 120))
        {
          val = val + *p3;
          p3++;
          l++;
        }
      }
    }
  }

  return val;
}
// -----------------------------------------------------------------------------------------------------------------------------------------------




// -----------------------------------------------------------------------------------------------------------------------------------------------
void loop() {

  long now = millis();

#ifdef DEBUG
  counter++;
  counter = 0;
#endif


      // ==================================== TCP server ====================================================
#ifdef USE_TCPSERVER
  if (tcpopen)
  {
   if (tcpserver.hasClient())
   {
    // client is connected
    if (!tcpclient || !tcpclient.connected())
    {
      if(tcpclient) tcpclient.stop();          // client disconnected
      tcpclient = tcpserver.available(); // ready for new client
#ifdef DEBUG
      Serial.println("CON "); // connesso !
#endif
    } else 
    {
      tcpserver.available().stop();  // have client, block new conections
#ifdef DEBUG
      Serial.println("STOP ");
#endif
    }
   }
  
   if (tcpclient && tcpclient.connected())
    tcpopen = 2;
   else
    tcpopen = 1;
  
  
   if (tcpclient && tcpclient.connected() && tcpclient.available())
   {
    char tcpBuffer[255];
    int  buflen;
    int  pos = 0;
    WORD_VAL maxp;
    maxp.Val = 0;

    char device  = 0;
    char devtype = 0;
    char deviceX = 0;
    char linesector;
    char nomeDevice[6];
    char AlexaDescr[21];


    // client input processing
    while (tcpclient.available())
    {
#ifdef DEBUG
      Serial.print(".");
#endif
      tcpBuffer[pos] = (uint8_t)tcpclient.read();
      pos++;
    }
    buflen = pos;
    tcpBuffer[pos] = 0;
    
#ifdef DEBUG 
    Serial.print("rx: ");
    Serial.write(&tcpBuffer[0], buflen);
#endif

// ------------------------------------------------------------------------------------------------------
    if (memcmp(tcpBuffer, "#request",8) == 0)
// ------------------------------------------------------------------------------------------------------
    {
      String busid = tcpJarg(tcpBuffer,"\"device\""); // bus id
      deviceX = ixOfDevice(&busid[0]);
      device = DeviceOfIx(deviceX);
      
      String sreq = tcpJarg(tcpBuffer,"\"request\""); // on-off-up-down-stop-nn%
      String scmd = tcpJarg(tcpBuffer,"\"command\""); // 0xnn

      if (scmd != "")
      {
        char command = aConvert(scmd);
        requestBuffer[requestLen++] = '§';
        requestBuffer[requestLen++] = 'y';

#ifdef KNX
// comando §y<source><linesector><destaddress><command>
        linesector = EEPROM.read(E_MQTT_TABDEVICES);
        requestBuffer[requestLen++] = 0x01;   // from device
        requestBuffer[requestLen++] = linesector; // to   device line-sector
        requestBuffer[requestLen++] = device; // to   device address
#endif

#ifdef SCS
// comando §y<destaddress><source><type><command>
        requestBuffer[requestLen++] = device; // to   device
        requestBuffer[requestLen++] = 0x00;   // from device
        requestBuffer[requestLen++] = 0x12;   // type:command
#endif
        requestBuffer[requestLen++] = command;// command
      }
    }	// #request
    else
// ------------------------------------------------------------------------------------------------------
    if (memcmp(tcpBuffer, "#putdevice",10) == 0)
// ------------------------------------------------------------------------------------------------------
    {
      String busid = tcpJarg(tcpBuffer,"\"device\"");
      if (busid != "")
      {
        deviceX = ixOfDevice(&busid[0]);
        device = DeviceOfIx(deviceX);
        if ((busid != "") && (deviceX > 0) && (deviceX < 168))
        {
          String alexadescr = tcpJarg(tcpBuffer,"\"descr\"");
          alexadescr.toCharArray(AlexaDescr, 21);  

          if (alexaParam == 'y' )
          {
            String edesc = ReadStream(&edesc[0], (int)E_ALEXA_DESC_DEVICE + (deviceX * E_ALEXA_DESC_LEN), E_ALEXA_DESC_LEN, 2); // tipo=0 binary array   1:ascii array   2:ascii string
            fauxmo.renameDevice(&edesc[0], &alexadescr[0]);
          }
          
          WriteEEP(AlexaDescr, (int) deviceX * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE, E_ALEXA_DESC_LEN);
        
          String stype = tcpJarg(tcpBuffer,"\"type\"");
          devtype = stype[0] - '0';
          if ((devtype > 0) && (devtype < 32))
             WriteEEP(devtype, deviceX + E_MQTT_TABDEVICES);
          String smaxpos = tcpJarg(tcpBuffer,"\"maxp\"");
          if (devtype == 9)
          {
            char *ch;
            maxp.Val = (int)strtoul(&smaxpos[0], &ch, 10);
            requestBuffer[requestLen++] = '§';
            requestBuffer[requestLen++] = 'U';
            requestBuffer[requestLen++] = '8';
            requestBuffer[requestLen++] = device;     // device id
            requestBuffer[requestLen++] = devtype;    // device type
            requestBuffer[requestLen++] = maxp.byte.HB;    // max position H
            requestBuffer[requestLen++] = maxp.byte.LB;    // max position L
            immediateSend();
            immediateReceive('k');
          } // devtype == 9
#ifdef DEBUG 
          sprintf(tcpBuffer, "{\"device\":\"%02X\",\"type\":\"%d\",\"maxp\":\"%d\"\
                            ,\"descr\":\"%s\"}",device,devtype,maxp.Val,AlexaDescr);
#endif
        } // deviceX > 0
      }  // busid != ""

      String cover = tcpJarg(tcpBuffer,"\"coverpct\"");
      if (cover == "false")
      {
        requestBuffer[requestLen++] = '§';
        requestBuffer[requestLen++] = 'U';
        requestBuffer[requestLen++] = '9';
        immediateSend();
        immediateReceive('k');
      }  // cover == "false"

      sprintf(tcpBuffer, "#ok");
      buflen = 0;
      while (tcpBuffer[buflen]) buflen++;
      tcpclient.write(tcpBuffer, buflen);
      EEPROM.commit();
      tcpclient.flush();
    }	// #putdevice
    else
// ------------------------------------------------------------------------------------------------------
    if (memcmp(tcpBuffer, "#getdevice",10) == 0)
// ------------------------------------------------------------------------------------------------------
    {
      deviceX = 1;
      while (deviceX < 168)
      {
        devtype = EEPROM.read(deviceX + E_MQTT_TABDEVICES);
        if ((devtype > 0) && (devtype < 32))
        {
          device = DeviceOfIx(deviceX, nomeDevice);
          ReadStream(&AlexaDescr[0], (int) deviceX * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE, E_ALEXA_DESC_LEN, 1); // tipo=0 binary array   1:ascii array   2:ascii string
          maxp.Val = 0;
          if (devtype == 9)
          {
            requestBuffer[requestLen++] = '§';
            requestBuffer[requestLen++] = 'U';
            requestBuffer[requestLen++] = '6';
            requestBuffer[requestLen++] = device;
            immediateSend();
            char m = immediateReceive('[');
            
            if (m > 8)
            {
              maxp.Val = replyBuffer[2] | replyBuffer[1] << 8;
            }
            
          }  // devtype == 9
          sprintf(tcpBuffer, "{\"device\":\"%s\",\"type\":\"%d\",\"maxp\":\"%d\"\
                              ,\"descr\":\"%s\"}",nomeDevice,devtype,maxp.Val,AlexaDescr);
          buflen = 0;
          while (tcpBuffer[buflen]) buflen++;
          tcpclient.write(tcpBuffer, buflen);
          tcpclient.flush();
        } // devtype > 0
        deviceX++;
      } // while devicex < 168
     
      sprintf(tcpBuffer, "#eof");
      buflen = 0;
      while (tcpBuffer[buflen]) buflen++;
      tcpclient.write(tcpBuffer, buflen);
      tcpclient.flush();
    } // received "#getdevices"
    else
// ------------------------------------------------------------------------------------------------------
    if (memcmp(tcpBuffer, "#setup ",7) == 0)
// ------------------------------------------------------------------------------------------------------
    {
      String uart = tcpJarg(tcpBuffer,"\"uart\"");
      if (uart == "tcp")  
      {
        tcpuart = 1;
//      udpopen = 0;
      }
      else
      if (uart == "udp")
      {
        tcpuart = 0;
//      udpopen = 1;
      }

      String freq = tcpJarg(tcpBuffer,"\"frequency\"");
      if (freq == "160")  
      {
        system_update_cpu_freq(160);
      }
      else
      if (freq == "80")
      {
        system_update_cpu_freq(80);
      }

      String debug = tcpJarg(tcpBuffer,"\"debug\"");
      if (debug == "tcp")  
      {
        tcpuart = 2;
      }

      sprintf(tcpBuffer, "#ok");
      buflen = 0;
      while (tcpBuffer[buflen]) buflen++;
      tcpclient.write(tcpBuffer, buflen);
      tcpclient.flush();
    }
// ------------------------------------------------------------------------------------------------------
    else
    {
      if (tcpuart == 1)
      {
        firstTime = 0;  // msg udp: ora i comandi MQTT vanno fatti precedere dal setup
        int s = 0;
        while (s < buflen)
        {
          Serial.write(tcpBuffer[s]);   // write on serial KNXgate/SCSgate
          delayMicroseconds(120);
          s++;
        }
        
//      if (replyLen > 0)
//      {
//        tcpclient.write(replyBuffer, replyLen);
//      }
      }
      else
      {
#ifdef DEBUG
        Serial.print("error");
#endif
        tcpclient.write(tcpBuffer, buflen);
//        tcpclient.flush(); 
        tcpclient.write("#err", 4);
//        tcpclient.flush(); 
      }
    }
   } // tcpclient connected and available
  } // tcpopen
  // =========================================== FINE TCP ==========================================
#endif







  // ================================================ OTA ==========================================
#ifdef USE_OTA
  if (ArduinoOTAflag == 1)
  {
    if (now - prevTime > 100)
    {
      prevTime = now;
      ArduinoOTA.handle();
   #ifdef BLINKLED
      ledCtr++;
      if (ledCtr > 9)
      {
        ledCtr = 0;
        digitalWrite(LED_BUILTIN, LOW);
      }
      else
      {
        digitalWrite(LED_BUILTIN, HIGH);
      }
   #endif
    }
  }
#endif

  // ======================================= MQTT KEEP ALIVE ===========================================
  if (mqttopen != 0)
  {

    if (mqttopen == 3)
    {
      client.loop();
      if (!client.connected())
      {
        mqttopen = 1;
        lastMsg  = 0;
        now  = 0;
      }
    }

    if (mqttopen == 2)
    {
      client.loop();
      client.subscribe(SUBSCRIBE1);
      client.subscribe(SUBSCRIBE2);
      client.subscribe(SUBSCRIBE3);
      mqttopen = 3;
    }

    if (mqttopen == 1)
    {
      if (now - lastMsg > 10000)
      { // nuova versione - controllo connessione mqtt solo ogni 10 secondi
        lastMsg = now;         //                per evitare overload
        lastMsg += 100;        // per gestire overflow
#ifdef DEBUG
        Serial.println("mqtt not connected");
#endif
        mqttopen = reconnect();
#ifdef DEBUG
        if (mqttopen == 2)
          Serial.println("ok ready");
        else
          Serial.println("ko");
#endif
        if (mqttopen == 1) // connection failed
        {
          mqtt_retry++;
          if ((mqtt_retrylimit > 0) && (mqtt_retry > mqtt_retrylimit))
            mqttopen = 0;
        }
        else
          mqtt_retry = 0;
      }
    }
  }
  // ===================================== END MQTT KEEP ALIVE ===========================================



  if (udpopen == 1)
  {
    // ====================================== receive from UDP - send to SERIAL =====================
    int packetSize = udpConnection.parsePacket();

    if (packetSize)
    {
#ifdef DEBUG
      Serial.println ( "UDP packet received" );
#endif
      int len = udpConnection.read(udpBuffer, 255);
      packetSize = len;
      udp_remote_ip = udpConnection.remoteIP();
      udp_remote_port = udpConnection.remotePort();
      udpBuffer[len] = 0;

      httpResp = "";

      /*
                  if (strcmp(packetBuffer,"@V") == 0) // query version
                  {
                    int s;
                    for (s=0; s<sizeof(VERSION); s++)
                    {
                      Serial.write(VERSION[s]);   // write on serial KNXgate/SCSgate
                      delayMicroseconds(50);
                    }
                  }
                  else
      */
      
      if (strcmp(udpBuffer, "@Keep_alive") == 0)
      {
      }
      else if (strcmp(udpBuffer, "@Kill") == 0)
      {
        udp_remote_ip = {0, 0, 0, 0};
      }
      else
      {
        firstTime = 0;  // msg udp: ora i comandi MQTT vanno fatti precedere dal setup
        int s = 0;
        while (s < len)
        {
          Serial.write(udpBuffer[s]);   // write on serial KNXgate/SCSgate
//        delayMicroseconds(120);
          s++;
        }
        tcpuart = 0;
      }
    } // packetsize
  }   // udpopen == 1



  //    delay(1);  // wait 1 mS (!)
  delayMicroseconds(500);


  // ====================================== receive from SERIAL - send to UDP =====================


  replyLen = 0;
  internal = 0;
  if (Serial.available() )
  {
    String log = "rx: ";
    char logCh[4];
    char lmax;
    char prefix = Serial.read();        // receive from serial USB
#ifdef DEBUG
    manualInput(prefix);
    prefix = 0;
#endif
    if ((prefix > 0xF0) && (prefix < 0xF9)) // 0xf5 y aa bb cc dd
    {
      internal = 1;
      lmax = (prefix & 0x0F);
      lmax++;
      if (mqtt_log == 'y')
      {
        sprintf(logCh, "%02X > ", prefix);
        log += logCh;
      }
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
      if (tcpuart == 2)  
      {
        sprintf(logCh, "%02X > ", prefix);
        log += logCh;
      }
  #endif
#endif
      replyBuffer[replyLen++] = prefix;
    }
    else
    {
      internal = 0;
      replyBuffer[replyLen++] = prefix;
      lmax = 255;
      if (mqtt_log == 'y')
      {
        sprintf(logCh, "%02X ", prefix);
        log += logCh;
      }
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
      if (tcpuart == 2)  
      {
        sprintf(logCh, "%02X ", prefix);
        log += logCh;
      }
  #endif
#endif
    }
    while (Serial.available() && (replyLen < lmax))
    {
      while (Serial.available() && (replyLen < lmax))
      {
        replyBuffer[replyLen] = Serial.read();        // receive from serial USB
        if (mqtt_log == 'y')
        {
          sprintf(logCh, "%02X ", replyBuffer[replyLen]);
          log += logCh;
        }
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
        if (tcpuart == 2)  
        {
          sprintf(logCh, "%02X ", replyBuffer[replyLen]);
          log += logCh;
        }
  #endif
#endif
        replyLen++;
        delayMicroseconds(INNERWAIT);
      }
      delayMicroseconds(OUTERWAIT);
    }


    if ((mqtt_log == 'y') && (replyLen > 0)) WriteLog(log);
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
    if ((tcpuart == 2) && (tcpclient) && (tcpclient.connected())) 
    {
      tcpclient.write((char*)&log[0], log.length());
      tcpclient.flush(); 
    }
  #endif
#endif
  } // serial_available

  replyBuffer[replyLen] = 0;



  // --------------------- RISPOSTA UDP se richiesto -------------------------------------------


  if (tcpuart == 0)
  {
    if ((udpopen == 1) && (udp_remote_ip) && (replyLen) && (internal == 0))
    {
      if (httpResp == "")
      {
        int success;
        do  {
          success =  udpConnection.beginPacket(udp_remote_ip, udp_remote_port);
        }   while (!success);

        int n = 0;
        while (n < replyLen)
        {
          udpConnection.write(replyBuffer[n++]);     // UDP reply
        }
        success = udpConnection.endPacket();
      }
    } // udp_remote_ip
  }


#ifdef USE_TCPSERVER
  else
  if ((tcpuart == 1) && (tcpclient) && (tcpclient.connected())) 
  {
    tcpclient.write(replyBuffer, replyLen);
//  tcpclient.flush(); 
  }
#endif


  // --------------------- RISPOSTA HTTP se richiesto -------------------------------------------


  if (((httpResp == "a") || (httpResp == "y")) && (httpCallback[0] == ':'))
  {
    if ((replyBuffer[0] == 0xF5) && (replyBuffer[1] == 'y') && (replyLen == 6))   // solo se stringa=   [0xF5] [y] 32 00 12 01
    {
      content = "http://";
      content += tcp_remote_ip.toString();
      content += httpCallback;
      char hBuffer[12];

#ifdef SCS
      // intero  [7] A8 32 00 12 01 21 A3
      // ridotto [0xF5] [y] 32 00 12 01
      // ----------------1---2--3--4--5--
      sprintf(hBuffer, "&type=%02X", replyBuffer[4]);
      content += hBuffer;
      sprintf(hBuffer, "&from=%02X", replyBuffer[3]);
      content += hBuffer;
      sprintf(hBuffer, "&to=%02X", replyBuffer[2]);
      content += hBuffer;
      sprintf(hBuffer, "&cmd=%02X", replyBuffer[5]);
      content += hBuffer;
#endif

#ifdef KNX
      // intero  [9] B4 10 29 0B 65 E1 00 81 7C
      // ridotto  [0xF5] [y] 29 0B 65 81
      // -----------------1---2--3--4--5--
      sprintf(hBuffer, "&from=%02X", replyBuffer[2]);
      content += hBuffer;
      sprintf(hBuffer, "&to=%02X%02X", replyBuffer[3], replyBuffer[4]);
      content += hBuffer;
      sprintf(hBuffer, "&cmd=%02X", replyBuffer[5]);
      content += hBuffer;
#endif

      httpClient.begin(content);
      int httpCode = httpClient.GET();                                  //Send the request
      httpClient.end();   //Close connection
    }
  } // httpResp == "a"
  // --------------------- FINE RISPOSTA HTTP -------------------------------------------





  // =========================================== M Q T T  PUBLISH=================================================


  // ------------[0xF3] [D] 32 00 ----(address,type)---------- CENSIMENTO DEVICES -------------------------------------------------------
  if ((replyLen == 4) && (devIx > 0) && (replyBuffer[0] == 0xF3) && (replyBuffer[1] == DEVICEREQUEST)) // started from handleMqttDevices
  {
    char devtype;
    char devCall = devIx;
    devIx   = replyBuffer[2];
    devtype = replyBuffer[3];
    if ((devtype == 0xFF) || ((devCall > 1) && (devIx < devCall)))
    {
      EEPROM.commit();
      if (firstTime == 0) setFirst();  // DEVONO essere attivi @MX  e §l
      devIx = 0;
      uartSemaphor = 1;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = 'U';
      requestBuffer[requestLen++] = '7'; // chiede di ripubblicare tutte le posizioni
      uartSemaphor = 0;
    }
    else if (devIx == 0)
    {
      mqttSectorLine = devtype;
      EEPROM.write(E_MQTT_TABDEVICES, devtype);
      devIx++;
      uartSemaphor = 1;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = DEVICEREQUEST;
      requestBuffer[requestLen++] = devIx;
      uartSemaphor = 0;
    }
    else
    {

#ifdef SCS
      MQTTnewdevice(devtype, devIx);
      devCtr++;
#endif
#ifdef KNX
      MQTTnewdevice(devtype, mqttSectorLine, devIx);
      devCtr++;
#endif
      EEPROM.write(devIx + E_MQTT_TABDEVICES, devtype);

      char dsc = EEPROM.read((int) devIx * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE);

      if ((dsc == 0) || (dsc == 0xFF))
      {
        // descrizione standard per ALEXA - E_ALEXA_DESC_DEVICE 1024 // device description max 168 x E_ALEXA_DESC_LEN char
        char AlexaDescr[E_ALEXA_DESC_LEN];
#ifdef SCS
        sprintf(AlexaDescr, "dispositivo scs %02X", devIx);  // device
#endif
#ifdef KNX
        sprintf(AlexaDescr, "dispositivo knx ");
        DeviceOfIx(devIx, (char *) &AlexaDescr[16]);
#endif
        WriteEEP((char *) AlexaDescr, (int) devIx * E_ALEXA_DESC_LEN + E_ALEXA_DESC_DEVICE, E_ALEXA_DESC_LEN);
      }

      devIx++;
      uartSemaphor = 1;
      requestBuffer[requestLen++] = '§';
      requestBuffer[requestLen++] = DEVICEREQUEST;
      requestBuffer[requestLen++] = devIx;
      uartSemaphor = 0;
    }
  } //  replyLen == 4...   fine censimento

  // ----------------------------------------- FINE CENSIMENTO DEVICES -------------------------------------------------------
  else

  // ---------------------u-posizione tapparelle %----------------------------------------------------------------------------
  if ((replyLen == 4) && (replyBuffer[0] == 0xF3) && (replyBuffer[1] == 'u'))    //u//
  { // replyLen==4 && replyBuffer == 0xF3 'u'
  // ----------------------------------------- ALEXA STATO DEVICES ---------------------------------------------------------
    if ((alexaParam == 'y') && (replyLen == 4) && (replyBuffer[0] == 0xF3) && (replyBuffer[1] == 'u'))    //u//
    { // aggiornamento posizione coverpct fauxmo   [0xF3] [u] 32 00
      unsigned char id_alexa = 0;

      while ((alexa_BUS_id[id_alexa] != replyBuffer[2]) && (id_alexa <= id_fauxmo)) {
        id_alexa++;
      };  // index: device id alexa max 168 devices    data: ID scs
      if (id_alexa <= id_fauxmo)
      {
        int pct = replyBuffer[3];
        pct *= 255;
        pct /= 100;
        if (pct == 0)  pct = 1;
        if (pct > 255) pct = 255;
        fauxmo.setState(id_alexa, 0xFF, pct);
      }
    } // alexaParam == 'y'
    
#ifdef NO_ALEXA_MQTT
    else
#endif    

    // ----------------------------------------- ALEXA STATO DEVICES END----------------------------------------------------

    // --------SCS-----uab-(address position)--- PUBBLICAZIONE STATO COVERPCT -------------------------------------------------------
    if ((mqttopen == 3) && (replyLen == 4) && (replyBuffer[0] == 0xF3) && (replyBuffer[1] == 'u'))    //u//
    { // pubblicazione posizione coverpct   [0xF3] [u] 32 00
      char actionc[6];

      sprintf(actionc, "%03u", replyBuffer[3]);     // position
      char nomeDevice[5];
#ifdef SCS
      sprintf(nomeDevice, "%02X", replyBuffer[2]);  // device
#endif
#ifdef KNX
      char sectorline = EEPROM.read(E_MQTT_TABDEVICES);
      sprintf(nomeDevice, "%02X%02X", sectorline, replyBuffer[2]);  // device
#endif
      String topic = COVERPCT_STATE;
      topic += nomeDevice;
      const char* cTopic = topic.c_str();
      client.publish(cTopic, actionc, mqtt_persistence);
    } // mqttopen == 3
    replyLen = 0; // per impedire pubblicazione UDP
  } // replyLen==4 && replyBuffer == 0xF3 'u'
    
  else




  // --------SCS-----4abcd-(from-to-type-cmd)------- PUBBLICAZIONE STATO DEVICES -------------------------------------------------------
  // --------KNX-----4abcd-(from-to-cmd)------------ PUBBLICAZIONE STATO DEVICES -------------------------------------------------------

  if ((mqttopen == 3) && (replyLen == 6) && (replyBuffer[0] == 0xF5) && (replyBuffer[1] == 'y'))
  { // START pubblicazione stato device        [0xF5] [y] 32 00 12 01
    char devtype; //                                  0x79
    char action;
    String topic = "NOTOPIC";
    String payload = "";

    // ================================ C O M A N D I     S C S ===========================================
#ifdef SCS

    // SCS intero   [7] A8 32 00 12 01 21 A3
    // SCS ridotto [0xF5] [y] 32 00 12 01

    char device = 0;
    if (replyBuffer[4] == 0x12)  // <-comando----------------------------
    {
      action = replyBuffer[5];
      if (replyBuffer[2] == 0xB1)   // <------------ indirizzato a TUTTI i devices
      {
        char nomeDevice[4];
        const char* cPayload;
        const char* cTopic;
        if (action == 0)
          payload = "ON";
        else if (action == 1)
          payload = "OFF";
        for (device = 1; device < 168; device++)
        {
          devtype = EEPROM.read(device + E_MQTT_TABDEVICES);
          if ((devtype == 1) || (devtype == 3))  // dimmer
          {
            sprintf(nomeDevice, "%02X", device);  // to
            topic = SWITCH_STATE;
            topic += nomeDevice;
            cPayload = payload.c_str();
            cTopic = topic.c_str();
            client.publish(cTopic, cPayload, mqtt_persistence);
          }
        }
        device = 0;
        devtype = 0;
      }                            // <------------ indirizzato a TUTTI i devices
      else
      {
        if (replyBuffer[2] < 0xB0)
          device = replyBuffer[2];  // to
        else
          device = replyBuffer[3];  // from
      }
    } // <-replyBuffer[4] == 0x12---------------------comando----------------------------

    if ((action == 0) || (action == 1))       // switch
      devtype = 1;
    else if ((action == 3) || (action == 4))      // dimmer
      devtype = 0; // sono solo richieste di +/- , poi arrivera' l'intensita'
    else if ((action == 0x08) || (action == 0x09) || (action == 0x0A))   // cover
      devtype = 8;
    else if ((action & 0x0F) == 0x0D) // da 0x1D a 0x9D
    {
      devtype = 3;
      action >>= 4;
      action *= 10;	// percentuale 10-90

      // --------------- percentuale da 1 a 255 (home assistant) ----------------
      //         if (domoticMode == 'h')   // h=as homeassistant
      {
        int pct = action;
        pct *= 255;      // da 0 a 25500
        pct /= 100;      // da 0 a 100
        action = (char) pct;
      }
    }
    else
      devtype = 0;





    if ((device != 0) && (devtype != 0))
      //      if ((device != 0) && (devtype != 0) && ((device != prevDevice) || (action != prevAction)))
    { // device valido & evitare doppioni
      prevDevice = device;
      prevAction = action;

      char nomeDevice[5];
      sprintf(nomeDevice, "%02X", device);  // to

      // ----------------------------------------- STATO SWITCH --------------------------------------------
      if (devtype == 1)
      {
        if (action == 0)
        {
          payload = "ON";
        }
        else if (action == 1)
        {
          payload = "OFF";
        }
        topic = SWITCH_STATE;
        topic += nomeDevice;
      }
      else
        // ----------------------------------------- STATO DIMMER --------------------------------------------
      if (devtype == 3)
      {
        char actionc[4];
        sprintf(actionc, "%02u", action);
        payload = String(actionc);
        topic = BRIGHT_STATE;
        topic += nomeDevice;
      }
      else
        // ----------------------------------------- STATO COVER --------------------------------------------
      if (devtype == 8)
      {
        if (action == 0x08)
        {
          if (domoticMode == 'h')
            payload = "open";
          else
            payload = "OFF";
        }
        else if (action == 0x09)
        {
          if (domoticMode == 'h')
            payload = "closed";
          else
            payload = "ON";
        }
        else if (action == 0x0A)
        {
          payload = "STOP";
        }

        topic = COVER_STATE;
        topic += nomeDevice;
      } // devtype=8
  // ----------------------------------------------------------------------------------------------------
  // ================================ F I N E   C O M A N D I     S C S ===========================================
#endif  // def SCS           










  // ================================ C O M A N D I     K N X  ===========================================
#ifdef KNX
  // KNX intero  [9] B4 10 29 0B 65 E1 00 81 7C
  // knx ridotto  [0xF5] [y] 29 0B 65 81

  //    word device = word(replyBuffer[2], replyBuffer[3]);

      char sectorline = replyBuffer[3];  // sector & line da telegramma
      char device = replyBuffer[4];      // id device da telegramma
      char dispari = 0;
      char eedev;

      action = replyBuffer[5];
      eedev = device;
      if (device & 0x01)
      {
        dispari = 1;           // dispari: id domoticz = id telegramma
      }
      else
      {
        device--;              // pari:    id domoticz = id telegramma - 1
      }
            
      eedev = ixOfDevice(device);

      devtype = EEPROM.read(eedev + E_MQTT_TABDEVICES);
      if ((devtype < 0x01) || (devtype > 8))
        devtype = 0x01;

      if ((device != 0) && (devtype != 0))
        //    if ((device != 0) && (devtype != 0) && ((device != prevDevice) || (action != prevAction)))
      { // device valido & evitare doppioni
        prevDevice = device;
        prevAction = action;
        char nomeDevice[6];
        sprintf(nomeDevice, "%02X%02X", sectorline, device);  // to
        // ----------------------------------------- STATO SWITCH --------------------------------------------
        if (devtype == 1)
        {
          if (action == 0x81)
          {
             payload = "ON";
          }
          else if (action == 0x80)
          {
            payload = "OFF";
          }
          topic = SWITCH_STATE;
          topic += nomeDevice;
        }
        else
        // ----------------------------------------- STATO DIMMER --------------------------------------------
        if (devtype == 4)
        {

        }
        else
        // ----------------------------------------- STATO COVER --------------------------------------------
        if (devtype == 8)
          if (dispari)    // stop
          {
            if ((action == 0x80) || (action == 0x81))
            {
              payload = "STOP";
            }
            topic = COVER_STATE;
            topic += nomeDevice;
          }
          else   //    (pari)    // up down
          {
            if (action == 0x80)
            {
              if (domoticMode == 'h')
                payload = "open";
              else
                payload = "OFF";
            }
            else if (action == 0x81)
            {
              if (domoticMode == 'h')
                payload = "closed";
              else
                payload = "ON";
            }

            topic = COVER_STATE;
            topic += nomeDevice;
          }

    // ----------------------------------------------------------------------------------------------------
    // ================================ F I N E   C O M A N D I     K N X ===========================================
#endif  // def KNX           








    // ======================================== P U B B L I C A Z I O N E ===========================================
          const char* cTopic = topic.c_str();
          if (payload == "")
          {
            char cPayload[24];
            sprintf(cPayload, "UNKNOWN: %02X %02X %02X %02X", replyBuffer[1], replyBuffer[2], replyBuffer[3], replyBuffer[4]);  // to
            client.publish(cTopic, cPayload, 0);
          }
          else
          {
            const char* cPayload = payload.c_str();
            client.publish(cTopic, cPayload, mqtt_persistence);
          }
        }      // evitare doppioni & device valido
      } // END pubblicazione stato devices

      // ===============================================================================================================




          // =====================================fauxmo handle (ALEXA) ====================================================
          if (alexaParam == 'y' )
          {
            // fauxmoESP uses an async TCP server but a sync UDP server
            // Therefore, we have to manually poll for UDP packets
            fauxmo.handle();

#ifdef DEBUG
            // This is a sample code to output free heap every 15 seconds
            // This is a cheap way to detect memory leaks
            //      static unsigned long last = millis();
            //      if (millis() - last > 15000) {
            //      last = millis();
            //      Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());
            //      }
#endif

            // If your device state is changed by any other means (MQTT, physical button,...)
            // you can instruct the library to report the new state to Alexa on next request:
            // fauxmo.setState(ID_YELLOW, true, 255);
          }  // alexaParam == 'y'
          // ===============================================================================================================






          // =============================CICLO SCRITTURA BUFFER UART ASYNCH=================================================

          if (asynchLen > 0)
          {
            uartSemaphor = 1;
            // =========================== send control char and data over serial =============================
            String log = "tx: ";
            char logCh[4];
            int s = 0;
            char continua = 1;

            while (continua)
//          while (s < asynchLen)
            {
              //       Serial.flush();
              //       Serial.write(asynchBuffer[s]);   // write on serial KNXgate/SCSgate - 90uS


              // USS = uart register 1C-19 (32 bit)  bit 16-23=data nr in tx fifo   (USTXC = 16)
              // UART STATUS Registers Bits
              // USTX    31 //TX PIN Level
              // USRTS   30 //RTS PIN Level
              // USDTR   39 //DTR PIN Level
              // USTXC   16 //TX FIFO COUNT (8bit)
              // USRXD   15 //RX PIN Level
              // USCTS   14 //CTS PIN Level
              // USDSR   13 //DSR PIN Level
              // USRXC    0 //RX FIFO COUNT (8bit)

#ifdef DEBUG
#else
              while (((USS(0) >> USTXC) & 0xff) > 0)     {	// aspetta il buffer sia completamente vuoto
                delay(0);
              }

              delayMicroseconds(OUTERWAIT);
              USF(0) = asynchBuffer[s];    // scrittura SERIALE

              while (((USS(0) >> USTXC) & 0xff) > 0)     {	// aspetta il buffer sia completamente vuoto
                delay(0);
              }
#endif
#ifdef DEBUG
              sprintf(logCh, "%02X ", asynchBuffer[s]);
              log += logCh;
#else
              if (mqtt_log == 'y')
              {
                sprintf(logCh, "%02X ", asynchBuffer[s]);
                log += logCh;
              }
#endif
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
              if (tcpuart == 2) 
              {
                sprintf(logCh, "%02X ", asynchBuffer[s]);
                log += logCh;
              }
  #endif
#endif

              s++;
              if (s >= asynchLen)
              {
                continua = 0;
              }
              else
                 delayMicroseconds(OUTERWAIT); // 100uS
            }
#ifdef DEBUG
            Serial.println("\r\n" + log);
#else
            if (mqtt_log == 'y') WriteLog(log);
#endif

#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
            if ((tcpuart == 2) && (tcpclient) && (tcpclient.connected())) 
            {
              tcpclient.write((char*)&log[0], log.length());
              tcpclient.flush(); 
            }
  #endif
#endif

            asynchLen = 0;
            uartSemaphor = 0;
          } // asynchlen > 0
          










          




          // ===================================CICLO SCRITTURA BUFFER UART=================================================

          if (requestLen > 0)
          {
            uartSemaphor = 1;
            // =========================== send control char and data over serial =============================
            String log = "tx: ";
            char logCh[4];
            int s = 0;
            while (s < requestLen)
            {
              //       Serial.flush();
              //       Serial.write(requestBuffer[s]);   // write on serial KNXgate/SCSgate - 90uS


              // USS = uart register 1C-19 (32 bit)  bit 16-23=data nr in tx fifo   (USTXC = 16)
              // UART STATUS Registers Bits
              // USTX    31 //TX PIN Level
              // USRTS   30 //RTS PIN Level
              // USDTR   39 //DTR PIN Level
              // USTXC   16 //TX FIFO COUNT (8bit)
              // USRXD   15 //RX PIN Level
              // USCTS   14 //CTS PIN Level
              // USDSR   13 //DSR PIN Level
              // USRXC    0 //RX FIFO COUNT (8bit)

#ifdef DEBUG
#else
              while (((USS(0) >> USTXC) & 0xff) > 0)     {	// aspetta il buffer sia completamente vuoto
                delay(0);
              }

              delayMicroseconds(OUTERWAIT);
              USF(0) = requestBuffer[s];    // scrittura SERIALE

              while (((USS(0) >> USTXC) & 0xff) > 0)     {	// aspetta il buffer sia completamente vuoto
                delay(0);
              }
#endif
#ifdef DEBUG
              sprintf(logCh, "%02X ", requestBuffer[s]);
              log += logCh;
#else
              if (mqtt_log == 'y')
              {
                sprintf(logCh, "%02X ", requestBuffer[s]);
                log += logCh;
              }
#endif
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
              if (tcpuart == 2) 
              {
                sprintf(logCh, "%02X ", requestBuffer[s]);
                log += logCh;
              }
  #endif
#endif
              delayMicroseconds(OUTERWAIT); // 100uS
              s++;
            }
#ifdef DEBUG
            Serial.println("\r\n" + log);
#else
            if (mqtt_log == 'y') WriteLog(log);
#endif

#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
            if ((tcpuart == 2) && (tcpclient) && (tcpclient.connected())) 
            {
              tcpclient.write((char*)&log[0], log.length());
              tcpclient.flush(); 
            }
  #endif
#endif

            requestLen = 0;
            uartSemaphor = 0;
          } // requestlen > 0
          
          
          // =====================================================================================================
          // =====================================================================================================
          if (webon == 1)
            server.handleClient();
          // =====================================================================================================
          // =====================================================================================================
        }


// =====================================================================================================
// =====================================================================================================
#ifdef KNX
char ixOfDevice(char * knxdevice)
{
  char device;
  char *ch;
  if (*(knxdevice+2) == 0) // 2 caratteri
      device = (char)strtoul(knxdevice, &ch, 16);
  else
  if (*(knxdevice+4) == 0) // 4 caratteri
  {
      char sectorline = EEPROM.read(E_MQTT_TABDEVICES);
      device = (char)strtoul(knxdevice+2, &ch, 16);
      *(knxdevice+2) = 0; // 2 caratteri
      char sectlin = (char)strtoul(knxdevice, &ch, 16);
      if (sectlin != sectorline)
          EEPROM.write(E_MQTT_TABDEVICES, sectlin);
  }
  else
  {
    *(knxdevice+2) = 0; // 2 caratteri
    device = (char)strtoul(knxdevice, &ch, 16);
  }

  device++;                  // id eeprom = id telegramma + 1 >> 1
  device>>=1;
//  devtype = EEPROM.read(knxdevice + E_MQTT_TABDEVICES);
  return device;
}
//--------------------------------------------------------------------
char ixOfDevice(char knxdevice)
{
//  0x4F (0100 1111) --> (0010 1000) 0x28
//  0x50 (0101 0000) --> (0010 1000) 0x28
//  0x51 (0101 0001) --> (0010 1001) 0x29
//  0x51 (0101 0010) --> (0010 1001) 0x29

  knxdevice++;                  // id eeprom = id telegramma + 1 >> 1
  knxdevice>>=1;
//  devtype = EEPROM.read(knxdevice + E_MQTT_TABDEVICES);
  return knxdevice;
}
//--------------------------------------------------------------------
char DeviceOfIx(char ixdevice)
{
//  (0010 1000) 0x28 --> 0x4F (0100 FFFF)  
//  (0010 1001) 0x29 --> 0x51 (0101 0001)
 
  char device = ixdevice;
  device<<=1;   // ricostruito a 8 bit
  device--;      // base dispari
  return device;
}
//--------------------------------------------------------------------
char DeviceOfIx(char ixdevice, char * knxname)
{
  char sectorline = EEPROM.read(E_MQTT_TABDEVICES);
  char device = ixdevice;
  device<<=1;   // ricostruito a 8 bit
  device--;      // base dispari
  sprintf(knxname, "%02X%02X", sectorline, device);  // to
  return device;
}
//--------------------------------------------------------------------
char DeviceOfIx(char sectorline, char ixdevice, char * knxname)
{
  char device = ixdevice;
  device<<=1;   // ricostruito a 8 bit
  device--;      // base dispari
  sprintf(knxname, "%02X%02X", sectorline, device);  // to
  return device;
}
#endif
//--------------------------------------------------------------------


//--------------------------------------------------------------------
#ifdef SCS
char ixOfDevice(char * scsdevice)
{
  char device;
  char *ch;
  *(scsdevice+2) = 0; // 2 caratteri
  device = (char)strtoul(scsdevice, &ch, 16);

  return device;
//devtype = EEPROM.read(knxdevice + E_MQTT_TABDEVICES);
}
//--------------------------------------------------------------------
char ixOfDevice(char scsdevice)
{
 return scsdevice;
}
//--------------------------------------------------------------------
char DeviceOfIx(char ixdevice)
{
  return ixdevice;
}
//--------------------------------------------------------------------
char DeviceOfIx(char ixdevice, char * scsname)
{
  sprintf(scsname, "%02X", ixdevice);
  return ixdevice;
}
#endif
// =====================================================================================================
// =====================================================================================================
char SendToPIC(char bufNr)
{
   char len = serOlen[bufNr];
   // =============================CICLO SCRITTURA BUFFER UART =================================================
   if (len > 0)
   {
     bufSemaphor = bufNr;
     // =========================== send control char and data over serial =============================
     String log = "tx: ";
     char logCh[4];
     int s = 0;
     while (s < len)
     {
       // USS = uart register 1C-19 (32 bit)  bit 16-23=data nr in tx fifo   (USTXC = 16)
       // UART STATUS Registers Bits
       // USTX    31 //TX PIN Level
       // USRTS   30 //RTS PIN Level
       // USDTR   39 //DTR PIN Level
       // USTXC   16 //TX FIFO COUNT (8bit)
       // USRXD   15 //RX PIN Level
       // USCTS   14 //CTS PIN Level
       // USDSR   13 //DSR PIN Level
       // USRXC    0 //RX FIFO COUNT (8bit)

#ifdef DEBUG
#else
       while (((USS(0) >> USTXC) & 0xff) > 0)     
       {	// aspetta il buffer sia completamente vuoto
          delay(0);
       }
       delayMicroseconds(OUTERWAIT);
       USF(0) = serObuffer[bufNr,s];    // scrittura SERIALE
       while (((USS(0) >> USTXC) & 0xff) > 0)     
       {	// aspetta il buffer sia completamente vuoto
          delay(0);
       }
#endif
#ifdef DEBUG
       sprintf(logCh, "%02X ", serObuffer[bufNr,s]);
       log += logCh;
#else
       if (mqtt_log == 'y')
       {
         sprintf(logCh, "%02X ", serObuffer[bufNr,s]);
         log += logCh;
       }
#endif
#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
       if (tcpuart == 2) 
       {
         sprintf(logCh, "%02X ", serObuffer[bufNr,s]);
         log += logCh;
       }
  #endif
#endif
       s++;
       if (s < len)
          delayMicroseconds(OUTERWAIT); // 100uS

#ifdef DEBUG
       Serial.println("\r\n" + log);
#else
       if (mqtt_log == 'y') WriteLog(log);
#endif

#ifdef USE_TCPSERVER
  #ifdef DEBUG_FAUXMO_TCP         
       if ((tcpuart == 2) && (tcpclient) && (tcpclient.connected())) 
       {
          tcpclient.write((char*)&log[0], log.length());
          tcpclient.flush(); 
       }
  #endif
#endif
     serOlen[bufNr] = 0;
     bufSemaphor = -1;
   } // len > 0
}  
// =====================================================================================================
// =====================================================================================================


