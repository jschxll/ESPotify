#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiClientSecureBearSSL.h>
#include <index.h>
#include <OLED.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <ArduinoJson.h>

#include <EEPROM.h>

#define SKIP_TRACK_BUTTON 14
#define PLAYBACK_BEHAVIOUR_BUTTON 12

const char *SSID = "YOUR WIFI SSID";
const char *PASSWD = "YOUR WIFI PASSWORD";

ESP8266WebServer server(80);
std::unique_ptr<BearSSL::WiFiClientSecure> client = std::make_unique<BearSSL::WiFiClientSecure>();
HTTPClient http;
String code = "";
long unsigned int token_expire_time;
int expires_counter;
String current_track_data = "";
String last_track_data = "";
String response;

// true if the access token was requested
bool got_access_token = false;

// true if current track is playing
bool is_playing = true;

// millis, to count the current progress of the track
int current_progress;

const char *track_ptr;
const char *artist_ptr;
const char *album_ptr;

String access_token;

void handle_not_found();
void handle_root();
void find_code_handler();
bool request_access_token(String &code);
bool request_refresh_token(const String &response);
void get_currently_playing_track();
String get_user_name();

// Declaration of the OLED display
U8G2_SH1106_128X64_NONAME_1_SW_I2C display(U8G2_R0, 5, 4, U8X8_PIN_NONE);
void print_to_display(const char *txt, int x, int y)
{
  display.clear();
  display.firstPage();
  do
  {
    display.setFont(u8g_font_6x10);
    display.drawStr(x, y, txt);
  } while (display.nextPage());
}

void print_to_display(const char *track_name, const char *album_name, const char *artist_name)
{
  display.clear();
  display.firstPage();
  display.setFont(u8g_font_6x10);
  String content = String(track_name) + " " + String(album_name) + " " + String(artist_name);
  do
  {
    display.drawUTF8(10, 10, track_name);
    display.drawUTF8(10, 20, artist_name);
    display.drawUTF8(10, 30, album_name);
  } while (display.nextPage());
}

void setup_server()
{
  if (!MDNS.begin("esp8266"))
    return;

  // Routing server
  server.on("/", handle_root);
  server.on("/callback", find_code_handler);
  server.onNotFound(handle_not_found);
  server.begin();
}

