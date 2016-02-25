/*
  This gap counter will record the clear time between vehicles and the time vehicles block the sight line.
  Each row recorded will include the length of time obscured and the length of the subsequent gap.
  Code by Bob Edmiston
  Sun Jan 20, 2016 updated for use with buck converter
*/

#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_LEDBackpack.h>
#include <Adafruit_GFX.h>
#include <SD.h>

#include <SoftwareSerial.h>

// A simple data logger for the Arduino analog pins
#define LOG_INTERVAL  5 // mills between entries
#define LAG 0 // slow it down so measurements are accurate // was 30
#define ECHO_TO_SERIAL   1 // echo data to serial port
#define S7S 0 // 1 if using the sparkfun red big led shield, 0 if using adafruit backpack on logger board
//#define WAIT_TO_START    0 // Wait for serial input in setup()
#define LEDPIN 9
#define LEDERROR 8  // digital pint for red error LED
#define SENSORPIN 2
#define VOLTAGEPIN A1
#define CHIPSELECT 10 // for the data logging shield, we use digital pin 10 for the SD cs line
#define BEEP 0
#define VOLTAGEFACTOR 55

#if !S7S
  Adafruit_7segment matrix = Adafruit_7segment();
#endif
// These are the Arduino pins required to create a software seiral
//  instance. We'll actually only use the TX pin.
#if S7S
const int softwareTx = 8;
const int softwareRx = 7;
SoftwareSerial s7s(softwareRx, softwareTx);
#endif

RTC_DS1307 rtc; // define the Real Time Clock object
File logFile; // the logging file
//LiquidTWI lcd(0); // Connect via i2c, default address #0 (A0-A2 not jumpered)
volatile unsigned long count = 0; // start the count at zero
volatile unsigned long blockedTime;
volatile unsigned long vacantTime;
volatile unsigned long gapstartTime;
volatile unsigned long gapendTime;
volatile unsigned long blockstartTime;
volatile unsigned long blockendTime;
int sensorValue = 0;  // variable to store the value coming from the sensor
int triggerSense = 0;
int vsensorValue = 0;
volatile unsigned long upTime;
volatile float sourceVolts;
String dataString = "";
unsigned int distance;
DateTime now;

void setup() {
  // assign pins
  pinMode(LEDPIN, OUTPUT);   // declare the LEDPIN as an OUTPUT:
  pinMode(LEDERROR, OUTPUT);   // declare the LEDERROR as an OUTPUT:
  pinMode(SENSORPIN, INPUT);   // declare the VOLTAGEPIN as an INPUT:
  digitalWrite(SENSORPIN,HIGH);
  pinMode(VOLTAGEPIN, INPUT);   // declare the VOLTAGEPIN as an INPUT:
  pinMode(CHIPSELECT, OUTPUT); // for the data logging shield, we use digital pin 10 for the SD cs line

  Wire.begin();

  rtc.begin(); // start the clock
  
#if ECHO_TO_SERIAL
  Serial.begin(115200);
  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
  }
  Serial.println("7 Segment Backpack Test");
#endif  //ECHO_TO_SERIAL

  if (!rtc.begin()) {
    logFile.println("RTC failed");
#if ECHO_TO_SERIAL
    Serial.println("RTC failed");
#endif  //ECHO_TO_SERIAL
  }
  
  if (openFileIO() == 0) {
    exit(1); // if making a new file fails, halt.
  }

   // Must begin s7s software serial at the correct baud rate.
  //  The default of the s7s is 9600.
#if S7S
  s7s.begin(9600);
  // Clear the display, and then turn on all segments and decimals
  clearDisplay();  // Clears display, resets cursor
  s7s.print("-HI-");  // Displays -HI- on all digits
  delay(LOG_INTERVAL);
  s7s.print("-LO-"); 
  delay(LOG_INTERVAL);
  //setDecimals(0b111111);  // Turn on all decimals, colon, apos
  clearDisplay();  // Clears display, resets cursor
#else 
  matrix.begin(0x70);
#endif
  digitalWrite(LEDPIN, HIGH);
  digitalWrite(LEDERROR, LOW);
//  clearDisplay();  // Clears display, resets cursor
  gapstartTime = millis();
  now = rtc.now();
} // setup

