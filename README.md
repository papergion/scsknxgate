# scsknxgate
esp8266 communication process for scsgate and knxgate

scsknxgate is the wifi communication process for devices esp_scsgate (scs protocol) and esp_knxgate (knx protocol).
communication between scsknxgate and esp_xxx devices is made between UART, speed is 115200 8 N 1

scsknxgate implement wifi communication in UDP - TCP - HTTP - MQTT.
also implement direct communication with Alexa devices using a modified library of fauxmoESP.

folder "BINARY" contains exportes bin files of compiled firmware. naming convention is "knxgate_<version>.ino.generic.bin" for KNX compiled versions,  "scsgate_<version>.ino.generic.bin" for SCS compiled versions.   .bat files contains demo of load an exported bin file in esp8266 using OTA.  Loading last version of bin firmware may also require to load last version on PIC firmware.
  
folder "PICIMAGES" contain last version of PIC firmware for knx (picknxgate.img) and for scs (picscsgate.img) - format is "spiffs format" suitable for load using "espota" as in demo .bat files.  After loading in spiffs, firmware can be updated using /picprog command like in user manual. Please don't use this updating method with old ESP firmwares.
subfolder "OLD" contain historycal version of PIC firmware for knx and scs.

for more info see: http://guidopic.altervista.org/alter/index.html
                   http://guidopic.altervista.org/alter/knxgate.html
                   http://guidopic.altervista.org/alter/eibscsgt.html
