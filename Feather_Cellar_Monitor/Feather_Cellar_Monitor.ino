/*
 * Temperature & humidity monitor built using an Adafruit Feather Huzzah and 
 * either an HTU21D (now retired) or Si7021, both from sparkfun.com and supported using a 
 * common sensor library
 * 
 * Author: David Bryant (david@disquisitioner.com)
 * 
 * Reports observations over Wifi via the Feather Huzzah and dweet.io.  Because the data gets 
 * reported to the web there's no display, though useful information is output via the serial
 * monitor to streamline development.
 * 
 * Arduino (Feather Huzzah) Pinouts
 *   4 (SDA) = SDA to Si7021 or HTU21D (CHECK TO SEE IF YOU NEED IN-LINE RESISTORS (see below))
 *   5 (SCL) = SCL to Si7021 or HTU21D (CHECK TO SEE IF YOU NEED IN-LINE RESISTORS (see below))
 *   3V  = Si7021 + or HTU21D VCC
 *   GND = SI7021 - or HTU22D GND
 *
 * Additional connections.  Note that newer versions of the Sparkfun Si7021 and HTU21D breakout
 * boards have in-line pull-up resistors for SCA & SDL given the sensor itself is
 * a 3.3V part and most Arduinos have 5V I/O lines. Check the documentation  for your breakout 
 * board to be sure. If not you'll need pull-ups resistors on SCA and SDL (4.7K works well).
 * Alternatively, if you are using multiple I2C devices you may need to disable the onboard
 * pullups and use one set for all devices on the bus.  Read device documentation to be sure.
 *   
 *   Note that the HTU21D has been retired and is no longer available.  The Si7021 has replaced it.
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>  
#include <ESP8266WiFi.h>
#include "SparkFun_Si7021_Breakout_Library.h"

// WiFi configuration settingswifi
const char* ssid     = "YOUR_SSID_GOES_HERE";
const char* password = "YOUR_WIFI_PASSWORD_GOES_HERE";


// Device configuration info for dweet.io & ThingSpeak
const char* dweetName = "DWEET_THING_NAME";
const int channelID   = 1234567;  // YOUR THINGSPEAK CHANNEL ID GOES HERE
String writeAPIKey    = "CHANNELWRITEAPIKEY"; // write API key for ThingSpeak Channel


// Use WiFiClient class to create TCP connections and talk to hosts
WiFiClient client;

//Create Instance of HTU21D or SI7021 temp and humidity sensor
Weather sensor;
/*
 * Need to accumulate several pieces of data to calculate ongoing readings
 *
 * In a moving vehicle good values are a sample delay of 2 seconds and a capture
 * delay (reporting inverval) of 2 minutes.  At a stationary location such as a home
 * weather station good values are a sample delay of 5 seconds and a capture delay
 * of 5 minutes.
 *
 */
const int sampleDelay = 20;         /* Interval at which temperature sensor is read (in seconds) */
const int reportDelay = 5;          /* Interval at which samples are averaged & reported (in minutes) */
const unsigned long reportDelayMs = reportDelay * 60 * 1000L;  /* Calculation interval */
const unsigned long sampleDelayMs = sampleDelay * 1000L;       /* Sample delay interval */
unsigned long prevReportMs = 0;  /* Timestamp for measuring elapsed capture time */
unsigned long prevSampleMs  = 0;  /* Timestamp for measuring elapsed sample time */
unsigned int numSamples = 0;  /* Number of overall sensor readings over reporting interval */
unsigned int numReports = 0; /* Number of capture intervals observed */

float TempF = 0;        /* Temperature F reading over capture interval */
float CurrentTempF = 0; /* Current reading from temperature sensor */
float AvgTempF;         /* Average temperature as reported last capture interval */
float MinTempF = 199;   /* Observed minimum temperature */
float MaxTempF = -99;   /* Observed maximum temperature */

float Humidity = 0;     /* Humidity reading over capture interval */
float CurrentHum = 0;   /* Current reading from humidity sensor */
float AvgHum;           /* Average humidity as reported last capture interval */
float MinHum = 199;     /* Observed minimum humidity */
float MaxHum = -99;     /* Observed maximum humidity */

void setup() {
  
  Serial.begin(115200);

  Serial.print("Sampling delay: ");  Serial.print(sampleDelay); Serial.println(" seconds");
  Serial.print("Reporting delay: "); Serial.print(reportDelay); Serial.println(" minutes");


  // We start by connecting to a WiFi network           
  WiFi.begin(ssid, password);

  // Give some feedback while we wait...
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
     
  Serial.println("done");
  Serial.println("WiFi connected");  
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Sampling...please wait");
  
  // Initialize the I2C sensors and ping them
  sensor.begin();

  // Remember current clock time
  prevReportMs = prevSampleMs = millis();
}

