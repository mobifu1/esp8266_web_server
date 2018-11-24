// Minimal NTP Time Demo with DST correction

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <time.h>
#include <simpleDSTadjust.h>
#include <sundata.h>

// -------------- Configuration Options -----------------

// Update time from NTP server every 5 hours
#define NTP_UPDATE_INTERVAL_SEC 5*3600

// Maximum of 3 servers
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov", "us.pool.ntp.org"

//Central European Time Zone (Paris, Berlin)
#define timezone 1 // +1 hour = Central European Time Zone
//Last > Sunday > March > 02:00 > +3600Sec(2Hours)
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600};    // Daylight time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 3, 1800};       // Standard time = UTC/GMT +1 hour

const char* ssid = "DD8ZJ";
const char* password = "44876708218574845522";

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String output1State = "off";
String output2State = "off";
boolean auto_switch_by_sun = false;
int auto_switch_off_hour = 21; //22:00 Uhr

// Assign output variables to GPIO pins
const int output1 = 0; //GPIO 0
const int output2 = 2; //GPIO 2

const int eeprom_size = 32 ; //Size can be anywhere between 4 and 4096 bytes
//-----------------------------------------------------------------

Ticker ticker1;
int32_t tick;

bool readyForNtpUpdate = false;// flag changed in the ticker function to start NTP Update
simpleDSTadjust dstAdjusted(StartRule, EndRule);// Setup simpleDSTadjust Library rules

float lat = 53.0;
float lon = 10.0;

int year_ ;
int  month_ ;
int  day_ ;
int  hour_ ;
int  minute_ ;
int  second_ ;

int time_diff_to_greenwich; // add to UTC > hour 1=winter  2=sommer
boolean daylight; //Day / Night

String am_pm = ""; //AM / PM
String european_time = ""; //CET / CEST
String sunrise_string = "";
String sunset_string = "";
String time_string = "";
String date_string = "";

