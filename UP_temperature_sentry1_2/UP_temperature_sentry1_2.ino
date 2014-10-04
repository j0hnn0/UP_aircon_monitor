/**********************************************************************************************************************************************************

 johann.maxen@gmail.com - September 2014                                                                                                                
                                                                                                                                                        
 This is Arduino (Ethernet model Uno R3) firmware code to poll a Dallas Semiconductor DS18B20 temperature probe, then serve data over ethernet via HTTP 
 to be read by a Nagios plugin or other scripts. A simple HTTP GET request returns the temperature                                                      
                                                                                                                                                        
 (See <EOF> for I/O spec and notes)                                                                                                                               

***********************************************************************************************************************************************************/

#include <SPI.h>            // Required for Wiznet ethernet chip
#include <Ethernet.h>       // Required for ethernet library
#include <OneWire.h>        // Required for Dallas DS18B20 temp sensor one-wire protocol
#include <EEPROM.h>         // Required for writing and reading from EEPROM
#include <SoftwareSerial.h> // Required for writing serial data to 7 seg
#include <avr/wdt.h>        // Atmel AVR controller's watchdog timer lib

#define SENSOR_PIN 3        // Arduino pin for the temp sensor data line
#define SEGBRIGHT 160       // 7 seg brightness 0-255, determines the segment multiplexing interval
#define CONVERSIONDELAY 800 // Time (ms) to wait for temp conversion to complete, no less than 750 ms
#define HEARTBEAT 1233      // System heartbeat - can be used to drive independent timer interrupt 
#define dSHORT 20           // Short delay in ms
#define dMEDIUM 50          // Medium delay in ms
#define dLONG 75            // Long delay in ms
#define BAUD 9600           // Baud rate for the 7 seg display
#define RADIX 0b00100010    // Radix indicators for the 7 seg
#define sysLED 9            // Small green SMD system status LED
#define auxsysLED 2         // Small red SMD secondary system status LED 
#define rLED 5              // Alarm, flashing for warning, solid for critical
#define gLED 6              // Temperature sensor activity
#define yLED 7              // Ethernet activity
#define tMIN 0              // Over and underflow conditions for display
#define tMAX 99.9
#define SERIALTX 8          // 7 Segment serial TX
#define SERIALRX 7          // 7 Segment serial RX (Does not really exist, but Lib wants it)
#define IP_PORT 61033       // TCP listener port for HTTP server
#define MAC_ADDRESS {0x90, 0xA2, 0xDA, 0x0D, 0x58, 0x86} // MAC for the Wiznet device
#define WARNTEMP 28         // Default alarm conditions
#define CRITTEMP 30

unsigned long previousMillis = 0;
unsigned long syspreviousMillis = 0;
unsigned long sensorpreviousMillis = 0;
unsigned long currentMillisSensor = 0;
unsigned long rLEDpreviousMillis = 0;
unsigned long yLEDpreviousMillis = 0;

byte sensor_addr[8];
byte mac[] = MAC_ADDRESS;  
byte gLEDState = LOW;
byte rLEDState = LOW;
byte yLEDState = LOW;

unsigned char conversionFlag = 0;
unsigned char z;
float celsius;
int warnTemp = WARNTEMP;
int critTemp = CRITTEMP;

SoftwareSerial s7s(SERIALRX, SERIALTX);      // 7 segment object instance
OneWire ds(SENSOR_PIN);                      // DS18B20 object instance
EthernetServer server(IP_PORT);              // Wiznet Ethernet server object


void setup() {

    pinMode(sysLED, OUTPUT);                 // Init digital I/O
    pinMode(yLED, OUTPUT);  
    pinMode(gLED, OUTPUT);
    pinMode(rLED, OUTPUT);
    ledsOff();  
    ledOn(auxsysLED);

    s7s.begin(BAUD);                         // Setup the 7s display
    clearDisplay();
    setBrightness(SEGBRIGHT);
    clearDisplay();

    for(z=0;z<10;z++) {
      s7s.print("Init");
      delay(100);
      clearDisplay();
      delay(100);
    }  

    s7s.print("Alr");                        // Test alarm  
    delay(500);
    for(z=0;z<10;z++) {
      ledOn(rLED);
      delay(75);
      ledOff(rLED);
      delay(75);
    }
    clearDisplay();  

    s7s.print("dHCp");
    delay(3000);
    while(!Ethernet.begin(mac));             // Init the ethernet device and start the server - DHCP request when no extra parameters are supplied
   
    server.begin();
    clearDisplay();

    s7s.print("SEnS");
    delay(3000);
    while(!ds.search(sensor_addr));         // Search for temp sensor on the OneWire bus and get its hex address 
    
    ledOff(auxsysLED);
    ledOn(yLED);                            // Leave YELLOW on, only successful HTTP request can reset this, so if it stays on after reset, network's not OK

    wdt_enable(WDTO_8S);                    // Setup the watchdog timer for 8s timeout  
    ledOn(sysLED);  
}