void  loop() {

  unsigned long currentMillis = millis();    // Check current timer value
  
  /* See if it is time to read the sensors */
  if( (currentMillis - prevSampleMs) >= sampleDelayMs) {
    numSamples ++;  // Since we started at 0...

    /* Get the temperature and humidity from the sensor */
    CurrentHum   = sensor.getRH();
    // Temperature is measured every time RH is requested.
    // It is faster, therefore, to read it from previous RH
    // measurement with getTemp() instead of with readTemp()
    CurrentTempF = sensor.getTempF();
    TempF  += CurrentTempF;
    Humidity += CurrentHum;
    
    // Save sample time
    prevSampleMs = currentMillis;
  }
  /* Now check and see if it is time to report averaged values */
  if( (currentMillis - prevReportMs) >= reportDelayMs) {

    AvgTempF = TempF / numSamples;
    if(AvgTempF > MaxTempF) MaxTempF = AvgTempF;
    if(AvgTempF < MinTempF) MinTempF = AvgTempF;
    
    AvgHum  =  Humidity / numSamples;
    if(AvgHum >  MaxHum) MaxHum = AvgHum;
    if(AvgHum < MinHum)  MinHum = AvgHum;

    /* Post both the current readings and historical max/min readings to the web */
    post_dweet(AvgTempF,AvgHum,MinTempF,MaxTempF,MinHum,MaxHum);

    // Also post the sensor data to ThingSpeak
    post_thingspeak(AvgTempF,AvgHum,MinTempF,MaxTempF,MinHum,MaxHum);
    
    // Reset counters and accumulators
    prevReportMs = currentMillis;
    numSamples = 0;
    TempF = 0;
    Humidity = 0;
  }
}

// Post a dweet to report the temperature reading.  This routine blocks while
// talking to the network, so may take a while to execute.
const char* dweet_host = "dweet.io";
void post_dweet(float tempF, float humidity, float mint, float maxt, float minh, float maxh)
{
  
  if(WiFi.status() != WL_CONNECTED) {
    Serial.print("Lost network connection to '");
    Serial.print(ssid);
    Serial.println("'!");
    return;
  }
  
  Serial.print("connecting to ");
  Serial.print(dweet_host);
  Serial.print(" as ");
  Serial.println(dweetName);
      
  // Use our WiFiClient to connect to dweet
  if (!client.connect(dweet_host, 80)) {
    Serial.println("connection failed");
    return;
  }

  long rssi = WiFi.RSSI();
  IPAddress ip = WiFi.localIP();

  // Use HTTP post and send a data payload as JSON
  
  String postdata = "{\"wifi_rssi\":\""     + String(rssi)           + "\"," +
                     "\"address\":\""       + ip.toString()          + "\"," +
                     "\"temperature\":\""   + String(tempF,2)        + "\"," +
                     "\"min_temp\":\""      + String(mint,2)         + "\"," + 
                     "\"max_temp\":\""      + String(maxt,2)         + "\"," +
                     "\"humidity\":\""      + String(humidity,2)     + "\"," +
                     "\"min_humidity\":\""  + String(minh,2)         + "\"," +
                     "\"max_humidity\":\""  + String(maxh,2)         + "\"}";
  // Note that the dweet device 'name' gets set here, is needed to fetch values
  client.print("POST /dweet/for/");
  client.print(dweetName);
  client.println(" HTTP/1.1");
  client.println("Host: dweet.io");
  client.println("User-Agent: ESP8266 (orangemoose)/1.0");  // Customize if you like
  client.println("Cache-Control: no-cache");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(postdata.length());
  client.println();
  client.println(postdata);
  Serial.println(postdata);

  delay(1500);  
  // Read all the lines of the reply from server (if any) and print them to Serial Monitor
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
   
  Serial.println("closing connection");
  Serial.println();
}

// ThingSpeak data upload
const char* ts_server = "api.thingspeak.com";
void post_thingspeak(float tempF, float humidity, float mint, float maxt, float minh, float maxh) {
  if (client.connect(ts_server, 80)) {
    
    // Measure Signal Strength (RSSI) of Wi-Fi connection
    long rssi = WiFi.RSSI();
    Serial.print("RSSI: ");
    Serial.println(rssi);

    // Construct API request body
    String body = "field1=";
           body += String(tempF,2);
           body += "&";
           body += "field2=";
           body += String(maxt,2);
           body += "&";
           body += "field3=";
           body += String(mint,2);
           body += "&";
           body += "field4=";
           body += String(humidity,2);
           body += "&";
           body += "field5=";
           body += String(maxh,2);
           body += "&";
           body += "field6=";
           body += String(minh,2);


    client.println("POST /update HTTP/1.1");
    client.println("Host: api.thingspeak.com");
    client.println("User-Agent: ESP8266 (orangemoose)/1.0");  // Customize if you like
    client.println("Connection: close");
    client.println("X-THINGSPEAKAPIKEY: " + writeAPIKey);
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println("Content-Length: " + String(body.length()));
    client.println("");
    client.print(body);
    Serial.println(body);

  }
  delay(1500);
    
  // Read all the lines of the reply from server (if any) and print them to Serial Monitor
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  client.stop();
}
