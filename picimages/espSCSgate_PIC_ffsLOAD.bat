echo off

set ipaddress=192.168.2.84
set IMG="H:\Arduino\ESP8266\domotic\scsknxgate_v5\picimages\picscsgate.img"

echo ipaddress %ipaddress%

if EXIST %IMG% GOTO AVANTI
echo FILE NOT EXISTS
GOTO FINE

:AVANTI
echo - invia immagine spiffs da file hex di espknxgate

pause conferma
cd \Arduino\ESP8266\espota
espota.py -i %ipaddress% -f %IMG% -s -r

:FINE
pause