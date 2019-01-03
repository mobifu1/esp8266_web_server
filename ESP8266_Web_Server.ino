/*
  Web-Server to switch 2 relais by sunset / sunrise or manual via Web-Site

  How it works:

  Importand Information: Arduino IDE > Arduino/Tools/Erase Flash:All Flash Contents !!!
  After flashing you can send variables by the same serial-comport in to the eeprom of esp8622

  Commands:
  ssid=xxxxx
  passwort=xxxxx
  servername=website name           show on the website
  button1=false/true                show on the website
  button2=false/true                show on the website
  debuging=false/true               serial debuging informations
  invert_gpio=false/true            invert the output signal for each relay-boards
  config -get                       show all stored variable on serial port
  img_src=default                   https://www.timeanddate.com/scripts/sunmap.php
  ip -get                           local IP

  after setting changes (ssid or password), you have to restart the device.
*/

#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <Ticker.h>
#include <simpleDSTadjust.h>
#include <sundata.h>

// -------------- Configuration Options -----------------

//Update time from NTP server every 5 hours
#define NTP_UPDATE_INTERVAL_SEC 5*3600

// Maximum of 3 servers
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov", "us.pool.ntp.org"

//Central European Time Zone (Paris, Berlin)
#define timezone 1 // +1 hour = Central European Time Zone
//Last > Sunday > March > 02:00 > +3600Sec(2Hours)
struct dstRule StartRule = {"CEST", Last, Sun, Mar, 2, 3600};    // Daylight time = UTC/GMT +2 hours
struct dstRule EndRule = {"CET", Last, Sun, Oct, 3, 1800};       // Standard time = UTC/GMT +1 hour

char* ssid = "ssid------------";
char* password = "password------------";

// Set web server port number to 80
WiFiServer server(80);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
boolean output1_state;
boolean output2_state;
boolean input1_state;
boolean invert_gpio;

//Buttons web site activation
boolean button1;
boolean button2;

boolean auto_switch_by_sun_down = false;
float auto_switch_off_hour_min = 16; //16:00 Uhr local time
float auto_switch_off_hour_max = 24; //23:59 Uhr local time
int auto_switch_off_hour;
int auto_switch_off_minute;

boolean auto_switch_by_sun_up = false;
float auto_switch_on_hour_min = 0;  //00:00 Uhr local time
float auto_switch_on_hour_max  = 9; //09:00 Uhr local time
int auto_switch_on_hour;
int auto_switch_on_minute;

//Output variables to GPIO pins, depending on used hardware
const int output1 = 0;    //GPIO 0  Board:ESP8266-01
const int output2 = 2;    //GPIO 2  Board:ESP8266-01 > Pin also used for UART Flash-Mode
//const int input1 = 0;   //GPIO 2  Board:Sonoff S20 > Button > pressed = HIGH-Level
//const int output1 = 12; //GPIO 12 Board:Sonoff S20 > Relais
//const int output2 = 13; //GPIO 13 Board:Sonoff S20 > LED

//EEprom statements
const int eeprom_size = 256 ; //Size can be anywhere between 4 and 4096 bytes

int auto_switch_by_sun_down_eeprom_address = 0;//boolean value
int auto_switch_by_sun_up_eeprom_address = 1;//boolean value
int debuging_eeprom_address = 2;        //boolean value
int button1_eeprom_address = 3;         //boolean value
int button2_eeprom_address = 4;         //boolean value
int invert_gpio_eeprom_address = 5;     //boolean value
int auto_switch_off_hour_eeprom_address = 6;//int value
int auto_switch_off_minute_eeprom_address = 8;//int value
int auto_switch_on_hour_eeprom_address = 10;//int value
int auto_switch_on_minute_eeprom_address = 12;//int value
int ssid_eeprom_address = 16;//string max 22
int password_eeprom_address = 40;//string max 32
int web_server_name_eeprom_address = 80;//string max 32
int img_src_eeprom_address = 120;//string max 96

String serial_line_0;//read bytes from serial port 0
//-----------------------------------------------------------------

Ticker ticker1;
int32_t tick;

bool readyForNtpUpdate = false;// flag changed in the ticker function to start NTP Update
bool ntp_is_allready_set = false;
simpleDSTadjust dstAdjusted(StartRule, EndRule);// Setup simpleDSTadjust Library rules

float lat = 53.48;//my home location
float lon = 10.23;//needed for exact local sunrise / sunset

int year_ ;
int month_ ;
int day_ ;
int hour_ ;
int minute_ ;
int second_ ;
boolean weekend;

