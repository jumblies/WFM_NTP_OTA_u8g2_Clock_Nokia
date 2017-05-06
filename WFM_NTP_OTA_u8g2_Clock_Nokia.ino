/*****************************************************************************
  The MIT License (MIT)

  Copyright (c) 2015 by bbx10node@gmail.com

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
 **************************************************************************/

/*
   ESP8266 Arduino IDE

   WiFi clock for PCD8544 LCD display periodically synchronized with Network
   Time Protocol servers.

   Synchronizes with time.nist.gov which randomly selects from a pool of
   NTP (Network Time Protocol) servers.

   The PCD8544 LCD driver is a fork of the Adafruit driver with changes for the ESP8266.
   Be sure to use the esp8266 branch!

   https://github.com/bbx10/Adafruit-PCD8544-Nokia-5110-LCD-library/tree/esp8266

   The Time library provides date and time with external date time sources. The library
   requests UTC date and time from a Network Time Protocol (NTP) server every 5 minutes.
   In between calls to the NTP server, the library uses the millis() function to update
   the date and time. The NTP part of this program is based on the Time_NTP example.

   https://github.com/PaulStoffregen/Time

   The Adafruit_GFX library should be installed using the Arduino IDE Library manager.
   No changes are needed for the ESP8266.

*/
// Libraries needed for WiFi Manager
#include <WiFiManager.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <DNSServer.h>

// Libraries used for NTP clock
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Time.h>

// Libraries needed for display
#include <SPI.h>
//#include <Adafruit_GFX.h>
//#include <Adafruit_PCD8544.h>

// OTA updates
#include <ArduinoOTA.h>

//  Font Library
#include <U8g2lib.h>

// u8g2 Constructor
U8G2_PCD8544_84X48_1_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ D5, /* data=*/ D7 , /* cs=*/ D1, /* dc=*/ D6, /* reset=*/ D2);

// EEPROM for DST variable
#include <EEPROM.h>


//for LED status
#include <Ticker.h>
Ticker ticker;

// Timekeeping
/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
static const char ntpServerName[] = "time.nist.gov";
static const char tzName[] = "Eastern";
static const int timeZone = -4;  // Eastern Standard Time (USA)

int DST; // -4 for DST, -5 for non DST
int EEPROMaddress = 0;


void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

//gets called when WiFiManager enters configuration mode
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  //if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  //entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}


WiFiUDP Udp;
uint16_t localPort;  // local port to listen for UDP packets

void setup()
{
  Serial.begin(115200);

  /* __    __       ___        _______     ___
    |  |  |  |     / _ \      /  _____|   |__ \
    |  |  |  |    | (_) |    |  |  __        ) |
    |  |  |  |     > _ <     |  | |_ |      / /
    |  `--'  |    | (_) |    |  |__| |     / /_
     \______/      \___/      \______|    |____|  */

  u8g2.begin();
  u8g2.firstPage();
  do {
    u8g2.setFontMode(1);              /* activate transparent font mode */
    u8g2.setDrawColor(1);             /* color 1 for the box */
    u8g2.drawBox(0, 0, 84, 48);
    u8g2.setFont(u8g2_font_ncenB14_tr);/* select Font */
    u8g2.setDrawColor(2);
    u8g2.drawStr(5, 30, "Booting");
  } while ( u8g2.nextPage() );
  delay(50);

  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //  wifiManager.resetSettings();

  //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect()) {
    Serial.println("failed to connect and hit timeout");
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }
  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  ticker.detach();

  //keep LED on LOW = on, HIGH = Off
  digitalWrite(BUILTIN_LED, HIGH);

  Serial.print(F("IP number assigned by DHCP is "));
  Serial.println(WiFi.localIP());

  // Seed random with values unique to this device
  uint8_t macAddr[6];
  WiFi.macAddress(macAddr);
  uint32_t seed1 =
    (macAddr[5] << 24) | (macAddr[4] << 16) |
    (macAddr[3] << 8)  | macAddr[2];
  randomSeed(WiFi.localIP() + seed1 + micros());
  localPort = random(1024, 65535);

  Serial.println(F("Starting UDP"));
  Udp.begin(localPort);
  Serial.print(F("Local port: "));
  Serial.println(Udp.localPort());
  Serial.println(F("waiting for sync"));

  //TIMEKEEPING
  setSyncProvider(getNtpTime);
  setSyncInterval(5 * 60);


  //EEPROM DST TEST
  //  EEPROM.put(EEPROMaddress, 1);
  //  Serial.println("EEPROM has been written as 1");

  //DST Time Correction
  DST = EEPROM.read(EEPROMaddress);
  Serial.print("EEPROM data reads as : ");
  Serial.println(DST);

  //OTA section
  ArduinoOTA.setHostname("ESPOTAClock");

  // No authentication by default
  //   ArduinoOTA.setPassword((const char *)"ESP8266NET");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();

}


void loop()
{

  static time_t prevDisplay = 0; // when the digital clock was displayed
  timeStatus_t ts = timeStatus();

  switch (ts) {
    case timeNeedsSync:
    case timeSet:
      if (now() != prevDisplay) { //update the display only if time has changed
        prevDisplay = now();
        digitalClockDisplay();
        if (ts == timeNeedsSync) {
          Serial.println(F("time needs sync"));
          break;
        }
      }
      break;
    case timeNotSet:
      Serial.println(F("Time not set"));
      delay(1000);                      //Missing delay here to prevent flooding of serial comm
      now();
      break;
    default:
      break;
  }
  ArduinoOTA.handle();

}

void digitalClockDisplay() {
  tmElements_t tm;
  char *dayOfWeek;
  breakTime(now(), tm);
  dayOfWeek = dayShortStr(tm.Wday);
  // digital clock display of the time
  Serial.printf("%s %02d %02d %04d %02d:%02d:%02d\r\n",
                dayOfWeek, tm.Month, tm.Day, tm.Year + 1970,
                (tm.Hour + (DST * -1)), tm.Minute, tm.Second);


  u8g2.firstPage();
  do {

    //Little numerals for date across top
    u8g2.setFont(u8g2_font_prospero_bold_nbp_tf);
    u8g2.setCursor(1, 9);
    u8g2.printf("%s %02d %02d %04d\n", dayOfWeek, tm.Month, tm.Day, tm.Year + 1970);

    // seconds
    u8g2.setCursor(68, 47);
    u8g2.printf("%02d", tm.Second);

    //Big Numerals for time
    u8g2.setFont(  u8g2_font_helvB24_tn);  //u8g2_font_logisoso24_tf Second Choice
    u8g2.setCursor(1, 35);
    u8g2.printf("%02d", tm.Hour + (DST * -1));

    u8g2.setCursor(35, 32);
    u8g2.print(":");  //Raising the colon higher than the bottom line

    u8g2.setCursor(45, 35);
    u8g2.printf("%02d", tm.Minute);

    // Graphical Seconds
    u8g2.setDrawColor(1); /* color  for the box */
    u8g2.drawFrame(0, 38, 61, 10);
    u8g2.drawBox(1, 38, (tm.Second), 10);

  } while ( u8g2.nextPage() );
  delay(50);
}

/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress timeServerIP; // time.nist.gov NTP server address

  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  Serial.print(F("Transmit NTP Request "));
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);
  Serial.println(timeServerIP);

  sendNTPpacket(timeServerIP);
  uint32_t beginWait = millis();
  while ((millis() - beginWait) < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + (timeZone * SECS_PER_HOUR);
    }
  }
  Serial.println(F("No NTP Response :-("));
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress & address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

