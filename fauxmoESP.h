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

#pragma once

#define FAUXMO_UDP_MULTICAST_IP     IPAddress(239,255,255,250)
#define FAUXMO_UDP_MULTICAST_PORT   1900
#define FAUXMO_TCP_MAX_CLIENTS      10
#define FAUXMO_TCP_PORT             80
#define FAUXMO_RX_TIMEOUT           3
//#define HUE_TCP_PACKETSIZE         870 // 536x2=1072-92(json) -> 972 - 90(header) -> 880
#define HUE_TCP_PACKETSIZE        2048 // 1460x2=2920-200=2720

//#define DEBUG_FAUXMO_TCP         

//#define MY_DEBUG_FAUXMO         
//#define DEBUG_FAUXMO                Serial
//#define DEBUG_FAUXMO_VERBOSE_TCP    true
// #define DEBUG_FAUXMO_VERBOSE_UDP    true


#ifdef DEBUG_FAUXMO
    #if defined(ARDUINO_ARCH_ESP32)
        #define DEBUG_MSG_FAUXMO(fmt, ...) { DEBUG_FAUXMO.printf_P((PGM_P) PSTR(fmt), ## __VA_ARGS__); }
    #else
        #define DEBUG_MSG_FAUXMO(fmt, ...) { DEBUG_FAUXMO.printf(fmt, ## __VA_ARGS__); }
    #endif
#else
    #define DEBUG_MSG_FAUXMO(...)
#endif

#ifndef DEBUG_FAUXMO_VERBOSE_TCP
#define DEBUG_FAUXMO_VERBOSE_TCP    false
#endif

#ifndef DEBUG_FAUXMO_VERBOSE_UDP
#define DEBUG_FAUXMO_VERBOSE_UDP    false
#endif

#include <Arduino.h>

#if defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <ESPAsyncTCP.h>
#elif defined(ESP32)
    #include <WiFi.h>
    #include <AsyncTCP.h>
#else
	#error Platform not supported
#endif

// #include <ESPAsyncWebServer.h>

#include <WiFiUdp.h>
#include <functional>
#include <vector>
#include "templates.h"

#ifdef DEBUG_FAUXMO_TCP         
typedef std::function<void(unsigned char, const char *, char, unsigned char, const char *, const char *)> TSetStateCallback;
#else
typedef std::function<void(unsigned char, const char *, char, unsigned char)> TSetStateCallback;
#endif

typedef struct {
    char * name;
    char state;
    unsigned char value;
} fauxmoesp_device_t;

class fauxmoESP {

    public:

        ~fauxmoESP();

        unsigned char addDevice(const char * device_name);
        unsigned char addDevice(const char * device_name, unsigned char initvalue);
        bool renameDevice(unsigned char id, const char * device_name);
        bool renameDevice(const char * old_device_name, const char * new_device_name);
        bool removeDevice(unsigned char id);
        bool removeDevice(const char * device_name);
        char * getDeviceName(unsigned char id, char * buffer, size_t len);
        int getDeviceId(const char * device_name);
        void onSetState(TSetStateCallback fn) { _setCallback = fn; }
        char getState(unsigned char id);
        bool setState(unsigned char id, char state, unsigned char value);
        bool setState(const char * device_name, char state, unsigned char value);
        bool process(AsyncClient *client, bool isGet, String url, String body);
        void enable(bool enable);
        void createServer(bool internal) { _internal = internal; }
        void setPort(unsigned long tcp_port) { _tcp_port = tcp_port; }
        void handle();
        unsigned char discovered(void); 
        unsigned int queryed(void); 
        unsigned char devices(void); 

    private:

        AsyncServer * _server;
        bool _enabled = false;
        bool _internal = true;
        unsigned int _tcp_port = FAUXMO_TCP_PORT;
        std::vector<fauxmoesp_device_t> _devices;
		#ifdef ESP8266
        WiFiEventHandler _handler;
		#endif
        WiFiUDP _udp;
        AsyncClient * _tcpClients[FAUXMO_TCP_MAX_CLIENTS];
        TSetStateCallback _setCallback = NULL;

        String _deviceJson(unsigned char id);
        String _deviceJson_first(unsigned char id);

        void _handleUDP();
        void _onUDPData(const IPAddress remoteIP, unsigned int remotePort, void *data, size_t len);
        void _sendUDPResponse();

        void _onTCPClient(AsyncClient *client);
        bool _onTCPData(AsyncClient *client, void *data, size_t len);
        bool _onTCPRequest(AsyncClient *client, bool isGet, String url, String body);
        bool _onTCPDescription(AsyncClient *client, String url, String body);
        bool _onTCPList(AsyncClient *client, String url, String body);
        bool _onTCPControl(AsyncClient *client, String url, String body);
        void _sendTCPResponse(AsyncClient *client, const char * code, char * body, const char * mime);

};