void loop() {
    
    wdt_reset();                            // Keep the watchdog happy
    readTemp();                             // Get the temperature 
    displayTemp();                          // Update the 7 segment 
    listenEthernet();                       // Listen for incoming Ethernet connections:
    checkAlarms();                          // Process alarm conditions

}


void listenEthernet() {

    // Listen for incoming clients
    EthernetClient client = server.available();
    if (client) {          // Got a client request
      ledOn(yLED); 
      // HTTP request ends with a blank line
      boolean currentLineIsBlank = true;
      while (client.connected()) {
        if (client.available()) {
          char c = client.read();
          // If you've gotten to the end of the line (received a newline
          // character) and the line is blank, the HTTP request has ended,
          // so you can send a reply
          if (c == '\n' && currentLineIsBlank) {
            // Send a standard HTTP response header
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: text/html");
            client.println();
            // Print the current readings, in HTML format:
            client.print("T:");
            client.print(celsius);
            break;
          }
          if (c == '\n') {
            // Starting a new line
            currentLineIsBlank = true;
          } 
          else if (c != '\r') {
            // You've got a character on the current line
            currentLineIsBlank = false;
          }
        }
      }
      // Give the web browser time to receive the data
      delay(5);
      // Close the connection:
      client.stop();
      ledOff(yLED);   
    }

}


void readTemp() {
  
    /* Reads the DS18B20 temperature sensor, writes data into the global temperature var */
    
    if(conversionFlag == 0) {              // No conversion in progress, so initiate a new one
      toggleLEDg();
      conversionFlag = 1;
      ds.reset();
      ds.select(sensor_addr);
      ds.write(0x44, 1);                   // Request conversion, with parasite power on at the end
      return;
    }
    
    currentMillisSensor = millis();             
    if((currentMillisSensor - sensorpreviousMillis > CONVERSIONDELAY) && conversionFlag == 1) {
      
      // If we get here, the conversion delay (minimum 750 ms for 12 bit conversion) has expired, and a conversion has already been requested, so fetch the data and reset the flags
      byte type_s = 0;
      byte temp_data[12];
      byte present = 0;
      byte i;
    
      present = ds.reset();
      ds.select(sensor_addr);    
      ds.write(0xBE);                      // Read scratchpad
      for ( i = 0; i < 9; i++) {           // we need 9 bytes
        temp_data[i] = ds.read();
      }
    
      // Convert the data to actual temperature
      // Because the result is a 16 bit signed integer, it should
      // be stored to an "int16_t" type, which is always 16 bits
      // even when compiled on a 32 bit processor.
      
      int16_t raw = (temp_data[1] << 8) | temp_data[0];
      if (type_s) {
        raw = raw << 3;                    // 9 bit resolution default
        if (temp_data[7] == 0x10) {        // Full 12 bit resolution
          raw = (raw & 0xFFF0) + 12 - temp_data[6];
        }
      } else {
        byte cfg = (temp_data[4] & 0x60);
        
        // At lower res, the low bits are undefined, so let's zero them
        // NB: default is 12 bit resolution, 750 ms conversion time
        
        if (cfg == 0x00) raw = raw & ~7;   // 9 bit resolution, 93.75 ms
        else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
        else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
      }
      
      celsius = (float)raw / 16.0;
      
      conversionFlag = 0;                  // Conversion complete - reset the flag and timer
      sensorpreviousMillis = currentMillisSensor;                                       
    }

}


