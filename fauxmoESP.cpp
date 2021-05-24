/*

  FAUXMO ESP

  Copyright (C) 2016-2018 by Xose Pérez <xose dot perez at gmail dot com>

  The MIT License (MIT)

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*/

#include <Arduino.h>
#include "fauxmoESP.h"
unsigned char iDevice = 0;
unsigned char iDiscovered = 0;
unsigned int  iqueryed = 0;
String macaddress;
String macshort;


// -----------------------------------------------------------------------------
// UDP
// -----------------------------------------------------------------------------

void fauxmoESP::_sendUDPResponse() {

  DEBUG_MSG_FAUXMO("[FAUXMO] Responding to M-SEARCH request\r\n");

  IPAddress ip = WiFi.localIP();

  char response[strlen(FAUXMO_UDP_RESPONSE_TEMPLATE) + 128];
  snprintf_P(
    response, sizeof(response),
    FAUXMO_UDP_RESPONSE_TEMPLATE,
    ip[0], ip[1], ip[2], ip[3],
    _tcp_port,
    macaddress.c_str(), macaddress.c_str()
  );

#if DEBUG_FAUXMO_VERBOSE_UDP
  DEBUG_MSG_FAUXMO("[FAUXMO] UDP response sent to %s:%d\r\n%s", _udp.remoteIP().toString().c_str(), _udp.remotePort(), response);
#endif

  _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
#if defined(ESP32)
  _udp.printf(response);
#else
  _udp.write(response);
#endif
  _udp.endPacket();

}

void fauxmoESP::_handleUDP() {

  int len = _udp.parsePacket();
  if (len > 0) {

    unsigned char data[len + 1];
    _udp.read(data, len);
    data[len] = 0;

#if DEBUG_FAUXMO_VERBOSE_UDP
    DEBUG_MSG_FAUXMO("[FAUXMO] UDP packet received\r\n%s", (const char *) data);
#endif

    String request = (const char *) data;
    if (request.indexOf("M-SEARCH") >= 0) 
    {
        if (request.indexOf("Windows") > 0)
        {
        }
        else
        if (request.indexOf("upnp:rootdevice") > 0 || request.indexOf("device:basic:1") > 0 || request.indexOf("ssdp:discover") > 0) 
	    {
          _sendUDPResponse();
        }
    }
  }

}


// -----------------------------------------------------------------------------
// TCP
// -----------------------------------------------------------------------------

void fauxmoESP::_sendTCPResponse(AsyncClient *client, const char * code, char * body, const char * mime) {

  char headers[strlen_P(FAUXMO_TCP_HEADERS) + 32];
  snprintf_P(
    headers, sizeof(headers),
    FAUXMO_TCP_HEADERS,
    code, mime, strlen(body)
  );

#if DEBUG_FAUXMO_VERBOSE_TCP
  DEBUG_MSG_FAUXMO("[FAUXMO] Response:\r\n%s%s\r\n", headers, body);
#endif

  client->write(headers);
  client->write(body);

}

String fauxmoESP::_deviceJson_first(unsigned char id) {

  if (id >= _devices.size()) return "{}";

  fauxmoesp_device_t device = _devices[id];
  char buffer[strlen_P(FAUXMO_DEVICE_JSON_TEMPLATE_FIRST) + 64];
  snprintf_P(
    buffer, sizeof(buffer),
    FAUXMO_DEVICE_JSON_TEMPLATE_FIRST,
#ifdef UNIQUE_MACADDRESS
    device.name, macaddress.c_str(), id + 1
#endif
#ifdef SHORT_MACADDRESS
    device.name, macshort.c_str(), id + 1
#endif
#ifdef UNIQUE_MY
    device.name, id + 1
#endif
  );

  return String(buffer);

}


