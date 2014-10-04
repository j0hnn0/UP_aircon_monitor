/*
  johann.maxen@gmail.com, September 2014
  
  Firmware code for Telnet Server on Arduino Uno Ethernet V3

*/

// Ethernet parameters
#include <SPI.h>
#include <Ethernet.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

byte mac[] =     { 0x90, 0xA2, 0xDA, 0x0D, 0x58, 0x87 };

int critTemp = 30;
int warnTemp = 28;


// Other global variables
#define textBuffSize 9 //length of longest command string plus two spaces for CR + LF
char textBuff[textBuffSize]; //someplace to put received text
int charsReceived = 0;

boolean connectFlag = 0; //we'll use a flag separate from client.connected
                         //so we can recognize when a new connection has been created
unsigned long timeOfLastActivity; //time in milliseconds of last activity
unsigned long allowedConnectTime = 180000; //3 minutes

EthernetServer telnet = EthernetServer(23); // Telnet listens on port 23
EthernetClient client = 0; // Client needs to have global scope so it can be called 
                   // from functions outside of loop, but we don't know
                   // what client is yet, so creating an empty object

void setup()
{
  getDefaults();
  Serial.begin(9600);
  Ethernet.begin(mac);
  telnet.begin(); 
}

void loop()
{
  // look to see if a new connection is created,
  // print welcome message, set connected flag
  if (telnet.available() && !connectFlag) {
    connectFlag = 1;
    client = telnet.available();
    client.println("\nAircon Sentry Telnet Server");
    client.println("\nTimeout is 3 minutes");
    client.println("\n? for help");
    
    printPrompt();
  }
  
  // check to see if text received
  if (client.connected() && client.available()) getReceivedText();
     
  // check to see if connection has timed out
  if(connectFlag) checkConnectionTimeout();

}


void getDefaults() {
  
  critTemp = EEPROM.read(0);
  warnTemp = EEPROM.read(1);
  
}  


void printPrompt()
{
  timeOfLastActivity = millis();
  client.flush();
  charsReceived = 0; //count of characters received
  client.print("\n>");
}


void checkConnectionTimeout()
{
  if(millis() - timeOfLastActivity > allowedConnectTime) {
    client.println();
    client.println("Timeout disconnect.");
    client.stop();
    connectFlag = 0;
  }
}


void getReceivedText()
{
  char c;
  int charsWaiting;

  // copy waiting characters into textBuff
  //until textBuff full, CR received, or no more characters
  charsWaiting = client.available();
  do {
    c = client.read();
    textBuff[charsReceived] = c;
    charsReceived++;
    charsWaiting--;
  }
  while(charsReceived <= textBuffSize && c != 0x0d && charsWaiting > 0);
  
  //if CR found go look at received text and execute command
  if(c == 0x0d) {
    parseReceivedText();
    // after completing command, print a new prompt
    printPrompt();
  }
  
  // if textBuff full without reaching a CR, print an error message
  if(charsReceived >= textBuffSize) {
    client.println();
    printErrorMessage();
    printPrompt();
  }
  // if textBuff not full and no CR, do nothing else;  
  // go back to loop until more characters are received
  
}   


void parseReceivedText()
{
  // look at first character and decide what to do
  switch (textBuff[0]) {
    case 'c' : setTemp();                break;
    case 'w' : setTemp();                break;
    case 'd' : displayCurrent();         break;
    case 'q' : checkCloseConnection();   break;
    case '?' : printHelpMessage();       break;
    case 0x0d :                          break;  //ignore a carriage return
    default: printErrorMessage();        break;
  }
 }


void setTemp() {

// If we got here textBuff[0] is 'c' or 'w', so get the next digits until <cr> (0x0d)  
  
  int tempSetting = 0;
  int textPosition = 1;  //start at textBuff[1]
  int digit;
  do {
    digit = parseDigit(textBuff[textPosition]); //look for a digit in textBuff
    if (digit >= 0 && digit <=9) {              //if digit found
      tempSetting = tempSetting * 10 + digit;     //shift previous result and add new digit
    }
    else tempSetting = -1;
    textPosition++;                             //go to the next position in textBuff
  }
  //if not at end of textBuff and not found a CR and not had an error, keep going
  while(textPosition < 5 && textBuff[textPosition] != 0x0d && tempSetting > -1);
   //if value is not followed by a CR, return an error
  if(textBuff[textPosition] != 0x0d) tempSetting = -1;   
  if(tempSetting <= 0 || tempSetting > 100) {
     printErrorMessage();
     return; 
  }  

  if(textBuff[0] == 'c') {
    critTemp = tempSetting;  
    client.println("\nCritical threshold set to:");
    client.println(critTemp);
    EEPROM.write(0, (byte)critTemp);
  }  

  if(textBuff[0] == 'w') {
    warnTemp = tempSetting;  
    client.println("\nWarning threshold set to:");
    client.println(warnTemp);
        EEPROM.write(1, (byte)warnTemp);
  }
  client.println("\nSaved to EEPROM!");  

}


void displayCurrent() {
  
  client.println("\nCURRENT VALUES (from EEPROM):");  
  client.println("\nCritical Alarm: ");
  client.println(critTemp);
  client.println("\nWarning Alarm: ");
  client.println(warnTemp);

}  
  

int parseDigit(char c)
{
  int digit = -1;
  digit = (int) c - 0x30; // subtracting 0x30 from ASCII code gives value
  if(digit < 0 || digit > 9) digit = -1;
  return digit;
}


void printErrorMessage()
{
  client.println("Unrecognized command.  ? for help.");
} 


void checkCloseConnection()
  // if we got here, textBuff[0] = 'c', check the next two
  // characters to make sure the command is valid
{
  if (textBuff[0] == 'q' && textBuff[1] == 0x0d) 
    closeConnection();
  else
    printErrorMessage();
} 


void closeConnection()
{
  client.println("\n\nBye!\n");
  client.stop();
  connectFlag = 0;
}


void printHelpMessage()
{

  client.println("PLEASE NOTE: NONE OF THE INPUT IS REALLY VALIDATED, SO MAKE ABSOLUTELY SURE YOU DON'T ENTER GARBAGE HERE!\n\n");
  client.println("Supported commands:");
  client.println("  cxxx - Set critical alarm threshold (Celsius). xxx = Positive integer values [0-100 C] only! Example: c30 - set critical alarm to 30 degrees.");
  client.println("  wyyy - Set warning alarm threshold (Celsius). yyy = Positive integer values [0-100 C] only! Example: w28 - set warning alarm to 28 degrees.");
  client.println("  d - Display current values");
  client.println("  q - Close connection and quit");
  client.println("  ? ------- Print this help message");

}


