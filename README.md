## Overview:
This project utilizes the Spotify API to display the currently playing song on an OLED display. Implemented using an ESP8266 microcontroller, it offers a sleek solution for visualizing your music playback. Additionally, the playback can be conveniently controlled using two buttons for skipping and play / pause songs.

## Features:
1. Real-time display of currently playing song information from Spotify.
2. Seamless integration with ESP8266 for effortless setup and usage.
3. User-friendly control with two buttons for playback management.
5. Expandable functionality for incorporating additional features or enhancements.

## How to Use:
1. Clone or download the repository.
2. Visit the [Spotify API Website](https://developer.spotify.com/) click on Documentation > Web API
3. Follow the "Getting started" steps
3. Copy your client IP as well as the client secret into the right field in the index.h file
3. Connect the ESP8266 to your display and two buttons.
4. Make sure you downloaded the PlatformIO extension in VSCode if not, do so
5. Open the repository with PlatformIO and upload the code
5. Open the IP Address shown on the OLED display and login to your Spotify account

## Contributions:
Contributions are welcome! Whether it's bug fixes, feature enhancements, or documentation improvements, feel free to copy the repository and submit a pull request.

## License:
This project is licensed under the MIT License, allowing for both personal and commercial use with proper attribution.
The used [U8glib2 library](https://github.com/olikraus/u8g2) is licensed under the 2-Claude BSD License.

## Disclaimer
Please note that some features in this program are only supported if you have a Spotify Premium account. So be prepared if a function does not work as expected when you log in with a free Spotify account.