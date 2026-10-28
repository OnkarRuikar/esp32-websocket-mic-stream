/*

ESP32 websocket mic stream

Stream INMP441 mic audio over Wi-Fi. Also, host an HTTP audio client on the same server.
Simply open ESP32's IP address in a browser and connect. 
For over the air firmware update use http://192.168.x.x/update URL.

Note: Read the code and make changes accordingly before the first deployment.

My ESP 32 module details
Chip type:          ESP32-C3 (QFN32) (revision v0.4)
Features:           Wi-Fi, BT 5 (LE), Single Core, 160MHz, Embedded Flash 4MB (XMC)
Crystal frequency:  40MHz
USB mode:           USB-Serial/JTAG
Manufacturer: 46
Device: 4016
Status value: 0x200200

*/

#include <driver/i2s_std.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <ElegantOTA.h>

// Update: Mic connections. 
// Use any available GPIO pins on the ESP32. But make sure to not use LED GPIO pin
#define I2S_SD 10
#define I2S_WS 6
#define I2S_SCK 9

#define I2S_PORT I2S_NUM_0
#define bufferCnt 10
#define bufferLen 1024
int16_t sBuffer[bufferLen];

// Use your Wi-Fi credentials
const char* ssid = "";
const char* password = "";

// Update: LED is on pin 8 on ESP32 C3 supermini
#define LED 8

uint8_t ledOn = LOW;
unsigned long lastToggle = 0;

using namespace websockets;
WebsocketsServer server;
WebsocketsClient client;
WebServer webServer(80);  // Web server on port 80 for serving HTML

bool isWebSocketConnected = false;
String htmlPage;

i2s_chan_handle_t rx_handle = NULL;

void setup() {
  delay(3000);
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW); // led is active low on my ESP32
  delay(2000);
  digitalWrite(LED, HIGH);
  delay(1000);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  }

  connectWiFi();
  setupWebServer();
  setupWebSocketServer();
  ElegantOTA.begin(&webServer);  // Uses existing webServer

  // Update: Use either of the following line according to availble cores on your ESP32 module
  //xTaskCreatePinnedToCore(micTask, "micTask", 10000, NULL, 1, NULL, 1);   // for 2 core ESP32
  xTaskCreate(micTask, "micTask", 10000, NULL, 1, NULL);    // for 1 core ESP32
}

void loop() {
  webServer.handleClient();
  ElegantOTA.loop();
  // Accept new connection if we don't have one
  if (!isWebSocketConnected && server.poll()) {
    client = server.accept();
    Serial.println("WebSocket client connected!");
    isWebSocketConnected = true;

    client.onMessage([](WebsocketsMessage msg) {
      Serial.print("Got message: ");
      Serial.println(msg.data());
    });

    client.onEvent([](WebsocketsEvent event, String data) {
      if (event == WebsocketsEvent::ConnectionClosed) {
        digitalWrite(LED, HIGH);
        Serial.println("WebSocket client disconnected");
        isWebSocketConnected = false;
      }
    });
  }

  // Handle WebSocket client messages
  if (isWebSocketConnected && client.available()) {
    client.poll();
  }

  if(!WiFi.isConnected()) {
    delay(5000);
    WiFi.reconnect();
  }
}