void handle_not_found()
{
  String message = "Something went wrong:(\nPlease retry\n\n";
  message += "URI: ";
  message += server.uri();

  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handle_root()
{
  server.send(200, "text/html", HOMEPAGE);
}

void setup()
{
  pinMode(SKIP_TRACK_BUTTON, INPUT);
  pinMode(PLAYBACK_BEHAVIOUR_BUTTON, INPUT);
  Serial.begin(9600);
  display.begin();
  display.enableUTF8Print();
  client->setInsecure();
  WiFi.begin(SSID, PASSWD);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
  String ip_addr = WiFi.localIP().toString();
  print_to_display(ip_addr.c_str(), (128 - ip_addr.length()) / 4, 32);
  setup_server();
}

void find_code_handler()
{
  // Search for the code section in the uri to extract the authorisation code
  String uri = server.arg("code");
  got_access_token = request_access_token(uri);

  // If the request was successfull the server send the user back to the homepage
  if (got_access_token)
    server.send(200, "text/html", SUCCESS_SITE);
}

bool is_valid_response(JsonDocument json)
{
  return json.containsKey("expires_in") &&
         json.containsKey("access_token") &&
         json.containsKey("refresh_token") &&
         json.containsKey("scope");
}

int str_width(String &str)
{
  return display.getUTF8Width(str.c_str());
}

bool request_access_token(String &code)
{
  String auth = "Basic " + base64::encode(String(CLIENT_ID) + ":" + String(CLIENT_SECRET));
  String requestBody = "grant_type=authorization_code&code=" + code + "&redirect_uri=" + REDIRECT_URL;
  String url = "https://accounts.spotify.com/api/token";
  http.begin(*client, url);
  http.addHeader("Authorization", auth);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int http_response_code = http.POST(requestBody);
  if (http_response_code == HTTP_CODE_OK)
  {
    JsonDocument json;
    DeserializationError error = deserializeJson(json, http.getString());
    if (error)
      return false;

    if (is_valid_response(json))
    {
      response = http.getString();
      expires_counter = json["expires_in"];
      access_token = json["access_token"].as<String>();

      // Print greetings when successfully connecting to Spotify
      String greeting = "Hello " + get_user_name() + "!";
      String info = "Music sleeping zzZZz";
      int xGreeting = (display.getDisplayWidth() - str_width(greeting)) / 2;
      int xInfo = (display.getDisplayWidth() - str_width(info)) / 2;
      int y = display.getDisplayHeight() / 2;
      display.firstPage();
      do
      {
        display.drawStr(xGreeting, y, greeting.c_str());
        display.drawStr(xInfo, 50, info.c_str());
      } while (display.nextPage());
      return true;
    }
  }
  else
    handle_not_found();

  http.end();
  return false;
}

bool request_refresh_token(const String &res)
{
  JsonDocument json_arr;
  deserializeJson(json_arr, res);
  if (json_arr.isNull() || !json_arr.containsKey("refresh_token") || json_arr.size() <= 1)
    return false;
  String refresh_token = json_arr["refresh_token"];
  String auth = "Basic " + base64::encode(String(CLIENT_ID) + ":" + String(CLIENT_SECRET));
  String requestBody = "grant_type=refresh_token&refresh_token=" + refresh_token;
  String url = "https://accounts.spotify.com/api/token";
  http.begin(*client, url);
  http.addHeader("Authorization", auth);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int http_response_code = http.POST(requestBody);
  if (http_response_code == HTTP_CODE_OK)
  {
    response = http.getString();
    token_expire_time = json_arr["expires_in"];
    access_token = json_arr["access_token"].as<String>();
    expires_counter = millis();
    return true;
  }

  http.end();
  return false;
}

String parseJsonValue(HTTPClient &http, String key)
{
  bool found = false;
  bool searched = true;
  String ret_str = "";

  int len = http.getSize();
  int index = 0;
  WiFiClient *stream = http.getStreamPtr();
  char buffer[1]; // Buffer to store read characters

  // Loop until the connection is closed or data is available
  while (http.connected() && (len > 0 || len == -1))
  {
    size_t size = stream->available();
    if (size)
    {
      size_t read_bytes = stream->readBytes(buffer, ((size > sizeof(buffer)) ? sizeof(buffer) : size));
      if (found)
      {
        if (buffer[0] != ':' && searched)
          continue;
        else if (buffer[0] != '\n')
        {
          if (buffer[0] == ':')
          {
            searched = false;
            int i = stream->readBytes(buffer, 1);
          }
          else
            ret_str += buffer[0];
        }
        else
          break;
      }
      else if (buffer[0] == key[0])
        index = 1;
      else if (buffer[0] == key[index])
      {
        index++;
        if (index == key.length())
          found = true;
      }
      else if (buffer[0] != key[index])
        index = 0;
    }
  }

  if (*(ret_str.end() - 1) == ',')
  {
    ret_str.substring(0, ret_str.length() - 1);
  }

  ret_str = ret_str.substring(1, ret_str.length() - 2);

  // If track info is larger than the display width, a slice of the info is shown on the display
  if (display.getStrWidth(ret_str.c_str()) > display.getDisplayWidth())
  {
    int desired_width = 95;
    int string_width = 0;
    int index = 0;
    String cutted_str = "";

    // As long as the pixel width of the string is smaller than the max size the cutted_str get more chars
    while (string_width <= desired_width)
    {
      cutted_str += ret_str[index];
      string_width = display.getStrWidth(cutted_str.c_str());
      index++;
    }
    cutted_str += "...";
    ret_str = cutted_str;
  }
  return ret_str;
}

String get_user_name()
{
  String user_name = "";

  if (!access_token.isEmpty())
  {
    String auth = "Bearer " + access_token;
    http.useHTTP10(true);
    http.begin(*client, "https://api.spotify.com/v1/me");
    http.addHeader("Authorization", auth);
    int status_code = http.GET();
    if (status_code != HTTP_CODE_OK)
      return "";
    user_name = parseJsonValue(http, "display_name");
  }
  return user_name;
}

void get_currently_playing_track()
{
  if (!access_token.isEmpty())
  {
    String auth = "Bearer " + String(access_token);
    http.useHTTP10(true);
    http.begin(*client, "https://api.spotify.com/v1/me/player/currently-playing");
    http.addHeader("Authorization", auth);
    int status_code = http.GET();
    if (status_code == HTTP_CODE_OK)
    {
      // Get track data from the currently playing track
      String artist_name = parseJsonValue(http, "name");
      String album_name = parseJsonValue(http, "name");
      String track_duration = parseJsonValue(http, "duration_ms");
      String track_name = parseJsonValue(http, "name");
      String track_progress = parseJsonValue(http, "progress_ms");

      // Check if current track is playing
      String is_playing_str = parseJsonValue(http, "is_playing");
      is_playing = is_playing_str == "true" ? true : false;

      track_ptr = track_name.c_str();
      artist_ptr = artist_name.c_str();
      album_ptr = album_name.c_str();

      current_track_data = track_name;
      if (!current_track_data.equals(last_track_data))
      {
        print_to_display(track_ptr, album_ptr, artist_ptr);
      }
      last_track_data = current_track_data;
    }
    http.end();
  }
}

void skip_track()
{
  if (!access_token.isEmpty())
  {
    String auth = "Bearer " + String(access_token);
    http.useHTTP10(true);
    http.begin(*client, "https://api.spotify.com/v1/me/player/next");
    http.addHeader("Authorization", auth);
    int status_code = http.POST("");
    if (status_code != HTTP_CODE_OK)
      return;
    http.end();
  }
}

void pause_playback()
{
  if (!access_token.isEmpty())
  {
    String auth = "Bearer " + String(access_token);
    http.useHTTP10(true);
    http.begin(*client, "https://api.spotify.com/v1/me/player/pause");
    http.addHeader("Authorization", auth);
    int status_code = http.PUT("");
    if (status_code != HTTP_CODE_OK)
      return;
    http.end();
  }
}

void resume_playback()
{
  if (!access_token.isEmpty())
  {
    String auth = "Bearer " + String(access_token);
    http.useHTTP10(true);
    http.begin(*client, "https://api.spotify.com/v1/me/player/play");
    http.addHeader("Authorization", auth);
    int status_code = http.PUT("");
    if (status_code != HTTP_CODE_OK)
      return;
    http.end();
  }
}

void loop()
{
  server.handleClient();
  if (got_access_token)
  {
    bool playback_behaviour_changed = digitalRead(PLAYBACK_BEHAVIOUR_BUTTON);
    if (playback_behaviour_changed && !is_playing)
      resume_playback();
    if (playback_behaviour_changed && is_playing)
      pause_playback();

    bool triggred_skip = digitalRead(SKIP_TRACK_BUTTON);
    if (triggred_skip)
      skip_track();
    get_currently_playing_track();

    if ((millis() - expires_counter) / 1000 >= token_expire_time - 60)
    {
      if (!request_refresh_token(response))
      {
        const char *error_msg = "Couldn't refresh access token";
        print_to_display(error_msg, display.getDisplayWidth() / 2, display.getDisplayHeight() / 2);
      }
    }
  }
}