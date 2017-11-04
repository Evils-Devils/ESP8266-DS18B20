#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <OneWire.h>

const char* ssid = "Your_wifi_network";
const char* password = "Your_wifi_pass";

ESP8266WebServer server(80);

/**************/
/* NTP things */
/**************/

unsigned int ntpPort = 2390;
const char* ntpServerName = "time.nist.gov";

unsigned long epoch;
unsigned long epoch_millis;
WiFiUDP udp;
IPAddress timeServerIP;
const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

unsigned long sendNTPpacket(IPAddress& address) {
  Serial.println("sending NTP packet...");
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
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket(); 
}

void checkNTPPacket() { /* Checks if there is an NTP reply and parses it if so */
  int cb = udp.parsePacket();
  if (cb) {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, extract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    epoch = secsSince1900 - seventyYears;
        epoch_millis = millis();
    // print Unix time:
    Serial.println(epoch);
    String test = String(epoch);
  } // else do nothing cause this will run many times
}

int getTime() {
  return epoch + (millis() - epoch_millis) / 1000;
}

/*********************/
/* Temperature stuff */
/*********************/

OneWire ds(2); // Temperature sensor on pin GPIO2

float temp_celcius = 0;

byte temp_addr[8];
byte temp_type_s;

void temp_init() {
  retry:
  if (!ds.search(temp_addr)) {
    Serial.println("No more addresses.");
    Serial.println();
    ds.reset_search();
    delay(1000);
    goto retry;
  }
  if (OneWire::crc8(temp_addr, 7) != temp_addr[7]) {
    Serial.println("CRC is not valid!");
    delay(1000);
    goto retry;
  }
  // the first ROM byte indicates which chip
  switch (temp_addr[0]) {
    case 0x10:
      temp_type_s = 1;
      break;
    case 0x28:
      temp_type_s = 0;
      break;
    case 0x22:
      temp_type_s = 0;
      break;
    default:
      return;
  }
}

void temp_request() {
  ds.reset();
  ds.select(temp_addr);
  ds.write(0x44, 1); // start conversion, with parasite power on at the end
}

void temp_read() { /* Should be run 750ms or more after temp_request() */
  byte i;
  byte data[12];

  ds.reset();
  ds.select(temp_addr);    
  ds.write(0xBE); // Read Scratchpad

  for (i = 0; i < 9; i++) { // we need 9 bytes
    data[i] = ds.read();
  }

  // Convert the data to actual temperature
  // because the result is a 16 bit signed integer, it should
  // be stored to an "int16_t" type, which is always 16 bits
  // even when compiled on a 32 bit processor.
  int16_t raw = (data[1] << 8) | data[0];
  if (temp_type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // "count remain" gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    // at lower res, the low bits are undefined, so let's zero them
    if (cfg == 0x00)
      raw = raw & ~7;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20)
      raw = raw & ~3; // 10 bit res, 187.5 ms
    else if (cfg == 0x40)
      raw = raw & ~1; // 11 bit res, 375 ms
    // default is 12 bit resolution, 750 ms conversion time
  }
  temp_celcius = (float) raw / 16.0;
}

/******************************************/
/* Setup and loop to glue things together */
/******************************************/

unsigned long packet_sent_time = 0; // Time we last requested time from an NTP server
unsigned long temp_update_time = 0;

void handleRoot() {
  server.send(200, "text/plain", "{\"time\":\"" + ((String) getTime()) + "\",\"t1\":\"" + ((String) temp_celcius + "\"}");
}

void setup() {
  pinMode(2, INPUT_PULLUP);
/*
  pinMode(5, OUTPUT);               // V
  pinMode(0, OUTPUT);               // V
  digitalWrite(5, HIGH);            // V
  digitalWrite(0, LOW); // power sensor from gpio
*/
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  temp_init();
  Serial.println("");

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");   //NTP
  udp.begin(ntpPort);               //NTP
  Serial.print("Local port: ");     //NTP
  Serial.println(udp.localPort());  //NTP

  server.begin();
  Serial.println("HTTP server started");

  server.on("/", handleRoot);
}

void loop() {
  // Deal with HTTP clients
  server.handleClient();
  // Request NTP time if it's been longer than 10sec ago
  unsigned long ct = millis();
  if (ct - packet_sent_time > 10000) {
    packet_sent_time = ct;
    WiFi.hostByName(ntpServerName, timeServerIP); 
    // send an NTP packet to a time server
    sendNTPpacket(timeServerIP);
  }
  // Also check if we should request new temperature from the sensor every second
  if (ct - temp_update_time > 1000) {
    temp_update_time = ct;
    temp_read(); // This means the first second running temperature will be invalid
    temp_request();
  }
  // See if a response from an NTP server came in
  checkNTPPacket();
}
