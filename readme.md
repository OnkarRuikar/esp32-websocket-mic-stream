# ESP32 websocket mic stream

Stream real-time microphone audio from an ESP32 to a browser over WebSocket. Includes a self-hosted HTML audio player and live audio visualization, using an INMP441 I2S microphone.

This project is based on article [Standalone I2S microphone server with an ESP32S3](https://arduinodiy.wordpress.com/2026/01/05/standalone-i2s-microphone-server-with-an-esp32s3/).

## Hardware

1. `ESP32-C3 Pro mini` Wi-Fi, BT 5 (LE), Single Core, 160MHz, Embedded Flash 4MB (XMC). Any ESP32 with Wi-Fi will do. I chose this because an external antenna could be attached to it to get better range.\
  ![ESP32 C3 Pro mini pinout](/images/ESP32-C3-PRO-MINI-5-Pinout.jpg)
2. `INMP441` omnidirectional microphone module.\
  ![INMP441 mic](/images/INMP441_mic.jpg)
3. Wires to connect the mic to ESP32-C3.

## Setup

### Connect mic to ESP32 board

Connect/solder the INMP441 mic module to ESP32. Refer the following pin connections:

|INMP441 mic pin|ESP32-C3 Pin|Function|
|--|--|--|
|VDD|3.3V pin|Power supply (1.62V to 3.63V). Do not connect to 5V pin!|
|GND|GND|Ground|
|L/R|GND|Left/Right channel select (GND = Left channel)|
|WS|Any free GPIO (I used pin 6)|Word Select / LR clock|
|SCK|Any free GPIO (pin 9)|Bit Clock|
|SD|Any free GPIO (pin 10)|Serial Data Output|

### Software

If you haven't done already then [setup ESP32 in Arduino IDE](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/). Install `ArduinoWebsockets` and `ElegantOTA` libraries using the library manager. Connect the ESP32 to computer using USB cable. For this ESP32 dev board use `nologo_esp32c3_super_mini` in Arduino IDE's board selection list.

[Get the ESP32 board's MAC address](https://randomnerdtutorials.com/get-change-esp32-esp8266-mac-address-arduino/) and assign static IP address to the device on your router.

Open the [audioServer.ino](/audioServer/audioServer.ino) in the Arduino IDE. We are using [over the air (OTA) deployment](https://github.com/ayushsharma82/ElegantOTA) so make sure you set the memory partition to NoFS 4MB, as shown below:

![partition](/images/espr2partition.png)

The board I am using, ESP32-C3 pro mini, has onboard LED on GPIO pin 8 so I haven't connected anything to the pin. Also the LED is active low, i.e. the LED glows when the GPIO 8 pin is pulled down. Switch `HIGH/LOW` values in the code as per your ESP32 board.

Read the code and update following:

1. GPIO pin numbers
2. The LED on my ESP32 C3 board is active low so I have to pull led pin low to glow the LED. Search and switch `HIGH` or `LOW` literals in code if your ESP32 LED is active high.
3. Update `xTaskCreate` function call as per the number of cores available on your ESP32.

Upload the code to the ESP32 and open the `http://192.168.x.x/` (IP address of ESP32) URL in browser. This is the HTTP client that connects to ESP32 and plays audio from it:

![HTTP client page](/images/htmlClient.jpg)

### Deployment

With OTA in place we don't have to connect the ESP32 every time to a computer to upload new code changes. We can simply deploy built `audioServer.ino.bin` file to the ESP32 using `http://192.168.x.x/update` URL. The page looks like below:

> ![OTA update page](/images/updatePage.jpg)

To get the `audioServer.ino.bin` file use `Sketch -> Export Compiled Binary` menu in Arduino IDE. The compiled bin file will be in `\audioServer\build\esp32.esp32.nologo_esp32c3_super_mini\audioServer.ino.bin` directory.

### Debugging

To see errors on ESP32 board look at serial monitor in Arduino IDE when ESP32 is connected to computer. And to see errors in HTTP client in web browser, see developer tool's Console logs.
