# scsknxgate
esp8266 communication process for scsgate and knxgate

scsknxgate is the wifi communication process for devices esp_scsgate (scs protocol) and esp_knxgate (knx protocol).
communication between scsknxgate and esp_xxx devices is made between UART, speed is 115200 8 N 1

scsknxgate implement wifi communication in UDP - TCP - HTTP - MQTT.
also implement direct communication with Alexa devices using a modified library of fauxmoESP.