void displayTemp() {
  
    /* Displays the latest temp reading on the 7 seg */
    
    double t = 0;
    char tempString[10];                         // sprintf buffer for constructing temperature output  
     
    t = round((double)(celsius*10));
    sprintf(tempString, "%3d%c",(int)t,0x5b);    // 0x5B for the 'C' indicator char
    
    if(t < tMIN*10 || t >= tMAX*10)              // Over- or underflow  
      sprintf(tempString, "8888");               
    
    s7s.print(tempString);
    setRadix(RADIX);                             // Enable radix 2 (bit 2) and Celsius apostrophe (bit 6)
   
}  


void checkAlarms() {                             

  /* Red LED shares the driver's open collector with the buzzer */
  
    if(celsius >= warnTemp && celsius < critTemp)
       toggleLEDr();                             
    if(celsius >= critTemp)
       ledOn(rLED);                              
    if(celsius < warnTemp)
       ledOff(rLED);  

}  
 

void led(int tLED, unsigned char i, unsigned char uTime, unsigned char dTime) {

    /* Flashes tLED i times on I/O port */
    
    unsigned char j;
    
    for(j=0;j<i;j++) {
      digitalWrite(tLED, HIGH);  // turn the LED on
      delay(uTime);              // wait a little ON
      digitalWrite(tLED, LOW);   // turn the LED off by pulling the line LOW
      delay(dTime);              // wait a little OFF 
    }

}  


void clearDisplay() {

    /* Send the clear display command (0x76). Homes the cursor also */
    
    s7s.write(0x76);  

}


void setBrightness(byte value) {

    /* Set the displays brightness. 0-255 */
    
    s7s.write(0x7A);  
    s7s.write(value); 

}


void setRadix(byte decimals) {

    /* Turn on any, none, or all of the 7 segment radix indicators.
       The six lowest bits in the decimals parameter sets a decimal 
       (or colon, or apostrophe) on or off. 
       [MSB] (X)(X)(Apos)(Colon)(Digit 4)(Digit 3)(Digit2)(Digit1) */
    
    s7s.write(0x77);
    s7s.write(decimals);

}


void ledsOff() {
  
    digitalWrite(sysLED,LOW);
    digitalWrite(rLED,LOW);
    digitalWrite(gLED,LOW);
    digitalWrite(yLED,LOW);  
    digitalWrite(sysLED,LOW);
    digitalWrite(auxsysLED,LOW);
  
}


void ledOn(byte led) {
  
    digitalWrite(led,HIGH);  
  
}


void ledOff(byte led) {
  
    digitalWrite(led,LOW); 
  
}


void toggleLEDg() {

    if (gLEDState == LOW)
        gLEDState = HIGH;
    else
        gLEDState = LOW;
    digitalWrite(gLED,gLEDState);

}  


void toggleLEDr() {
  
    unsigned long currentMillis = millis();
     
    if(currentMillis - rLEDpreviousMillis > dLONG) {
      rLEDpreviousMillis = currentMillis;   
    
      if (rLEDState == LOW)
        rLEDState = HIGH;
      else
        rLEDState = LOW;
    
      // set the LED with the ledState of the variable:
      digitalWrite(rLED, rLEDState);
    } 
  
}  


void toggleLEDy() {
  
    unsigned long currentMillis = millis();
     
    if(currentMillis - yLEDpreviousMillis > dMEDIUM) {
      yLEDpreviousMillis = currentMillis;   
    
      if (yLEDState == LOW)
        yLEDState = HIGH;
      else
        yLEDState = LOW;
    
      // set the LED with the ledState of the variable:
      digitalWrite(yLED, yLEDState);
    } 

}  


double round(double x) {

    /* BSD Unix implementation of round() instead of linking in the whole math.h lib just for one calc */
    
    double t;
    if (!isfinite(x)) return (x);
    if (x >= 0.0) {
      t = floor(x);
      if (t - x <= -0.5) t += 1.0;
        return (t);
    } else {
        t = floor(-x);
        if (t + x <= -0.5) t += 1.0;
          return (-t);
      }
    
}

 
/************************************************************
   I/O:
 
     5V   ----------------  VCC
     GND  ----------------  GND
      9   ----------------  System LED (SMD green)
      8   ----------------  7-Segment RX
      7   ----------------  Yellow LED
      6   ----------------  Green LED
      5   ----------------  Red LED/Buzzer 
      3   ----------------  Temp sensor input
      2   ----------------  Aux system LED (SMD red)

*************************************************************/