void loop() {  // begin the relatime loop at zero
  delay (LOG_INTERVAL);  // Debouncing delay. Wait this long of clear time before taking another measurement.
  sensorValue = digitalRead(SENSORPIN); // see if any obstructions are present
  triggerSense = sensorValue;  // save the observed value for writing the the file because sensorValue is reused to check for vacancy
  now = rtc.now(); // record the time and date now so it can be written to the file when the row is written.
  if ((now.hour()==0) && (now.minute()==0)) count = 0; // count turns into a pumpkin at midnight
  if (!sensorValue) { // if there is voltage, then something is present in the beam so this is the end of the gap and beginning of the block.  //was +40 ms
    gapendTime = millis(); // timestamp the end of the gap in traffic because something blocked the beam
    blockstartTime = millis(); // timestamp the start of the blockage of the beam.  Same as gap end timestamp.
    count++;  // increment the number of beam blockages counted since power on.
    
    digitalWrite(LEDPIN, LOW);   // turn the Green LED off because the beam is blocked.
    digitalWrite(LEDERROR, HIGH); // turn on the Red LED because the beam is blocked.

    
#if BEEP  // for counters that have the buzzer implemented, buzz when there is obstruction.  Typically used for debugging because the buzzer is annoying in the field.
    tone(6, 4000); // play a note on pin 6 until the beam is cleared
#endif
   clearDisplay();  // Clears display, resets cursor
   while (!sensorValue) { // wait until the beam is cleared.  When sense voltage drops below BASELINE, the view is clear.
      sensorValue = digitalRead(SENSORPIN);  // read the sensor to see if it's still blocked.
      writeLED((millis()-gapendTime)/1000); // display the length of the blockage in integer seconds 
      blockedTime = (millis()-gapendTime)/10;
//      delay(800);
//      writeLED(sensorValue);
//      delay(800);
      delay(LOG_INTERVAL);
   }
   
#if BEEP
    noTone(6);         // turn off tone function for pin 6:
#endif
    blockendTime = millis();
    vacantTime = gapendTime - gapstartTime; // The gap in traffic in milliseconds is the time from when the beam last cleared until it got blocked again.
    upTime = millis() / 1000; // A measure of uptime in seconds. Number of seconds since we powered on the unit.

    // assemble the comma delimited string for output to serial port and to file io
    dataString = "";  // clear out the data string since we last used it.
    dataString += now.year();dataString += ('/');
    dataString += now.month();dataString += ('/');
    dataString += now.day();
    dataString += " "; 
    dataString += now.hour();  dataString += (':');
    dataString += now.minute(); dataString += (':');
    dataString += now.second(); dataString += (',');
    dataString += now.year(); dataString += (',');
    dataString += now.month(); dataString += (',');
    dataString += now.day(); dataString += (',');
    dataString += now.hour(); dataString += (',');
    dataString += now.minute(); dataString += (',');
    dataString += now.second(); dataString += (',');
    dataString += String(upTime); dataString += (',');
    vsensorValue = analogRead(VOLTAGEPIN);
    sourceVolts = float (vsensorValue / VOLTAGEFACTOR);
    dataString += String(sourceVolts); dataString += (',');
    dataString += String(vacantTime); dataString += (',');
    dataString += String(blockendTime - blockstartTime); dataString += (',');
    dataString += ('1'); dataString += (',');
    dataString += String(count);

    logFile.println(dataString); // write data string to file
    logFile.flush(); // The following line will 'save' the file to the SD card after every
#if ECHO_TO_SERIAL
    Serial.println(dataString); // print to the serial port too:
#endif  //ECHO_TO_SERIAL

    //writeLED(count); // display the total count so far to the display.  remains until the next break
    digitalWrite(LEDPIN, HIGH); //green on
    digitalWrite(LEDERROR, LOW); // red off
    gapstartTime = millis();
    clearDisplay();  // Clears display, resets cursor
  } else {
    //clearDisplay();  // Clears display, resets cursor
    vacantTime = (millis() - gapstartTime)/1000;
    //vsensorValue = analogRead(VOLTAGEPIN);
    //sourceVolts = float (vsensorValue / VOLTAGEFACTOR);
    //writeLED(sourceVolts);
    writeLED(count); // showing the total beam breaks, not broken down by day.
    delay(LOG_INTERVAL);
  }
} // loop

int openFileIO() {
  // initialize the SD card
#if ECHO_TO_SERIAL
  Serial.print("Waking up the SD card...");
#endif  //ECHO_TO_SERIAL

  if (!SD.begin(CHIPSELECT)) {
#if ECHO_TO_SERIAL
    Serial.println("Card failed, or not present");
#endif  //ECHO_TO_SERIAL
    // don't do anything more:
    return 0;
  }

#if ECHO_TO_SERIAL
  Serial.println("card initialized.");
#endif  //ECHO_TO_SERIAL
  // create a new file
  char filename[] = "LOGS0000.CSV";

  for (uint8_t i = 0; i < 100; i++) {
#if ECHO_TO_SERIAL
    Serial.println(filename);
#endif  //ECHO_TO_SERIAL
    filename[4] = i / 1000 + '0';
    filename[5] = i / 100 + '0';
    filename[6] = i / 10 + '0';
    filename[7] = i % 10 + '0';
    if (! SD.exists(filename)) {
      // only open a new file if it doesn't exist
      logFile = SD.open(filename, FILE_WRITE);
      break;  // leave the loop!
      delay(80);
    }
  }

  if (!logFile) {
#if ECHO_TO_SERIAL
    Serial.println(filename);
#endif  //ECHO_TO_SERIAL
    error("shit, no file");
  }
  dataString = "MM/DD/YYYY HH:MM:SS,YYYY,MM,DD,HH,MM,SS,UPTIMEs,VOLTSv,GAPms,BLOCKEDms,I,N";
#if ECHO_TO_SERIAL
  Serial.print("Logging to: ");
  Serial.println(filename);
  Serial.println(dataString);
#endif  //ECHO_TO_SERIAL
  logFile.println(dataString); // write data string to file
  return 1;
}