String fauxmoESP::_deviceJson(unsigned char id) {

  if (id >= _devices.size()) return "{}";

  fauxmoesp_device_t device = _devices[id];
  char buffer[strlen_P(FAUXMO_DEVICE_JSON_TEMPLATE) + 64];
  snprintf_P(
    buffer, sizeof(buffer),
    FAUXMO_DEVICE_JSON_TEMPLATE,
#ifdef UNIQUE_MACADDRESS
    device.name, macaddress.c_str(), id + 1,
#endif
#ifdef SHORT_MACADDRESS
    device.name, macshort.c_str(), id + 1,
#endif
#ifdef UNIQUE_MY
    device.name, id + 1,
#endif
    (device.state & 0x01) ? "true" : "false",
    device.value
  );
  return String(buffer);

}

bool fauxmoESP::_onTCPDescription(AsyncClient *client, String url, String body) {

  (void) url;
  (void) body;

  IPAddress ip = WiFi.localIP();

  DEBUG_MSG_FAUXMO("[FAUXMO] Handling /description.xml request\r\n");

  char response[strlen_P(FAUXMO_DESCRIPTION_TEMPLATE) + 64];
  snprintf_P(
    response, sizeof(response),
    FAUXMO_DESCRIPTION_TEMPLATE,
    ip[0], ip[1], ip[2], ip[3], _tcp_port,
    ip[0], ip[1], ip[2], ip[3], _tcp_port,
    macaddress.c_str(), macaddress.c_str()
  );

  _sendTCPResponse(client, "200 OK", response, "text/xml");

  return true;

}

bool fauxmoESP::_onTCPList(AsyncClient *client, String url, String body) {

  DEBUG_MSG_FAUXMO("[FAUXMO] Handling list request\r\n");

  // Get the index
  int pos = url.indexOf("lights");
  if (-1 == pos) return false;

  // Get the id
  unsigned char id = url.substring(pos + 7).toInt();

  // This will hold the response string
  String response;

  unsigned char first = 0;
  // Client is requesting all devices - limit is bufsize (2920 bytes)
  if (0 == id)
  {
    DEBUG_MSG_FAUXMO("[FAUXMO] Handling ALL DEVICES list <=========================\r\n");
    //      Serial.print("[FAUXMO] Handling ALL DEVICES list <=========================\r\n");
    if (iDevice >= _devices.size()) iDevice = 0;
    if (iDevice < _devices.size())
    {
      response += "{";
//      while ((response.length() < HUE_TCP_PACKETSIZE) && (iDevice < _devices.size()))
      while ((response.length() < HUE_TCP_PACKETSIZE) && (iDevice < _devices.size()))
      {
        if (first++ > 0) response += ",";
#ifdef SHORT_DECLARE
        response += "\"" + String(iDevice + 1) + "\":" + _deviceJson_first(iDevice); // reduced  string
#else
        response += "\"" + String(iDevice + 1) + "\":" + _deviceJson(iDevice);  // original string
#endif
        iDevice++;
        iDiscovered++;
      }
      response += "}";
      //        Serial.printf("[FAUXMO] Response:\r\n%s\r\n", response.c_str());
      _sendTCPResponse(client, "200 OK", (char *) response.c_str(), "application/json");
    }
    //	  else
    //       Serial.printf("[FAUXMO] -> no response\r\n");
    // Client is requesting a single device
  } else {

    response = _deviceJson(id - 1);
    iqueryed++;

#ifdef MY_DEBUG_FAUXMO         
    //    Serial.print("[FAUXMO] Handling single device\r\n");
    if (id == 2)
    {
      Serial.printf("\r\ntcp require 0x11 status: %02X value: %02X\r\n",_devices[id-1].state,_devices[id-1].value);
      Serial.println(response);
    }
    //    Serial.printf("[FAUXMO] Response:\r\n%s\r\n", response.c_str());
#endif

    _sendTCPResponse(client, "200 OK", (char *) response.c_str(), "application/json");

    _devices[id-1].state &= 0x8F; // cover da 0xC0 a 0x80
    if ((_devices[id-1].state & 0x80) == 0x80)
    {
       _devices[id-1].state = 1; // cover 
       _devices[id-1].value = 128; // cover 
    }
  }
  return true;
}

