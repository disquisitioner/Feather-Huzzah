/*
 * Air Quality Monitor built using an Adafruit Feather Huzzah with a Plantower PM2.5 sensor 
 * (also from Adafruit).   PM2.5 sensor reports data approximately every second using a serial
 * interface, which is read here using the software serial library.  
 * 
 * Author: David Bryant (david@disquisitioner.com)
 * 
 * Reports observations over Wifi via the Feather Huzzah and dweet.io.  Because the data gets 
 * reported to the web there's no display, though useful information is output via the serial
 * monitor to streamline development.
 *
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>  
#include <ESP8266WiFi.h>
#include "Adafruit_PM25AQI.h"

// PM2.5 reports data over a Serial interface
#include <SoftwareSerial.h>
SoftwareSerial pmSerial(12,13);   // PM2.5 TX connected to digital pin 12, PM2.5 RX ignored

// WiFi configuration settings
const char* ssid     = "YOUR_SSID_GOES_HERE";
const char* password = "YOUR_WIFI_PASSWORD_GOES_HERE";


// Device configuration info for dweet.io & ThingSpeak
/* Info for home cellar monitor */
const char* dweetName = "DWEET_THING_NAME";
const int channelID   = 1234567;  // YOUR THINGSPEAK CHANNEL ID GOES HERE
String writeAPIKey    = "CHANNELWRITEAPIKEY"; // write API key for ThingSpeak Channel


// Use WiFiClient class to create TCP connections and talk to hosts
WiFiClient client;

//Create Instance of PM2.5 sensor
Adafruit_PM25AQI aqi = Adafruit_PM25AQI();

/*
 * Need to accumulate several pieces of data to calculate ongoing readings
 *
 * In a moving vehicle good values are a sample delay of 2 seconds and a capture
 * delay (reporting inverval) of 2 minutes.  At a stationary location such as a home
 * weather station good values are a sample delay of 5 seconds and a capture delay
 * of 5 minutes.
 *
 */
const int sampleDelay = 2;         /* Interval at which temperature sensor is read (in seconds) */
const int reportDelay = 5;          /* Interval at which samples are averaged & reported (in minutes) */
const unsigned long reportDelayMs = reportDelay * 60 * 1000L;  /* Calculation interval */
const unsigned long sampleDelayMs = sampleDelay * 1000L;       /* Sample delay interval */
unsigned long prevReportMs = 0;  /* Timestamp for measuring elapsed capture time */
unsigned long prevSampleMs  = 0;  /* Timestamp for measuring elapsed sample time */
unsigned int numSamples = 0;  /* Number of overall sensor readings over reporting interval */
unsigned int numReports = 0; /* Number of capture intervals observed */

float Pm25 = 0;        /* Air Quality reading (PM2.5) over capture interval */
float CurrentPm25 = 0; /* Current PM2.5 reading from AQI sensor */
float AvgPm25;         /* Average PM2.5 as reported last capture interval */
float MinPm25 = 1999;   /* Observed minimum PM2.5 */
float MaxPm25 = -99;   /* Observed maximum PM2.5 */

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
  
  // Wait one second for sensor to boot up!
  delay(1000);
  pmSerial.begin(9600);
  if (! aqi.begin_UART(&pmSerial)) { // connect to the sensor over software serial 
    Serial.println("Could not find PM 2.5 sensor!");
    while (1) delay(10);
  }
  Serial.println("PM25 found!");

  // Remember current clock time
  prevReportMs = prevSampleMs = millis();
}

void  loop() {
  PM25_AQI_Data data;
  unsigned long currentMillis = millis();    // Check current timer value
  
  /* See if it is time to read the sensors */
  if( (currentMillis - prevSampleMs) >= sampleDelayMs) {
    if (! aqi.read(&data)) {
      // Serial.println("Could not read from AQI");
      return;
    }
    // Since we have a sample, count it
    numSamples ++;  // Since we started at 0...

    CurrentPm25 = data.pm25_env;
    Pm25 +=  CurrentPm25;

    Serial.print("Read data PM25: ");
    Serial.print(CurrentPm25);
    Serial.print(" = AQI: ");
    Serial.println(pm25toAQI(CurrentPm25));
    
    // Save sample time
    prevSampleMs = currentMillis;
  }
  /* Now check and see if it is time to report averaged values */
  if( (currentMillis - prevReportMs) >= reportDelayMs) {

    AvgPm25 = Pm25 / numSamples;
    if(AvgPm25 > MaxPm25) MaxPm25 = AvgPm25;
    if(AvgPm25 < MinPm25) MinPm25 = AvgPm25;

    /* Post both the current readings and historical max/min readings to the web */
    post_dweet(AvgPm25,pm25toAQI(MinPm25),pm25toAQI(MaxPm25),pm25toAQI(AvgPm25));

    // Also post the sensor data to ThingSpeak
    post_thingspeak(AvgPm25,pm25toAQI(MinPm25),pm25toAQI(MaxPm25),pm25toAQI(AvgPm25));
    
    // Reset counters and accumulators
    prevReportMs = currentMillis;
    numSamples = 0;
    Pm25 = 0;
  }
}

// Post a dweet to report the temperature reading.  This routine blocks while
// talking to the network, so may take a while to execute.
const char* dweet_host = "dweet.io";
void post_dweet(float pm25, float minaqi, float maxaqi, float aqi)
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
                     "\"AQI\":\""           + String(aqi,2)          + "\"," +
                     "\"address\":\""       + ip.toString()          + "\"," +
                     "\"PM25_value\":\""    + String(pm25,2)          + "\"," +
                     "\"min_AQI\":\""       + String(minaqi,2)       + "\"," + 
                     "\"max_AQI\":\""       + String(maxaqi,2)       + "\"}";
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
void post_thingspeak(float pm25, float minaqi, float maxaqi, float aqi) {
  if (client.connect(ts_server, 80)) {
    
    // Measure Signal Strength (RSSI) of Wi-Fi connection
    long rssi = WiFi.RSSI();
    Serial.print("RSSI: ");
    Serial.println(rssi);

    // Construct API request body
    String body = "field1=";
           body += String(aqi,2);
           body += "&";
           body += "field2=";
           body += String(pm25,2);
           body += "&";
           body += "field3=";
           body += String(maxaqi,2);
           body += "&";
           body += "field4=";
           body += String(minaqi,2);


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

// Converts pm25 reading to AQI using the AQI Equation
// (https://forum.airnowtech.org/t/the-aqi-equation/169)

float pm25toAQI(float pm25)
{  
  if(pm25 <= 12.0)       return(fmap(pm25,  0.0, 12.0,  0.0, 50.0));
  else if(pm25 <= 35.4)  return(fmap(pm25, 12.1, 35.4, 51.0,100.0));
  else if(pm25 <= 55.4)  return(fmap(pm25, 35.5, 55.4,101.0,150.0));
  else if(pm25 <= 150.4) return(fmap(pm25, 55.5,150.4,151.0,200.0));
  else if(pm25 <= 250.4) return(fmap(pm25,150.5,250.4,201.0,300.0));
  else if(pm25 <= 500.4) return(fmap(pm25,250.5,500.4,301.0,500.0));
  else return(505.0);  // AQI above 500 not recognized
}

float fmap(float x, float xmin, float xmax, float ymin, float ymax)
{
    return( ymin + ((x - xmin)*(ymax-ymin)/(xmax - xmin)));
}