void writeLED(unsigned long amount) { //called only when the beam has been broken
  // try to print a number thats too long
#if S7S
  char tempString[10];  // Will be used with sprintf to create strings
  sprintf(tempString, "%4d", amount);
  //clearDisplay();  // Clears display, resets cursor
  s7s.print(tempString);
#else  //matrix.writeDisplay();
  matrix.println(amount);
  matrix.writeDisplay();
#endif
}

void error(char errorstring[]) // when the file writing fails.
{
#if ECHO_TO_SERIAL
  Serial.print("error: ");
  Serial.println(errorstring);
#endif  //ECHO_TO_SERIAL 
  // red LED indicates error
  digitalWrite(LEDERROR, HIGH);
  logFile.println(errorstring);
  while (1); // halt
}


// Send the clear display command (0x76)
//  This will clear the display and reset the cursor
void clearDisplay()
{
#if S7S
  s7s.write(0x76);  // Clear display command
#else
  matrix.println(0);
  matrix.writeDisplay();
#endif
}


// Set the displays brightness. Should receive byte with the value
//  to set the brightness to
//  dimmest------------->brightest
//     0--------127--------255
void setBrightness(byte value)
{
#if S7S
  s7s.write(0x7A);  // Set brightness command byte
  s7s.write(value);  // brightness data byte
#endif
}

// Turn on any, none, or all of the decimals.
//  The six lowest bits in the decimals parameter sets a decimal
//  (or colon, or apostrophe) on or off. A 1 indicates on, 0 off.
//  [MSB] (X)(X)(Apos)(Colon)(Digit 4)(Digit 3)(Digit2)(Digit1)
void setDecimals(byte decimals)
{
#if S7S
  s7s.write(0x77);
  s7s.write(decimals);
#endif
}

////  Wiring connections
//  Sparkfun RedBoard $24.95 Amazon Prime ASIN: B00BFGZZJO
//  Adafruit Assembled Data Logging shield for Arduino $19.95 + $1.95 stackable headers
//  Sanyo eneloop 2000 mAh typical, 1900 mAh minimum, 1500 cycle, 8 pack AA, Ni-MH Pre-Charged Rechargeable Batteries $20.19 Model: SEC-HR3U8BPN
//  Adafruit Diffused Green 3mm LED (25 pack) - $4.95  ID: 779
//  Adafruit 9V battery clip with 5.5mm/2.1mm plug -  ID: 80  could be better with a 90 degree barrel plug for clearance $3
//  Adafruit Diffused Red 3mm LED (25 pack) -$4.95 ID: 777
//  Adafruit 0.56" 4-Digit 7-Segment Display w/I2C Backpack - White ID:1002 $12,95 plus shipping
//  Optional: Adafruit In-line power switch for 2.1mm barrel jack - ID: 1125  Watch for faulty female connector, had to replace. $3
//  220 ohm  resistor $0.99
//  Radio Shack battery holder $2.99 RadioShack® 8 “AA” Battery Holder Model: 270-407  | Catalog #: 270-407 $3
//
//  IR Sensor Sharp GP2Y0A710K Distance Sensor (100-550cm) - DFRobot.com, got on Amazon for $34.96 out the door.
//  Black x 2 -> Ground
//  Red x 2 -> +5V
//  Blue -> A0 3.3v max so fine with Seeeduino Stalker logic
//
//  Red Error LED
//  D8 -> Anode ->  1K Resistor -> Ground
//
//  Green Count LED
//  D9 -> Anode ->  1K Resistor -> Ground
//
//  Adafruit 0.56" 4-Digit 7-Segment Display w/I2C Backpack (optional)
//  Wiring to the matrix is really easy
//  Connect CLK SCL to the I2C clock - on Arduino UNO thats Analog #5, on the Leonardo it's Digital #3, on the Mega it's digital #21
//  Connect DAT SDA to the I2C data - on Arduino UNO thats Analog #4, on the Leonardo it's Digital #2, on the Mega it's digital #20
//  Connect GND to common ground
//  Connect VCC+ to power - 5V is best but 3V also seems to work for 3V microcontrollers.  3v is dimmer which is better