unsigned char fauxmoESP::discovered(void)
{
  return iDiscovered;
}
unsigned int fauxmoESP::queryed(void)
{
  return iqueryed;
}

unsigned char fauxmoESP::devices(void)
{
  return iDevice;
}

bool fauxmoESP::_onTCPControl(AsyncClient *client, String url, String body) {

  // "devicetype" request
  if (body.indexOf("devicetype") > 0) {
    DEBUG_MSG_FAUXMO("[FAUXMO] Handling devicetype request\r\n");
    _sendTCPResponse(client, "200 OK", (char *) "[{\"success\":{\"username\": \"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]", "application/json");
    return true;
  }

#ifdef MY_DEBUG_FAUXMO         
  Serial.println("tcp req: ");
  Serial.print(url + "  ");
  Serial.print(body);
#endif
 
  // "state" request
  if ((url.indexOf("state") > 0) && (body.length() > 0)) {

    // Get the index
    int pos = url.indexOf("lights");
    if (-1 == pos) return false;

    DEBUG_MSG_FAUXMO("[FAUXMO] Handling state request\r\n");

    // Get the index
    unsigned char id = url.substring(pos + 7).toInt();
    if (id > 0) {

      --id;

      char _command = 0;
      unsigned char value = _devices[id].value;

      //// old // State is a bool (ON/OFF) and value a number from 0 to 255 (if you say "set kitchen light to 50%" you will receive a 128 here).
      // command is a char (1=ON 2=OFF    3=bright equal  4=bright+   5=bright- )

      // Brightness
      pos = body.indexOf("bri");
      if (pos > 0) {
        value = body.substring(pos + 5).toInt();
        if (value > _devices[id].value)
          _command = 4;
        else if (value < _devices[id].value)
          _command = 5;
        else
          _command = 3;
        _devices[id].value = value; // <-----------------------------------------------
      }
      else if (body.indexOf("false") > 0)
      {
        _command = 2;
        _devices[id].state &= 0xFE;		// <-----------------------------------------------
      }
      else if (body.indexOf("true") > 0)
      {
        _command = 1;
        _devices[id].state |= 0x01;		// <-----------------------------------------------
        if (0 == _devices[id].value) _devices[id].value = 255;
      }

/*
      char response[strlen_P(FAUXMO_TCP_STATE_RESPONSE) + 10];
      snprintf_P(
        response, sizeof(response),
        FAUXMO_TCP_STATE_RESPONSE,
        id + 1, (_devices[id].state & 0x01)? "true" : "false", id + 1, _devices[id].value
      );
*/

      char response[strlen_P(FAUXMO_TCP_VALUE_RESPONSE) + 10];
      if (_command < 3) // on-off      
      {
        snprintf_P(
          response, sizeof(response),
          FAUXMO_TCP_STATE_RESPONSE,
          id + 1, (_devices[id].state & 0x01)? "true" : "false"
        );
      }
      else
      {
        snprintf_P(
          response, sizeof(response),
          FAUXMO_TCP_VALUE_RESPONSE,
          id + 1, _devices[id].value
        );
      }
      
#ifdef MY_DEBUG_FAUXMO         
      Serial.printf("\r\n  response: %s", response);
#endif

      _sendTCPResponse(client, "200 OK", response, "text/xml");

      if (_setCallback)
      {
#ifdef DEBUG_FAUXMO_TCP         
        _setCallback(id, _devices[id].name, _command, value, (char*) &body[0], (char*) &response[0]);
#else
		_setCallback(id, _devices[id].name, _command, value);
#endif
      }

      return true;

    }

  }

  return false;

}

