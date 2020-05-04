h:
echo off

set ipaddress=192.168.2.34
set versione=50617
set folder=H:\Arduino\ESP8266\domotic\scsknxgate_v5\binary

echo ________________________SCS______________________________

echo ipaddress %ipaddress%
echo version %versione%
if EXIST %folder%\scsgate_v%versione%.ino.generic.bin GOTO AVANTI
echo FILE NOT EXISTS
GOTO FINE

:AVANTI
pause
cd \Arduino\ESP8266\espota
espota.py -i %ipaddress% -f %folder%\scsgate_v%versione%.ino.generic.bin -r

:FINE
pause