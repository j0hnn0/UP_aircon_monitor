UP_aircon_monitor
=================
This is an Arduino/ATMEGA328 project to monitor our server and UPS rooms' temperatures with Dallas18B20 temperature sensors. The code also includes a HTTP server which is queried periodically by a python script on a RHEL Linux server running Nagios for alerts and graphing.

If codespace allows, a Telnet server will be included to update default parameters on the Arduino's EEPROM.

