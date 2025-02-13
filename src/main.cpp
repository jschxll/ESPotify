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

#define SKIP_TRACK_BUTTON 14
#define PLAYBACK_BEHAVIOUR_BUTTON 12

class DisplayView
{
private:
  const char *_albumName;
  const char *_trackName;
  const char *_artistName;
  bool _isPlaying;
  void draw_play_button(U8G2_SH1106_128X64_NONAME_1_SW_I2C &display, int x, int y, int size)
  {
    int half_size = size / 2;
    int x1 = x - half_size;
    int y1 = y - half_size;
    int y2 = y + half_size;

    display.drawTriangle(x1, y1, x1, y2, x + half_size, y);
  }
  void draw_pause_button(U8G2_SH1106_128X64_NONAME_1_SW_I2C &display, int x, int y, int width, int height)
  {
    int barSpacing = width;
    int half_width = width / 2;
    int half_height = height / 2;

    display.drawBox(x - barSpacing - half_width, y - half_height, width, height);
    display.drawBox(x + barSpacing - half_width, y - half_height, width, height);
  }

public:
  ~DisplayView()
  {
  }
  void set_album(const char *title)
  {
    _albumName = title;
  }
  void set_track(const char *trackName)
  {
    _trackName = trackName;
  }
  void set_artist(const char *artistName)
  {
    _artistName = artistName;
  }
  void is_playing(bool isPlaying)
  {
    _isPlaying = isPlaying;
  }
  bool getPlayingState()
  {
    return _isPlaying;
  }
  const char *get_track()
  {
    return _trackName ? _trackName : "";
  }
  static void init(U8G2_SH1106_128X64_NONAME_1_SW_I2C &display)
  {
    display.clear();
    display.firstPage();
    display.setFont(u8g_font_6x10);
  }
  void draw_music_view(U8G2_SH1106_128X64_NONAME_1_SW_I2C &display)
  {
    init(display);
    do
    {
      if (_albumName)
        display.drawUTF8(10, 30, _albumName);
      if (_trackName)
        display.drawUTF8(10, 10, _trackName);
      if (_artistName)
        display.drawUTF8(10, 20, _artistName);
      if (_isPlaying)
        draw_play_button(display, display.getDisplayWidth() / 2, 50, 15);
      else
        draw_pause_button(display, display.getDisplayWidth() / 2, 50, 5, 15);
    } while (display.nextPage());
  }
  static void draw_message(U8G2_SH1106_128X64_NONAME_1_SW_I2C &display, const char *txt, int x, int y)
  {
    init(display);
    do
    {
      display.drawStr(x, y, txt);
    } while (display.nextPage());
  }
};

class DisplayBuilder
{
private:
  DisplayView _displayView;

public:
  ~DisplayBuilder()
  {
  }
  DisplayBuilder()
  {
    _displayView = DisplayView();
  }

  DisplayBuilder &build_album(const char *title)
  {
    _displayView.set_album(title);
    return *this;
  }

  DisplayBuilder &build_track(const char *trackName)
  {
    _displayView.set_track(trackName);
    return *this;
  }

  DisplayBuilder &build_artist(const char *artist)
  {
    _displayView.set_artist(artist);
    return *this;
  }

  DisplayBuilder &build_play_stop_view(bool isPlaying)
  {
    _displayView.is_playing(isPlaying);
    return *this;
  }

  DisplayView get_view()
  {
    return _displayView;
  }
};

const char *SSID = "SSID";
const char *PASSWD = "WIFI PASSWORD";

ESP8266WebServer server(80);
std::unique_ptr<BearSSL::WiFiClientSecure> client = std::make_unique<BearSSL::WiFiClientSecure>();
HTTPClient http;
long unsigned int token_expire_time;
int expires_counter;
DisplayView current_view = DisplayView();
DisplayView previous_view = DisplayView();
String response;

// true if the access token was requested
bool got_access_token = false;

// true = playing; false = pause
bool lastState = true;

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
DisplayView view_builder = DisplayView();

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
  display.begin();
  display.enableUTF8Print();
  client->setInsecure();
  WiFi.begin(SSID, PASSWD);
  while (WiFi.status() != WL_CONNECTED)
    delay(500);
  String ip_addr = WiFi.localIP().toString();
  DisplayView::draw_message(display, ip_addr.c_str(), (128 - ip_addr.length()) / 4, 32);
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
  {
    handle_not_found();
  }

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

String parse_json_value(HTTPClient &http, String key)
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

  // If track info is larger than the display width, a slice of the information is shown on the display
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
    user_name = parse_json_value(http, "display_name");
  }
  return user_name;
}

void get_currently_playing_track(DisplayView &display_builder)
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
      String artist_name = parse_json_value(http, "name");
      String album_name = parse_json_value(http, "name");
      String track_duration = parse_json_value(http, "duration_ms");
      String track_name = parse_json_value(http, "name");
      String track_progress = parse_json_value(http, "progress_ms");

      current_view.set_track(track_name.c_str());
      if (strcmp(current_view.get_track(), previous_view.get_track()) != 0 || display_builder.getPlayingState() != previous_view.getPlayingState())
      {
        current_view = DisplayBuilder()
                           .build_track(current_view.get_track())
                           .build_album(album_name.c_str())
                           .build_artist(artist_name.c_str())
                           .build_play_stop_view(display_builder.getPlayingState())
                           .get_view();
        current_view.draw_music_view(display);
      }
      previous_view = current_view;
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
    static unsigned long lastDebounceTime = 0;
    const unsigned long debouncedDelay = 10; // 50
    static bool lastButtonState = LOW;
    bool playback_behaviour_changed = digitalRead(PLAYBACK_BEHAVIOUR_BUTTON);

    if (playback_behaviour_changed != lastButtonState)
      lastDebounceTime = millis();

    if ((millis() - lastDebounceTime) > debouncedDelay)
    {
      if (playback_behaviour_changed)
      {
        if (lastState)
        {
          resume_playback();
          view_builder.is_playing(false);
          lastState = false;
        }
        else
        {
          pause_playback();
          view_builder.is_playing(true);
          lastState = true;
        }
      }
    }

    lastButtonState = playback_behaviour_changed;
    bool triggred_skip = digitalRead(SKIP_TRACK_BUTTON);
    if (triggred_skip)
    {
      skip_track();
      view_builder.is_playing(false);
    }

    get_currently_playing_track(view_builder);

    if ((millis() - expires_counter) / 1000 >= token_expire_time - 60)
    {
      if (!request_refresh_token(response))
      {
        const char *error_msg = "Couldn't refresh access token";
        DisplayView::draw_message(display, error_msg, display.getDisplayWidth() / 2, display.getDisplayHeight() / 2);
      }
    }
  }
}