//-----------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(false);

  // Initialize the output variables as outputs
  pinMode(output2, OUTPUT);
  pinMode(output1, OUTPUT);

  // Set outputs to LOW
  digitalWrite(output1, LOW);
  digitalWrite(output2, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  Serial.println("\nDone");
  updateNTP(); // Init the NTP time
  printTime(0); // print initial time now.

  tick = NTP_UPDATE_INTERVAL_SEC; // Init the NTP update countdown ticker
  ticker1.attach(1, secTicker); // Run a 1 second interval Ticker
  Serial.print("Next NTP Update: ");
  printTime(tick);

  server.begin();//Web-Server
}
//-----------------------------------------------------------------
void loop() {

  website();

  if (readyForNtpUpdate)
  {
    readyForNtpUpdate = false;
    printTime(0);
    updateNTP();
    Serial.print("\nUpdated time from NTP Server: ");
    printTime(0);
    Serial.print("Next NTP Update: ");
    printTime(tick);
  }

  delay(100);  // to reduce upload failures
}
//-----------------------------------------------------------------
//----------------------- Functions -------------------------------
//-----------------------------------------------------------------
void secTicker() {// NTP timer update ticker

  tick--;
  if (tick <= 0)
  {
    readyForNtpUpdate = true;
    tick = NTP_UPDATE_INTERVAL_SEC; // Re-arm
  }

  printTime(0);  // Uncomment if you want to see time printed every second
}
//-----------------------------------------------------------------
void updateNTP() {

  configTime(timezone * 3600, 0, NTP_SERVERS);

  delay(500);
  while (!time(nullptr)) {
    Serial.print("#");
    delay(1000);
  }
}
//-----------------------------------------------------------------
void printTime(time_t offset) {

  char buf[30];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev) + offset;
  struct tm *timeinfo = localtime (&t);

  int hour = (timeinfo->tm_hour + 11) % 12 + 1; // take care of noon and midnight
  sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d%s %s\n", timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_year + 1900, hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour >= 12 ? "pm" : "am", dstAbbrev);
  //Serial.print(buf);
  String line = buf;
  time_split_parameter (line);
  sunrise (lat, lon, time_diff_to_greenwich);//Hamburg 53,5° 10,0°
}
//-----------------------------------------------------------------
void time_split_parameter (String line) { //11/23/2018 03:57:30pm CET

  int line_length = line.length();
  year_ = line.substring(6, 10).toInt();
  month_ = line.substring(0, 2).toInt();
  day_ = line.substring(3, 5).toInt();
  hour_ = line.substring(11, 13).toInt();
  minute_ = line.substring(14, 16).toInt();
  second_ = line.substring(17, 19).toInt();
  am_pm = line.substring(19, 21);
  european_time = line.substring(22, line_length - 1);

  if (am_pm == "pm") {
    if (hour_ >= 1 && hour_ <= 11) hour_ += 12;
  }

  if (european_time == "CET") time_diff_to_greenwich = 1;
  if (european_time == "CEST") time_diff_to_greenwich = 2;

  String lead_zero_hour = ("");
  if (hour_ < 10) {
    lead_zero_hour = (F("0"));
  }
  String lead_zero_minute = ("");
  if (minute_ < 10) {
    lead_zero_minute = (F("0"));
  }
  String lead_zero_second = ("");
  if (second_ < 10) {
    lead_zero_second = (F("0"));
  }
  String lead_zero_day = ("");
  if (day_ < 10) {
    lead_zero_day = (F("0"));
  }
  String lead_zero_month = ("");
  if (month_ < 10) {
    lead_zero_month = (F("0"));
  }

  time_string = "Time: " + lead_zero_hour + String(hour_) + ":" + lead_zero_minute + String(minute_) + ":" + lead_zero_second + String(second_) + " " + european_time;
  date_string = "Date: " + lead_zero_day + String(day_) + "." + lead_zero_month + String(month_) + "." + String(year_);

  //Serial.println(time_string);
  //Serial.println(date_string);
}
//-----------------------------------------------------------------
void sunrise( float latitude , float longitude , int time_diff_to_greenwich) {

  sundata sun = sundata(latitude, longitude, time_diff_to_greenwich); //creat object with latitude and longtitude declared in degrees and time difference from Greenwhich

  sun.time( year_, month_, day_, hour_, minute_, second_);  //insert year, month, day, hour, minutes and seconds
  sun.calculations();                                             //update calculations for last inserted time

  float sun_el_rad = sun.elevation_rad();                    //store sun's elevation in rads
  //float sun_el_deg = sun.elevation_deg();                  //store sun's elevation in degrees
  //Serial.println(String(el_deg) + "Elevation");

  float sun_az_rad = sun.azimuth_rad();                      //store sun's azimuth in rads
  //float az_deg = sun.azimuth_deg();                        //store sun's azimuth in degrees
  //Serial.println(String(az_deg) + "Azimuth");

  float sunrise = sun.sunrise_time();                        //store sunrise time in decimal form
  //Serial.println(String(sunrise) + "Sunrise");
  //sunrise = (sunrise - 0.141666667); //correction factor -8,5 min > Sundata.h calculates unexact
  int sunrise_hour = int(sunrise);
  int sunrise_minute = int((sunrise - sunrise_hour) * 60);

  float sunset = sun.sunset_time();                          //store sunset time in decimal form
  //Serial.println(String(sunset) + "Sunset");
  //sunset = (sunset + 0.1625); //correction factor +9,75 min > Sundata.h calculates unexact
  int sundown_hour = int(sunset);
  int sundown_minute = int((sunset - sundown_hour) * 60);

  if (daylight == true) {
    //Serial.println("now is Day");
    if (auto_switch_by_sun == true) {
      output1State = "off";
      output2State = "off";
    }
  }
  if (daylight == false) {
    //Serial.println("now is Night");
    if (auto_switch_by_sun == true) {
      if (hour_ < auto_switch_off_hour) {
        output1State = "on";
        output2State = "on";
      }
      else {
        output1State = "off";
        output2State = "off";
      }
    }
  }

  String lead_zero = ("");
  if (sunrise_minute < 10) {
    lead_zero = (F("0"));
  }

  sunrise_string = "Sunrise: " + String(sunrise_hour) + ":" + lead_zero + String(sunrise_minute);
  //Serial.println(sunrise_string);

  lead_zero = ("");
  if (sundown_minute < 10) {
    lead_zero = (F("0"));
  }

  sunset_string = "Sunset: " + String(sundown_hour) + ":" + lead_zero + String(sundown_minute);
  //Serial.println(sunset_string);

}
//-----------------------------------------------------------------
void website() {

  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    Serial.println("New Client.");          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // turns the GPIOs on and off
            if (header.indexOf("GET /2/on") >= 0) {
              Serial.println("GPIO 2 on");
              output2State = "on";
              digitalWrite(output2, HIGH);
            } else if (header.indexOf("GET /2/off") >= 0) {
              Serial.println("GPIO 2 off");
              output2State = "off";
              digitalWrite(output2, LOW);
            } else if (header.indexOf("GET /1/on") >= 0) {
              Serial.println("GPIO 1 on");
              output1State = "on";
              digitalWrite(output1, HIGH);
            } else if (header.indexOf("GET /1/off") >= 0) {
              Serial.println("GPIO 1 off");
              output1State = "off";
              digitalWrite(output1, LOW);
            } else if (header.indexOf("GET /3/off") >= 0) {
              Serial.println("Auto Modus off");
              auto_switch_by_sun = false;
            } else if (header.indexOf("GET /3/on") >= 0) {
              Serial.println("Auto Modus on");
              auto_switch_by_sun = true;
            } else if (header.indexOf("Switch+off+Time=") >= 0) {  //GET /%20action_page.php?Switch+off+Time=16 HTTP/1.1
              int index = header.indexOf("=");
              index += 1;
              auto_switch_off_hour = (header.substring(index, index + 2)).toInt();
              Serial.println("Set Switch off Time to: " + String(auto_switch_off_hour));
            }

            // Display the HTML web page
            client.println("<!DOCTYPE html><html>");
            client.println("<html><head><title>ESP8266 Web-Server</title></head><body>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println(".button2 {background-color: #77878A;}</style></head>");
            client.println("<meta http-equiv=\"refresh\" content=\"30\">\r\n");

            // Web Page Heading
            client.println("<body><h1>ESP8266 Web-Server</h1>");

            // Display current state, and ON/OFF buttons for GPIO 1
            client.println("<p>GPIO 1 - State " + output1State + "</p>");
            // If the output1State is off, it displays the OFF button
            if (output1State == "off") {
              client.println("<p><a href=\"/1/on\"><button class=\"button\">OFF</button></a></p>");
            } else {
              client.println("<p><a href=\"/1/off\"><button class=\"button button2\">ON</button></a></p>");
            }

            // Display current state, and ON/OFF buttons for GPIO 2
            client.println("<p>GPIO 2 - State " + output2State + "</p>");
            // If the output2State is off, it displays the OFF button
            if (output2State == "off") {
              client.println("<p><a href=\"/2/on\"><button class=\"button\">OFF</button></a></p>");
            } else {
              client.println("<p><a href=\"/2/off\"><button class=\"button button2\">ON</button></a></p>");
            }

            client.println("<p>---------------------------------------------</p>");
            client.println("<p>" + time_string + " / " + date_string + "</p>");
            client.println("<p>" + sunrise_string + " / " + sunset_string + "</p>");
            client.println("<p>---------------------------------------------</p>");
            client.println("<p>Auto switch on by Sunset / Auto switch off at: " + String(auto_switch_off_hour) + ":00" + "</p>");

            if (auto_switch_by_sun == false) { //button for auto_switch_by_sun
              client.println("<p><a href=\"/3/on\"><button class=\"button\">OFF</button></a></p>");
            } else {
              client.println("<p><a href=\"/3/off\"><button class=\"button button2\">ON</button></a></p>");
            }

            client.println("<form action=\" / action_page.php\">");
            client.println("Time off (between 16 and 23):");
            client.println("<input type=\"number\" name=\"Switch off Time\" min=\"16\" max=\"23\">");
            client.println("<input type=\"submit\">");
            client.println("</form>");

            client.println("</body></html>");

            // The HTTP response ends with another blank line
            client.println();
            // Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    // Clear the header variable
    header = "";
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}
//-----------------------------------------------------------------
void read_eeprom_value(int address) {

  EEPROM.begin(eeprom_size);
  int value = EEPROM.read(address);
  EEPROM.end();
  Serial.print("EEprom: ");
  Serial.print(address);
  Serial.print(" = ");
  Serial.println(value);
}
//-----------------------------------------------------------------
void write_eeprom_value(int address) {

  EEPROM.begin(eeprom_size);
  int value;
  EEPROM.write(address, value);
  EEPROM.end();
}
//-----------------------------------------------------------------
void clear_eeprom_value(int address) {

  EEPROM.begin(eeprom_size);
  EEPROM.write(address, 0);
  EEPROM.end();
}
//-----------------------------------------------------------------
