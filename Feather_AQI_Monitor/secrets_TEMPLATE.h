/*
 * This separate header file allows passwords, API keys, and other sensitive info
 * private and out of code that otheriwse would be shared or distributed (e.g., in GitHub
 * or blog posts, etc.).
 * 
 * IMPORTANT: This instance, "secrets_TEMPLATE.h", is the template from which a proper,
 * local "secrets.h" file should be created.  Copy this file to create "./secrets.h" and
 * edit it to have the right configuration values for your development and deployment
 * environment.
 * 
 * While you'll need "./secrets.h" for the sketch to work you should never share that
 * file or include it (or its contents) in published code.
 */

// WiFi configuration settings
const char* ssid     = "YOUR_SSID_GOES_HERE";
const char* password = "YOUR_WIFI_PASSWORD_GOES_HERE";


// Device configuration info for dweet.io & ThingSpeak
/* Info for device on dweet.io */
const char* dweetName = "DWEET_THING_NAME";
const int channelID   = 1234567;  // YOUR THINGSPEAK CHANNEL ID GOES HERE
String writeAPIKey    = "CHANNELWRITEAPIKEY"; // write API key for ThingSpeak Channel