bool fauxmoESP::_onTCPRequest(AsyncClient *client, bool isGet, String url, String body) {

  if (!_enabled) return false;

#if DEBUG_FAUXMO_VERBOSE_TCP
  DEBUG_MSG_FAUXMO("[FAUXMO] isGet: %s\r\n", isGet ? "true" : "false");
  DEBUG_MSG_FAUXMO("[FAUXMO] URL: %s\r\n", url.c_str());
  if (!isGet) DEBUG_MSG_FAUXMO("[FAUXMO] Body:\r\n%s\r\n", body.c_str());
#endif

  if (url.equals("/description.xml")) {
    return _onTCPDescription(client, url, body);
  }

  if (url.startsWith("/api")) {
    if (isGet) {
      return _onTCPList(client, url, body);
    } else {
      return _onTCPControl(client, url, body);
    }
  }

  return false;

}

bool fauxmoESP::_onTCPData(AsyncClient *client, void *data, size_t len) {

  if (!_enabled) return false;

  char * p = (char *) data;
  p[len] = 0;

#if DEBUG_FAUXMO_VERBOSE_TCP
  DEBUG_MSG_FAUXMO("[FAUXMO] TCP request\r\n%s\r\n", p);
#endif

  // Method is the first word of the request
  char * method = p;

  while (*p != ' ') p++;
  *p = 0;
  p++;

  // Split word and flag start of url
  char * url = p;

  // Find next space
  while (*p != ' ') p++;
  *p = 0;
  p++;

  // Find double line feed
  unsigned char c = 0;
  while ((*p != 0) && (c < 2)) {
    if (*p != '\r') {
      c = (*p == '\n') ? c + 1 : 0;
    }
    p++;
  }
  char * body = p;

  bool isGet = (strncmp(method, "GET", 3) == 0);

  return _onTCPRequest(client, isGet, url, body);

}

void fauxmoESP::_onTCPClient(AsyncClient *client) {

  if (_enabled) {

    for (unsigned char i = 0; i < FAUXMO_TCP_MAX_CLIENTS; i++) {

      if (!_tcpClients[i] || !_tcpClients[i]->connected()) {

        _tcpClients[i] = client;

        client->onAck([i](void *s, AsyncClient * c, size_t len, uint32_t time) {
        }, 0);

        client->onData([this, i](void *s, AsyncClient * c, void *data, size_t len) {
          _onTCPData(c, data, len);
        }, 0);

        client->onDisconnect([this, i](void *s, AsyncClient * c) {
          _tcpClients[i]->free();
          _tcpClients[i] = NULL;
          delete c;
          DEBUG_MSG_FAUXMO("[FAUXMO] Client #%d disconnected\r\n", i);
        }, 0);

        client->onError([i](void *s, AsyncClient * c, int8_t error) {
          DEBUG_MSG_FAUXMO("[FAUXMO] Error %s (%d) on client #%d\r\n", c->errorToString(error), error, i);
        }, 0);

        client->onTimeout([i](void *s, AsyncClient * c, uint32_t time) {
          DEBUG_MSG_FAUXMO("[FAUXMO] Timeout on client #%d at %i\r\n", i, time);
          c->close();
        }, 0);

        client->setRxTimeout(FAUXMO_RX_TIMEOUT);

        DEBUG_MSG_FAUXMO("[FAUXMO] Client #%d connected\r\n", i);
        return;

      }

    }

    DEBUG_MSG_FAUXMO("[FAUXMO] Rejecting - Too many connections\r\n");

  } else {
    DEBUG_MSG_FAUXMO("[FAUXMO] Rejecting - Disabled\r\n");
  }

  client->onDisconnect([](void *s, AsyncClient * c) {
    c->free();
    delete c;
  });
  client->close(true);

}

// -----------------------------------------------------------------------------
// Devices
// -----------------------------------------------------------------------------

fauxmoESP::~fauxmoESP() {

  // Free the name for each device
  for (auto& device : _devices) {
    free(device.name);
  }

  // Delete devices
  _devices.clear();

}

unsigned char fauxmoESP::addDevice(const char * device_name) {
  return addDevice(device_name, 128);
}