int time_diff_to_greenwich; // add to UTC > hour 1=winter  2=sommer (timezone: Berlin,Paris)
String am_pm = "";          //AM / PM
String european_time = "";  //CET / CEST
String sunrise_string = ""; //build for the website
String sunset_string = "";  //build for the website
String time_string = "";    //build for the website
String date_string = "";    //build for the website
String auto_switch_on_string = "";
String auto_switch_off_string = "";
String sun_psition = "";
String day_string = "";

String web_server_name = "";
boolean debuging = false;
const String weekdays[7] = {"Thursday", "Friday", "Saturday", "Sunday", "Monday", "Tuesday", "Wednesday" };
String img_src = "";
#define img_src_default F("https://www.timeanddate.com/scripts/sunmap.php")
#define versionsname F("(v1.6-r)")
#define default_servername F("ESP8266")
#define html_border F("<p>----------------------------------------------------------------------------</p>")
#define lock_info F("<p>Locked by Auto Modus</p>")

//-----------------------------------------------------------------
void setup() {

  delay(2000);

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  //Load config
  load_config();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println(F("\nConnecting to WiFi"));
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("."));
    read_serial_port_0();
    delay(1000);
  }

  Serial.println(F("\nDone"));
  updateNTP();  //Init the NTP time
  printTime(0); //print initial time now.

  tick = NTP_UPDATE_INTERVAL_SEC; //Init the NTP update countdown ticker
  ticker1.attach(1, secTicker);   //Run a 1 second interval Ticker
  if (debuging == true) Serial.print(F("Next NTP Update: "));
  printTime(tick);

  server.begin();//Web-Server
}
//-----------------------------------------------------------------
void loop() {

  read_serial_port_0();
  read_input_pin();
  website();

  if (readyForNtpUpdate) {

    readyForNtpUpdate = false;

    printTime(0);
    updateNTP();
    if (debuging == true) Serial.print(F("\nUpdated time from NTP Server: "));
    printTime(0);
    if (debuging == true) Serial.print(F("Next NTP Update: "));
    printTime(tick);
  }

  delay(100);  //To reduce upload failures
}
//-----------------------------------------------------------------
//----------------------- Functions -------------------------------
//-----------------------------------------------------------------
void secTicker() {//NTP timer update ticker

  tick--;
  if (tick <= 0) {

    readyForNtpUpdate = true;
    tick = NTP_UPDATE_INTERVAL_SEC; // Re-arm
  }

  printTime(0);  //Uncomment if you want to see time printed every second
}
//-----------------------------------------------------------------
void updateNTP() {

  ntp_is_allready_set = false;

  configTime(timezone * 3600, 0, NTP_SERVERS);

  delay(500);
  while (!time(nullptr)) {
    if (debuging == true) Serial.print(F("#"));
    delay(1000);
  }

  ntp_is_allready_set = true;
}
//-----------------------------------------------------------------
void printTime(time_t offset) {

  char buf[30];
  char *dstAbbrev;
  time_t t = dstAdjusted.time(&dstAbbrev) + offset;
  struct tm *timeinfo = localtime (&t);

  int hour = (timeinfo->tm_hour + 11) % 12 + 1; // take care of noon and midnight
  sprintf(buf, "%02d/%02d/%04d %02d:%02d:%02d%s %s\n", timeinfo->tm_mon + 1, timeinfo->tm_mday, timeinfo->tm_year + 1900, hour, timeinfo->tm_min, timeinfo->tm_sec, timeinfo->tm_hour >= 12 ? "pm" : "am", dstAbbrev);
  if (debuging == true) Serial.print(buf);
  String line = buf;
  time_split_parameter (line);
  sunrise (lat, lon, time_diff_to_greenwich);//Hamburg 53,5° 10,0°
  day_string = weekdays[day_of_week (t)];
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

  if (am_pm == "am") {
    if (hour_ == 12) hour_ -= 12;
  }

  if (european_time == "CET") time_diff_to_greenwich = 1;
  if (european_time == "CEST") time_diff_to_greenwich = 2;

  String lead_zero_hour = ("");
  if (hour_ < 10) {
    lead_zero_hour = ("0");
  }
  String lead_zero_minute = ("");
  if (minute_ < 10) {
    lead_zero_minute = ("0");
  }
  String lead_zero_second = ("");
  if (second_ < 10) {
    lead_zero_second = ("0");
  }
  String lead_zero_day = ("");
  if (day_ < 10) {
    lead_zero_day = ("0");
  }
  String lead_zero_month = ("");
  if (month_ < 10) {
    lead_zero_month = ("0");
  }

  time_string = "Time: " + lead_zero_hour + String(hour_) + ":" + lead_zero_minute + String(minute_) + ":" + lead_zero_second + String(second_) + " " + european_time;
  date_string = "Date: " + lead_zero_day + String(day_) + "." + lead_zero_month + String(month_) + "." + String(year_);
}
//-----------------------------------------------------------------
void sunrise( float latitude , float longitude , int time_diff_to_greenwich) {

  sundata sun = sundata(latitude, longitude, time_diff_to_greenwich); //creat object with latitude and longtitude declared in degrees and time difference from Greenwhich

  sun.time( year_, month_, day_, hour_, minute_, second_); //insert year, month, day, hour, minutes and seconds
  sun.calculations();                                      //update calculations for last inserted time

  //float sun_el_rad = sun.elevation_rad();                //store sun's elevation in rads
  float sun_el_deg = sun.elevation_deg();                  //store sun's elevation in degrees
  //Serial.println(String(el_deg) + "Elevation");

  //float sun_az_rad = sun.azimuth_rad();                  //store sun's azimuth in rads
  float az_deg = sun.azimuth_deg();                        //store sun's azimuth in degrees
  //Serial.println(String(az_deg) + "Azimuth");

  float sunrise = sun.sunrise_time();                      //store sunrise time in decimal form
  //Serial.println(String(sunrise) + "Sunrise");
  //sunrise = (sunrise - 0.141666667); //correction factor -8,5 min > Sundata.h calculates unexact
  int sunrise_hour = int(sunrise);
  int sunrise_minute = int((sunrise - sunrise_hour) * 60);

  float sunset = sun.sunset_time();                        //store sunset time in decimal form > 15:30 = 15,5
  //Serial.println(String(sunset) + "Sunset");
  //sunset = (sunset + 0.1625); //correction factor +9,75 min > Sundata.h calculates unexact
  int sundown_hour = int(sunset);
  int sundown_minute = int((sunset - sundown_hour) * 60);

  //Calculation daylight
  float time_now = float(hour_) + (float(minute_) / 60);

  if (auto_switch_by_sun_down == true || auto_switch_by_sun_up == true) {

    boolean auto_power_on = false;
    float time_off = float(auto_switch_off_hour) + (float(auto_switch_off_minute) / 60);
    float time_on = float(auto_switch_on_hour) + (float(auto_switch_on_minute) / 60);

    if (auto_switch_by_sun_down == true) {
      if (time_off >= auto_switch_off_hour_min && time_now <= auto_switch_off_hour_max) {// 16 & 23
        if (time_now >= sunset) {
          if (time_now < time_off) {
            if (debuging == true) Serial.println(F("Power on / sun down"));
            auto_power_on = true;
          }
        }
      }
    }

    if (auto_switch_by_sun_up == true) {
      if (time_on >= auto_switch_on_hour_min && time_now <= auto_switch_on_hour_max) {// 5 & 9
        if (time_now < sunrise) {
          if (time_now >= time_on) {
            if (weekend == false) {
              if (debuging == true) Serial.println(F("Power on / sun up"));
              auto_power_on = true;
            }
          }
        }
      }
    }

    if (auto_power_on == true) {
      if (ntp_is_allready_set == true) {
        set_gpio_pins(1, true);
        set_gpio_pins(2, true);
      }
      else {
        if (debuging == true) Serial.println(F("Cant switch while NTP is not allready set !"));
      }

    }
    else {
      if (ntp_is_allready_set == true) {
        set_gpio_pins(1, false);
        set_gpio_pins(2, false);
      }
      else {
        if (debuging == true) Serial.println(F("Cant switch while NTP is not allready set !"));
      }
    }
  }

  //Format time to leading zeros:
  sunrise_string = "Sunrise: ";
  if (sunrise_hour < 10) sunrise_string += "0";
  sunrise_string += (String(sunrise_hour) + ":");
  if (sunrise_minute < 10) sunrise_string += "0";
  sunrise_string += String(sunrise_minute);
  //Serial.println(sunrise_string);

  //Format time to leading zeros:
  sunset_string = "Sunset: ";
  if (sundown_hour < 10) sunset_string += "0";
  sunset_string += (String(sundown_hour) + ":");
  if (sundown_minute < 10) sunset_string += "0";
  sunset_string += String(sundown_minute);
  //Serial.println(sunset_string);

  //Format the sun position
  sun_psition = ("Sun: Azimuth: " + String(az_deg) + " deg / Elevation: " + String(sun_el_deg) + " deg / ");
  if (sun_el_deg >= 0)sun_psition += F("Daylight");
  if (sun_el_deg >= -6 && sun_el_deg < 0 )sun_psition += F("Twilight");
  if (sun_el_deg >= -12 && sun_el_deg < -6 )sun_psition += F("Astronomical-Twilight");
  if (sun_el_deg < -12)sun_psition += F("Night");
}
//-----------------------------------------------------------------
void website() {

  WiFiClient client = server.available();   // Listen for incoming clients

  if (client) {                             // If a new client connects,
    if (debuging == true) Serial.println(F("New Client."));          // print a message out in the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        if (debuging == true) Serial.write(c);                    // print it out the serial monitor
        header += c;
        if (c == '\n') {                    // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            //HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            //and a content-type so the client knows what's coming, then a blank line:
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-type:text/html"));
            client.println(F("Connection: close"));
            client.println();

            //Handle the incoming information from the website user
            if (header.indexOf(F("GET /2/on")) >= 0) {//button2
              if (auto_switch_by_sun_down == false && auto_switch_by_sun_up == false) {
                set_gpio_pins(2, true);
              }

            } else if (header.indexOf(F("GET /2/off")) >= 0) {//button2
              if (auto_switch_by_sun_down == false && auto_switch_by_sun_up == false) {
                set_gpio_pins(2, false);
              }

            } else if (header.indexOf(F("GET /1/on")) >= 0) {//button1
              if (auto_switch_by_sun_down == false && auto_switch_by_sun_up == false) {
                set_gpio_pins(1, true);
              }

            } else if (header.indexOf(F("GET /1/off")) >= 0) {//button1
              if (auto_switch_by_sun_down == false && auto_switch_by_sun_up == false) {
                set_gpio_pins(1, false);
              }

            } else if (header.indexOf(F("GET /3/off")) >= 0) {//button3
              write_eeprom_bool(auto_switch_by_sun_down_eeprom_address, false);
              load_config();
              if (debuging == true) Serial.println(F("Auto Modus 1 off"));

            } else if (header.indexOf(F("GET /3/on")) >= 0) {//button3
              write_eeprom_bool(auto_switch_by_sun_down_eeprom_address, true);
              load_config();
              if (debuging == true) Serial.println(F("Auto Modus 1 on"));

            } else if (header.indexOf(F("Switch_off_Time=")) >= 0) {  //GET /%20action_page.php?Switch+on+Time=21%3A11 HTTP/1.1
              int index = header.indexOf(F("="));
              index += 1;
              int value_0 = (header.substring(index, index + 2)).toInt();
              int value_1 = (header.substring(index + 5, index + 7)).toInt();
              float input_time = float(value_0) + (float(value_1) / 60);
              if (input_time >= auto_switch_off_hour_min && input_time <= auto_switch_off_hour_max) {
                write_eeprom_int(auto_switch_off_hour_eeprom_address, value_0);
                write_eeprom_int(auto_switch_off_minute_eeprom_address, value_1);
                load_config();
                if (debuging == true) Serial.println("Set Switch off Time to: " + String(auto_switch_off_hour) + ":" + String(auto_switch_off_minute));
              }

            } else if (header.indexOf(F("GET /4/off")) >= 0) {//button4
              write_eeprom_bool(auto_switch_by_sun_up_eeprom_address, false);
              load_config();
              if (debuging == true) Serial.println(F("Auto Modus 2 off"));

            } else if (header.indexOf(F("GET /4/on")) >= 0) {//button4
              write_eeprom_bool(auto_switch_by_sun_up_eeprom_address, true);
              load_config();
              if (debuging == true) Serial.println(F("Auto Modus 2 on"));

            } else if (header.indexOf(F("Switch_on_Time=")) >= 0) {  //GET /%20action_page.php?Switch+on+Time=05:30 HTTP/1.1
              int index = header.indexOf(F("="));
              index += 1;
              int value_0 = (header.substring(index, index + 2)).toInt();
              int value_1 = (header.substring(index + 5, index + 7)).toInt();
              float input_time = float(value_0) + (float(value_1) / 60);
              if (input_time >= auto_switch_on_hour_min && input_time <= auto_switch_on_hour_max) {
                write_eeprom_int(auto_switch_on_hour_eeprom_address, value_0);
                write_eeprom_int(auto_switch_on_minute_eeprom_address, value_1);
                load_config();
                if (debuging == true) Serial.println("Set Switch on Time to: " + String(auto_switch_on_hour) + ":" + String(auto_switch_on_minute));
              }
            }

            //Display the HTML web page
            client.println(F("<!DOCTYPE html><html>"));
            client.println("<html><head><title>" + web_server_name + "</title></head><body>");
            client.println(F("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"));
            client.println(F("<link rel=\"icon\" href=\"data:,\">"));

            //CSS to style the on/off buttons
            client.println(F("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}"));
            client.println(F(".button { background-color: #195B6A; border: none; color: white; padding: 16px 40px;"));
            client.println(F("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}"));
            client.println(F(".button2 {background-color: #77878A;}</style></head>"));
            client.println(F("<meta http-equiv=\"refresh\" content=\"30\">\r\n"));

            //Web Page Heading
            client.println("<body><h1>" + web_server_name + " " + versionsname + "</h1>");


            //button 1
            if (button1 == true) {
              client.println(html_border);
              client.println(F("<p>Button 1</p>"));
              if (output1_state == false) {
                client.println(F("<p><a href=\"/1/on\"><button class=\"button\">OFF</button></a></p>"));
              } else {
                client.println(F("<p><a href=\"/1/off\"><button class=\"button button2\">ON</button></a></p>"));
              }
              if (auto_switch_by_sun_down == true || auto_switch_by_sun_up == true) {
                client.println(lock_info);
              }
            }

            //button 2
            if (button2 == true) {
              client.println(html_border);
              client.println(F("<p>Button 2</p>"));
              if (output2_state == false) {
                client.println(F("<p><a href=\"/2/on\"><button class=\"button\">OFF</button></a></p>"));
              } else {
                client.println(F("<p><a href=\"/2/off\"><button class=\"button button2\">ON</button></a></p>"));
              }
              if (auto_switch_by_sun_down == true || auto_switch_by_sun_up == true) {
                client.println(lock_info);
              }
            }

            //time and sunset , sunrise informations
            client.println(html_border);
            client.println("<p>" + day_string + " / " + time_string + " / " + date_string + "</p>");
            client.println("<p>" + sunrise_string + " / " + sunset_string + "</p>");
            client.println("<p>" + sun_psition + "</p>");

            //image
            client.println(F("<wbr>"));
            client.print(F("<img src=\""));
            client.print(img_src);
            client.println(F("\" alt=\"(img-url not reachable)\" width=\"571\" height=\"300\">"));

            //auto switch on button (by sun set)
            client.println(html_border);
            client.println("<p>Auto switch on at Sunset / Auto switch off at: " + auto_switch_off_string + "</p>");

            if (auto_switch_by_sun_down == false) { //button for auto_switch_by_sun
              client.println(F("<p><a href=\"/3/on\"><button class=\"button\">OFF</button></a></p>"));
            } else {
              client.println(F("<p><a href=\"/3/off\"><button class=\"button button2\">ON</button></a></p>"));
            }

            //inputform to define the auto switch off time
            client.println(F("<form action=\"/action_page.php\">"));
            client.println(F("Time off (between 16:00 and 23:59):"));
            client.println(F("<input type=\"time\" name=\"Switch_off_Time\">"));
            client.println(F("<input type=\"submit\">"));
            client.println(F("</form>"));

            //auto switch off button (by sun rise)
            client.println(html_border);
            client.println("<p>Auto switch on at: "  + auto_switch_on_string + " / Auto switch off at Sunrise" + "</p>");

            //auto switch on button (by sun rise)
            if (auto_switch_by_sun_up == false) { //button for auto_switch_by_sun
              client.println(F("<p><a href=\"/4/on\"><button class=\"button\">OFF</button></a></p>"));
            } else {
              client.println(F("<p><a href=\"/4/off\"><button class=\"button button2\">ON</button></a></p>"));
            }
            if (weekend == true) {
              client.println("<p>Weekend Quiet Modus On</p>");
            }
            //inputform to define the auto switch on time
            client.println(F("<form action=\"/action_page.php\">"));
            client.println(F("Time on (between 00:00 and 09:00):"));
            client.println(F("<input type=\"time\" name=\"Switch_on_Time\">"));
            client.println(F("<input type=\"submit\">"));
            client.println(F("</form>"));

            client.println(html_border);

            if (ntp_is_allready_set == true) client.print(F("<p>NTP OK / "));
            if (ntp_is_allready_set == false) client.print(F("<p>NTP ? / "));

            client.print(F("Next NTP-Update in: "));
            if (tick > 60) {
              client.print(String(tick / 60));
              client.println(F(" Minutes</p>"));
            }
            else {
              client.print(String(tick));
              client.println(F(" Seconds</p>"));
            }
            client.println(F("</body></html>"));

            //The HTTP response ends with another blank line
            client.println();
            //Break out of the while loop
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
        }
      }
    }
    //Clear the header variable
    header = "";

    //Close the connection
    client.stop();
    if (debuging == true) Serial.println(F("Client disconnected."));
    if (debuging == true) Serial.println("");
  }
}
//-----------------------------------------------------------------
int day_of_week (long epoch) {

  long day = epoch / 86400L;
  int day_of_the_week = day % 7;

  if (day_of_the_week == 2 || day_of_the_week == 3) {
    weekend = true;
  }
  else {
    weekend = false;
  }

  if (debuging == true) {
    Serial.println("Dow:" + String(day_of_the_week));
    Serial.println("Wed:" + String(weekend));
  }

  //Since January 1, 1970 was a Thursday the results are:
  //0=Thursday
  //1=Friday
  //2=Saturday
  //3=Sunday
  //4=Monday
  //5=Tuesday
  //6=Wednesday

  return day_of_the_week;
}
//-----------------------------------------------------------------
void write_eeprom_string(int address, String value) {

  EEPROM.begin(eeprom_size);
  int len = value.length();
  len++;
  if (len < 32) {
    int address_end = address + len;
    char buf[len];
    byte count = 0;
    value.toCharArray(buf, len);
    for (int i = address ; i < address_end ; i++) {
      EEPROM.write(i, buf[count]);
      count++;
    }
  }
  EEPROM.end();
}
//-----------------------------------------------------------------
void write_eeprom_long(int address, long value) {

  EEPROM.begin(eeprom_size);
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
  EEPROM.end();
}
//-----------------------------------------------------------------
void write_eeprom_int(int address, int value) {

  EEPROM.begin(eeprom_size);
  EEPROM.write(address, highByte(value));
  EEPROM.write(address + 1, lowByte(value));
  EEPROM.end();
}
//-----------------------------------------------------------------
void write_eeprom_byte(int address, byte value) {

  EEPROM.begin(eeprom_size);
  //EEPROM.write(address, value);
  EEPROM.write(address, value);
  EEPROM.end();
}
//-----------------------------------------------------------------
void write_eeprom_bool(int address, boolean value) {

  EEPROM.begin(eeprom_size);
  //EEPROM.write(address, value);
  if (value == true)EEPROM.write(address, 1);
  if (value == false)EEPROM.write(address, 0);
  EEPROM.end();
}
//-----------------------------------------------------------------
String read_eeprom_string(int address) {

  EEPROM.begin(eeprom_size);
  String value;
  byte count = 0;
  char buf[32];
  for (int i = address ; i < (address + 31) ; i++) {
    buf[count] = EEPROM.read(i);
    if (buf[count] == 0) break; //endmark of string
    value += buf[count];
    count++;
  }
  EEPROM.end();
  return value;
}
//-----------------------------------------------------------------
long read_eeprom_long(int address) {

  EEPROM.begin(eeprom_size);
  long four = EEPROM.read(address);
  long thre = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);
  EEPROM.end();
  return ((four << 0) & 0xFF) + ((thre << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}
//-----------------------------------------------------------------
int read_eeprom_int(int address) {

  EEPROM.begin(eeprom_size);
  int value;
  int value_1;
  value = EEPROM.read(address); //highByte(value));
  value = value << 8;
  value_1 = EEPROM.read(address + 1); //lowByte(value));
  value = value + value_1;
  EEPROM.end();
  return value;
}
//-----------------------------------------------------------------
byte read_eeprom_byte(int address) {

  EEPROM.begin(eeprom_size);
  byte value;
  value = EEPROM.read(address);
  EEPROM.end();
  return value;
}
//-----------------------------------------------------------------
boolean read_eeprom_bool(int address) {

  EEPROM.begin(eeprom_size);
  byte value;
  boolean bool_value;
  value = EEPROM.read(address);
  if (value == 1)bool_value = true;
  if (value != 1)bool_value = false;
  return bool_value;
  EEPROM.end();
}
//-----------------------------------------------------------------
void read_input_pin() { // needed for sonoff S20 > Switch Button

  //  int val = digitalRead(input1);
  //
  //  if (auto_switch_by_sun_down == false && auto_switch_by_sun_up == false) {
  //    if (val == LOW) {
  //      if (input1_state == true) {
  //        input1_state = false;
  //        set_gpio_pins(1, false);
  //        delay(500);
  //      }
  //      else {
  //        input1_state = true;
  //        set_gpio_pins(1, true);
  //        delay(500);
  //      }
  //    }
  //  }
}
//-----------------------------------------------------------------
void set_gpio_pins(int gpio, boolean state) {

  if (button1 == true) {
    if (gpio == 1 && state == false) {
      output1_state = state;
      if (invert_gpio == false) digitalWrite(output1, LOW);
      if (invert_gpio == true) digitalWrite(output1, HIGH);
    }

    if (gpio == 1 && state == true) {
      output1_state = state;
      if (invert_gpio == false) digitalWrite(output1, HIGH);
      if (invert_gpio == true) digitalWrite(output1, LOW);
    }
  }

  if (button2 == true) {
    if (gpio == 2 && state == false) {
      output2_state = state;
      if (invert_gpio == false) digitalWrite(output2, LOW);
      if (invert_gpio == true) digitalWrite(output2, HIGH);
    }

    if (gpio == 2 && state == true) {
      output2_state = state;
      if (invert_gpio == false) digitalWrite(output2, HIGH);
      if (invert_gpio == true) digitalWrite(output2, LOW);
    }
  }
}
//-----------------------------------------------------------------
void load_config() {

  Serial.println(F("config load:"));

  debuging = read_eeprom_bool(debuging_eeprom_address);
  Serial.println("debuging=" + String(debuging));

  String value = "";
  value = read_eeprom_string(ssid_eeprom_address);
  strcpy(ssid, value.c_str());
  Serial.println("ssid=" + String(ssid));
  value = read_eeprom_string(password_eeprom_address);
  strcpy(password, value.c_str());
  Serial.println("password=" + String(password));

  auto_switch_by_sun_down = read_eeprom_bool(auto_switch_by_sun_down_eeprom_address);
  auto_switch_by_sun_up = read_eeprom_bool(auto_switch_by_sun_up_eeprom_address);

  auto_switch_off_hour = read_eeprom_int(auto_switch_off_hour_eeprom_address);
  auto_switch_off_minute = read_eeprom_int(auto_switch_off_minute_eeprom_address);

  if (auto_switch_off_hour == 65535) { //format the dirt
    write_eeprom_int(auto_switch_off_hour_eeprom_address, auto_switch_off_hour_max - 1); //default value
    write_eeprom_int(auto_switch_off_minute_eeprom_address, 0);
    auto_switch_off_hour = read_eeprom_int(auto_switch_off_hour_eeprom_address);
    auto_switch_off_minute = read_eeprom_int(auto_switch_off_minute_eeprom_address);
  }

  auto_switch_on_hour = read_eeprom_int(auto_switch_on_hour_eeprom_address);
  auto_switch_on_minute = read_eeprom_int(auto_switch_on_minute_eeprom_address);

  if (auto_switch_on_hour == 65535) { //format the dirt
    write_eeprom_int(auto_switch_on_hour_eeprom_address, auto_switch_on_hour_min + 1);//default value
    write_eeprom_int(auto_switch_on_minute_eeprom_address, 0);
    auto_switch_on_hour = read_eeprom_int(auto_switch_on_hour_eeprom_address);
    auto_switch_on_minute = read_eeprom_int(auto_switch_on_minute_eeprom_address);
  }

  //Format time to leading zeros:
  auto_switch_off_string = "";
  if (auto_switch_off_hour < 10) auto_switch_off_string += "0";
  auto_switch_off_string += (String(auto_switch_off_hour) + ":");
  if (auto_switch_off_minute < 10) auto_switch_off_string += "0";
  auto_switch_off_string += String(auto_switch_off_minute);

  //Format time to leading zeros:
  auto_switch_on_string = "";
  if (auto_switch_on_hour < 10) auto_switch_on_string += "0";
  auto_switch_on_string += (String(auto_switch_on_hour) + ":");
  if (auto_switch_on_minute < 10) auto_switch_on_string += "0";
  auto_switch_on_string += String(auto_switch_on_minute);

  //load button1 value
  button1 = read_eeprom_bool(button1_eeprom_address);
  if (button1 == true) {
    pinMode(output1, OUTPUT);   //needed for using relais to switch power
    digitalWrite(output1, LOW); //Set outputs to LOW
  }
  else {
    pinMode(output1, INPUT);  //needed for using sonoff with connected hardware
  }
  Serial.println("button1=" + String(button1));

  //load button2 value
  button2 = read_eeprom_bool(button2_eeprom_address);
  if (button2 == true) {
    pinMode(output2, OUTPUT);   //needed for using relais to switch power
    digitalWrite(output2, LOW); //Set outputs to LOW
  }
  else {
    pinMode(output2, INPUT); //needed for using sonoff with connected hardware
  }
  Serial.println("button2=" + String(button2));

  web_server_name = read_eeprom_string(web_server_name_eeprom_address);
  char searchChar = 255;
  if (web_server_name.indexOf(searchChar) >= 0) { //format the dirt
    write_eeprom_string(web_server_name_eeprom_address, default_servername );//default
    web_server_name = read_eeprom_string(web_server_name_eeprom_address);
  }
  Serial.println("servername=" + web_server_name);

  invert_gpio = read_eeprom_bool(invert_gpio_eeprom_address);
  Serial.println("invert_gpio=" + String(invert_gpio));

  img_src = read_eeprom_string(img_src_eeprom_address);
  if (img_src == "default")img_src = img_src_default;
  Serial.println("img_src=" + String(img_src));
}
//-----------------------------------------------------------------
void lookup_commands() {

  int length_ = serial_line_0.length();
  length_ -= 1;

  if (serial_line_0.substring(0, 5) == F("ssid=")) {
    write_eeprom_string(ssid_eeprom_address, serial_line_0.substring(5, length_));
    Serial.println(serial_line_0.substring(0, 5) + serial_line_0.substring(5, length_));
    load_config();
  }

  if (serial_line_0.substring(0, 9) == F("password=")) {
    write_eeprom_string(password_eeprom_address, serial_line_0.substring(9, length_));
    Serial.println(serial_line_0.substring(0, 9) + serial_line_0.substring(9, length_));
    load_config();
  }

  if (serial_line_0.substring(0, 11) == F("servername=")) {
    write_eeprom_string(web_server_name_eeprom_address, serial_line_0.substring(11, length_));
    Serial.println(serial_line_0.substring(0, 11) + serial_line_0.substring(11, length_));
    load_config();
  }

  if (serial_line_0.substring(0, 9) == F("debuging=")) {
    if (serial_line_0.substring(9, length_) == "false") {
      write_eeprom_bool(debuging_eeprom_address, false);
      Serial.println(serial_line_0.substring(0, 9) + serial_line_0.substring(9, length_));
      load_config();
    }
    if (serial_line_0.substring(9, length_) == "true") {
      write_eeprom_bool(debuging_eeprom_address, true);
      Serial.println(serial_line_0.substring(0, 9) + serial_line_0.substring(9, length_));
      load_config();
    }
  }

  if (serial_line_0.substring(0, 8) == F("button1=")) {
    if (serial_line_0.substring(8, length_) == "false") {
      write_eeprom_bool(button1_eeprom_address, false);
      Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
      load_config();
    }
    if (serial_line_0.substring(8, length_) == "true") {
      write_eeprom_bool(button1_eeprom_address, true);
      Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
      load_config();
    }
  }

  if (serial_line_0.substring(0, 8) == F("button2=")) {
    if (serial_line_0.substring(8, length_) == "false") {
      write_eeprom_bool(button2_eeprom_address, false);
      Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
      load_config();
    }
    if (serial_line_0.substring(8, length_) == "true") {
      write_eeprom_bool(button2_eeprom_address, true);
      Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
      load_config();
    }
  }

  if (serial_line_0.substring(0, 12) == F("invert_gpio=")) {
    if (serial_line_0.substring(12, length_) == "false") {
      write_eeprom_bool(invert_gpio_eeprom_address, false);
      Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
      load_config();
    }
    if (serial_line_0.substring(12, length_) == "true") {
      write_eeprom_bool(invert_gpio_eeprom_address, true);
      Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
      load_config();
    }
  }

  if (serial_line_0.substring(0, 8) == F("img_src=")) {
    write_eeprom_string(img_src_eeprom_address, serial_line_0.substring(8, length_));
    Serial.println(serial_line_0.substring(0, 8) + serial_line_0.substring(8, length_));
    load_config();
  }

  if (serial_line_0.substring(0, 7) == F("ip -get")) {
    Serial.print(F("ip="));
    Serial.println(WiFi.localIP());
  }

  if (serial_line_0.substring(0, 11) == F("config -get")) {
    load_config();
  }
}
//-----------------------------------------------------------------
void read_serial_port_0() {

  if (Serial.available() > 0) {
    serial_line_0 = Serial.readStringUntil('\n');
    //Serial.println(serial_line_0);
    lookup_commands();
  }
}
//-----------------------------------------------------------------
