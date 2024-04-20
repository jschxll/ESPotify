#include <string>
#include <math.h>

String generate_random_string(int size);

const String CLIENT_ID = "YOUR CLIENT ID";
const String CLIENT_SECRET = "YOUR CLIENT SECRET";
const String SCOPE = "user-read-private user-read-currently-playing user-modify-playback-state";
const String REDIRECT_URL = "http://YOUR_IP_ADDRESS/callback";
const String ERROR_PAGE = "<h1>Something went wrong</h1><p>Connection to Spotify Account went wrong. Please retry.</p>";
const String SUCCESS_SITE = "<h1>Connection successful!</h1><p>You are now connected to your Spotify Account. You can now close this site.</p>";
const String HOMEPAGE =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<title>Spotify Authentication</title>\n"
    "</head>\n"
    "<body>\n"
    "<p>Hello World! Press <a href='https://accounts.spotify.com/authorize?response_type=code&client_id=" +
    String(CLIENT_ID) + "&scope=" + String(SCOPE) + "&redirect_uri=" + String(REDIRECT_URL) + "&state=" + generate_random_string(16) + "'>here</a> to login to Spotify</p>\n"
                                                                                                                                       "</body>\n"
                                                                                                                                       "</html>\n";
String generate_random_string(int size)
{
    String letters = "ABCDEFGHIKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890";
    String r_string = "";
    srand(time(NULL));
    for (int i = 0; i < size; i++)
    {
        r_string += letters.charAt(rand() % letters.length());
    }
    return r_string;
}