unsigned char fauxmoESP::addDevice(const char * device_name, unsigned char initvalue) {

  fauxmoesp_device_t device;
  unsigned int device_id = _devices.size();

  // init properties
  device.name = strdup(device_name);
  device.state = 0;
  device.value = initvalue;

  // Attach
  _devices.push_back(device);

  DEBUG_MSG_FAUXMO("[FAUXMO] Device '%s' added as #%d\r\n", device_name, device_id);

  return device_id;

}

int fauxmoESP::getDeviceId(const char * device_name) {
  for (unsigned int id = 0; id < _devices.size(); id++) {
    if (strcmp(_devices[id].name, device_name) == 0) {
      return id;
    }
  }
  return -1;
}

bool fauxmoESP::renameDevice(unsigned char id, const char * device_name) {
  if (id < _devices.size()) {
    free(_devices[id].name);
    _devices[id].name = strdup(device_name);
    DEBUG_MSG_FAUXMO("[FAUXMO] Device #%d renamed to '%s'\r\n", id, device_name);
    return true;
  }
  return false;
}

bool fauxmoESP::renameDevice(const char * old_device_name, const char * new_device_name) {
  int id = getDeviceId(old_device_name);
  if (id < 0) return false;
  return renameDevice(id, new_device_name);
}

bool fauxmoESP::removeDevice(unsigned char id) {
  if (id < _devices.size()) {
    free(_devices[id].name);
    _devices.erase(_devices.begin() + id);
    DEBUG_MSG_FAUXMO("[FAUXMO] Device #%d removed\r\n", id);
    return true;
  }
  return false;
}

bool fauxmoESP::removeDevice(const char * device_name) {
  int id = getDeviceId(device_name);
  if (id < 0) return false;
  return removeDevice(id);
}

char * fauxmoESP::getDeviceName(unsigned char id, char * device_name, size_t len) {
  if ((id < _devices.size()) && (device_name != NULL)) {
    strncpy(device_name, _devices[id].name, len);
  }
  return device_name;
}

char fauxmoESP::getState(unsigned char id) {
  if (id < _devices.size()) {
    return _devices[id].state;
  }
  return 0;
}

bool fauxmoESP::setState(unsigned char id, char state, unsigned char value) {
  if (id < _devices.size()) {
    if (state != 0xFF) _devices[id].state = state;
    if (value)         _devices[id].value = value;
    return true;
  }
  return false;
}

bool fauxmoESP::setState(const char * device_name, char state, unsigned char value) {
  int id = getDeviceId(device_name);
  if (id < 0) return false;
  _devices[id].state = state;
  _devices[id].value = value;
  return true;
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool fauxmoESP::process(AsyncClient *client, bool isGet, String url, String body) {
  return _onTCPRequest(client, isGet, url, body);
}

void fauxmoESP::handle() {
  if (_enabled) _handleUDP();
}


void fauxmoESP::enable(bool enable) {

  macaddress = WiFi.macAddress();	//01:34:67:90:23:56
  macaddress.toLowerCase();
  macshort = macaddress.substring(9);
  macaddress.replace(":", "");

  if (enable == _enabled) return;
  _enabled = enable;
  if (_enabled) {
    DEBUG_MSG_FAUXMO("[FAUXMO] Enabled\r\n");
  } else {
    DEBUG_MSG_FAUXMO("[FAUXMO] Disabled\r\n");
  }

  if (_enabled) {

    // Start TCP server if internal
    if (_internal) {
      if (NULL == _server) {
        _server = new AsyncServer(_tcp_port);
        _server->onClient([this](void *s, AsyncClient * c) {
          _onTCPClient(c);
        }, 0);
      }
      _server->begin();
    }

    // UDP setup
#ifdef ESP32
    _udp.beginMulticast(FAUXMO_UDP_MULTICAST_IP, FAUXMO_UDP_MULTICAST_PORT);
#else
    _udp.beginMulticast(WiFi.localIP(), FAUXMO_UDP_MULTICAST_IP, FAUXMO_UDP_MULTICAST_PORT);
#endif
    DEBUG_MSG_FAUXMO("[FAUXMO] UDP server started\r\n");

  }

}