void connectWiFi() {
  WiFi.setHostname("ESP32-StreamingMic");
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupWebServer() {
  // if required flip image using inline style 'transform: scaleY(-1) scaleX(-1);'
  htmlPage = R"rawliteral(
<!DOCTYPE html><html><title>Video Audio Streams</title><meta content='width=device-width,initial-scale=1' name=viewport><meta charset=utf-8>
<style>#cam,body,html{width:100%;height:100%}#status,a{padding:10px}*{box-sizing:border-box}body,html{margin:0;background:#000;text-align:center}#cam{d1inset:0;object-fit:contain}#ctl{position:fixed;top:10px;right:10px}a{display:block;margin:5px;color:#fff;text-decoration:none}.on{background:#198754}.off{background:#dc3545}.container{background-color:wheat;padding:30px;display:inline-block}button{padding:10px 20px;font-size:16px;margin:10px;cursor:pointer}.connected{background-color:#d4edda;color:#155724}.disconnected{background-color:#f8d7da;color:#721c24}.dials{display:grid}</style>
</style>
    <!-- Video stream from a camera. Delete following lines if you don't need it -->
    <img id='cam' src='http://192.168.0.6/dev/080c00031DF6/live' style=''>
    <div id=ctl>
        <a class=on href='http://192.168.0.6/dev/080c00031DF6/irOn' onclick=ir(event,this.href)>IR On</a>
        <a class=off href='http://192.168.0.6/dev/080c00031DF6/irOff' onclick=ir(event,this.href)>IR Off</a>
    </div>
    <script>function ir(e, n) {e.preventDefault(),(new Image).src = n}</script>
    <!-- camera code ends here -->
    <!-- Audio stream controls -->
    <div class=container>
        <h1>ESP32 Audio Stream</h1>
        <p>Device IP: 192.168.0.29
        <p>Sample Rate: 44100 Hz | Buffer: 16384 samples
        <div class=controls>
            <button onclick=connectWebSocket() id=connectBtn>Connect Audio</button>
            <button onclick=disconnectWebSocket() id=disconnectBtn disabled>Disconnect</button>
            <button onclick=playTestTone() id=testBtn>Test Tone</button>
            <button onclick=resetConnection()>Reset Connection</button>
            <button onclick=hardReset() style=background:#dc3545;color:#fff>🔴 Hard Reset ESP32</button>
        </div>
        <div class='disconnected status' id=status>Disconnected</div>
        <div class=dials>
            <label>Volume (0-100):</label>
            <span id=volumeValue>100</span>
            <input id=volume max=100 min=0 onchange=changeVolume(this.value) type=range value=100>
            <br>
            <label>Volume multiplier (1-20):</label>
            <span id=multiplierValue>8</span>
            <input id=multiplier max=20 min=1 onchange=changeMultiplier(this.value) type=range value=3>
            <br>
            <label>High pass frequency (20-20,000Hz):</label>
            <input id=highF max=20000 min=20 onchange=changeHightF(this.value) type=range value=200>
            <span id=hfValue>200</span>
            <br>
            <label>Low Pass frequency (20-20,000Hz):</label>
            <span id=lfValue>12000</span>
            <input id=lowF max=20000 min=20 onchange=changeLowF(this.value) type=range value=12000>
        </div>
        <div style=margin-top:20px>
            <canvas height=250 id=visualizer width=500></canvas>
        </div>
    </div>
    <script>
    function resetConnection(){websocket&&(websocket.close(),console.log("WebSocket closed")),fetch("/reset").then(e=>{e.ok&&(alert("Connection reset"),console.log("ESP32 connection reset"))}).catch(e=>{console.error("Reset error:",e)})}function hardReset(){confirm("⚠️ WARNING: This will restart the ESP32!\n\nContinue?")&&fetch("/hardreset").then(()=>{alert("ESP32 restarting... Page will reload in 5 seconds."),setTimeout(()=>location.reload(),5e3)}).catch(e=>{alert("Reset failed: "+e)})}let websocket=null,audioContext=null,gainNode=null,sourceNode=null,highPassFilter=null,lowPassFilter=null,isPlaying=!1,audioBuffer=[],volumemultiplier=3;const bufferSize=16384,canvas=document.getElementById("visualizer"),ctx=canvas.getContext("2d");function getWebSocketUrl(){return`${"https:"===window.location.protocol?"wss:":"ws:"}//${window.location.hostname}:8090`}function connectWebSocket(){var e;websocket&&websocket.readyState===WebSocket.OPEN?alert("Already connected!"):initializeAudio()?(e=getWebSocketUrl(),console.log("Connecting to:",e),(websocket=new WebSocket(e)).binaryType="arraybuffer",websocket.onopen=function(){console.log("WebSocket connected"),updateStatus(!0),document.getElementById("connectBtn").disabled=!0,document.getElementById("disconnectBtn").disabled=!1,startAudioPlayback()},websocket.onmessage=function(e){e.data instanceof ArrayBuffer?processAudioData(e.data):console.log("Message:",e.data)},websocket.onclose=function(){console.log("WebSocket disconnected"),updateStatus(!1),stopAudioPlayback(),document.getElementById("connectBtn").disabled=!1,document.getElementById("disconnectBtn").disabled=!0},websocket.onerror=function(e){console.error("WebSocket error:",e),updateStatus(!1)}):alert("Failed to initialize audio. Check browser permissions.")}function initializeAudio(){try{if(!audioContext){if(document.hidden)return!1;(gainNode=(audioContext=new(window.AudioContext||window.webkitAudioContext)({sampleRate:44100})).createGain()).gain.setValueAtTime(1,audioContext.currentTime),console.log("AudioContext created, sample rate:",audioContext.sampleRate),(highPassFilter=audioContext.createBiquadFilter()).type="highpass",highPassFilter.frequency.setValueAtTime(200,audioContext.currentTime),highPassFilter.Q.setValueAtTime(2,audioContext.currentTime),(lowPassFilter=audioContext.createBiquadFilter()).type="lowpass",lowPassFilter.frequency.value=12e3,lowPassFilter.Q.value=2,audioContext.createOscillator().connect(gainNode).connect(lowPassFilter).connect(highPassFilter).connect(audioContext.destination)}return!0}catch(e){return console.error("Audio initialization error:",e),!1}}function startAudioPlayback(){audioContext&&!isPlaying&&((sourceNode=audioContext.createScriptProcessor(16384,1,1)).onaudioprocess=onAudioProcess,sourceNode.connect(gainNode),isPlaying=!0,console.log("Audio playback started"))}function onAudioProcess(e){let t=e.outputBuffer,o=t.getChannelData(0);var n=o.length;let i=0;if(audioBuffer.length>=n)for(let a=0;a<n;a++)o[a]=audioBuffer.shift(),i<Math.abs(o[a])&&(i=o[a]);else{var c=audioBuffer.length;for(let s=0;s<c;s++)o[s]=audioBuffer.shift(),i<Math.abs(o[s])&&(i=o[s]);for(let l=c;l<n;l++)o[l]=Math.floor(Math.random()*(.01-.001))+.001}visualizeAudio(o,i)}function processAudioData(e){if(isPlaying){var t=new Int16Array(e),o=t.length;for(let n=0;n<o;n++){var i=t[n]/32768*volumemultiplier;audioBuffer.push(i),34e3<audioBuffer.length&&audioBuffer.shift()}}}function stopAudioPlayback(){sourceNode&&(sourceNode.disconnect(),sourceNode=null),isPlaying=!1,audioBuffer=[],console.log("Audio stopped")}function disconnectWebSocket(){websocket&&websocket.close(),stopAudioPlayback(),audioContext&&(audioContext.close(),audioContext=null)}function changeVolume(e){gainNode&&(gainNode.gain.setValueAtTime(e/100,audioContext.currentTime),volumeValue.innerText=e)}function changeMultiplier(e){volumemultiplier=e,multiplierValue.innerText=e}function changeHightF(e){highPassFilter&&(highPassFilter.frequency.setValueAtTime(e,audioContext.currentTime),hfValue.innerText=e)}function changeLowF(e){lowPassFilter&&(lowPassFilter.frequency.setValueAtTime(e,audioContext.currentTime),lfValue.innerText=e)}function updateStatus(e){let t=document.getElementById("status");e?(t.textContent="Connected - Streaming Audio",t.className="status connected"):(t.textContent="Disconnected",t.className="status disconnected")}function visualizeAudio(e,t){if(ctx){let o=canvas.width,n=canvas.height;ctx.fillStyle="rgb(220, 190, 180)",ctx.fillRect(0,0,o,n),ctx.beginPath(),ctx.moveTo(0,n/2);for(let i=0;i<e.length;i++){let a=i/e.length*o,c=(.5-e[i]/2)*n;0===i?ctx.moveTo(a,c):ctx.lineTo(a,c)}ctx.strokeStyle="blue",ctx.lineWidth=1,ctx.stroke(),ctx.beginPath(),ctx.moveTo(0,n/2),ctx.lineTo(o,n/2),ctx.strokeStyle="red",ctx.stroke()}}function playTestTone(){if(initializeAudio()){let e=audioContext.createOscillator(),t=audioContext.createGain();e.connect(t),t.connect(audioContext.destination),e.frequency.setValueAtTime(400,audioContext.currentTime),t.gain.setValueAtTime(1,audioContext.currentTime),e.start(),setTimeout(()=>{e.stop()},1e3),console.log("Playing test tone (440Hz)")}}document.addEventListener("click",function e(){audioContext||initializeAudio(),document.removeEventListener("click",e)},{once:!0});
    </script>
)rawliteral";

  // Add the reset endpoint handler
  webServer.on("/reset", []() {
    if (isWebSocketConnected) {
      client.close();
      isWebSocketConnected = false;
      Serial.println("Connection reset via web request");
    }
    webServer.send(200, "text/plain", "Connection reset");
  });

  // Serve the HTML page
  webServer.on("/", []() {
    webServer.send(200, "text/html", htmlPage);
  });

  // Add this to your web server routes:
  webServer.on("/hardreset", []() {
    Serial.println("=== HARD RESET REQUESTED ===");
    webServer.send(200, "text/html",
                   "<h1>ESP32 Restarting...</h1>"
                   "<p>Device will restart in 3 seconds.</p>"
                   "<script>setTimeout(() => window.location.href='/', 5000);</script>");

    delay(100);     // Ensure response is sent
    ESP.restart();  // Hard reset
  });

  // Start web server
  webServer.begin();
  Serial.println("HTTP server started");
}

void setupWebSocketServer() {
  // Start WebSocket server on port 8080 (different from HTTP port 80)
  server.listen(8090);
  Serial.println("WebSocket server started on port 8090");
}

void i2s_init_std() {
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = bufferCnt;
  chan_cfg.dma_frame_num = bufferLen; 

  // Allocate the RX channel
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

  // Set up the Standard Philips mode configuration for the INMP441
  i2s_std_config_t std_cfg = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100), // 44.1kHz sample rate
    // Note: INMP441 requires a 32-bit physical slot size to communicate properly.
    // The driver automatically truncates/aligns this data to your target 16-bit resolution.
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
    .gpio_cfg = {
      .mclk = I2S_GPIO_UNUSED,
      .bclk = (gpio_num_t)I2S_SCK,
      .ws   = (gpio_num_t)I2S_WS,
      .dout = I2S_GPIO_UNUSED,
      .din  = (gpio_num_t)I2S_SD,
    },
  };
  std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
}

void micTask(void* parameter) {
  i2s_init_std();

  size_t bytesIn = 0;
  unsigned long lastPrint = 0;

  while (1) {
    esp_err_t result = i2s_channel_read(rx_handle, &sBuffer, bufferLen, &bytesIn, portMAX_DELAY);

    if (result == ESP_OK) {
      if (millis() - lastPrint > 1000) {
        //Serial.printf("I2S: Read %d bytes, WS Connected: %d\n", bytesIn, isWebSocketConnected);
        lastPrint = millis();
      }

      if (isWebSocketConnected && client.available()) {
        bool sent = client.sendBinary((const char*)sBuffer, bytesIn);
        if (!sent) {
          Serial.println("Failed to send WebSocket data!");
          isWebSocketConnected = false;
        } else {
          if ((millis() - lastToggle) >= 1000) {
            lastToggle = millis();
            digitalWrite(LED, ledOn);
            ledOn = !ledOn;
          }
        }
      } else {
        digitalWrite(LED, HIGH);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}