#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ESP32-CAM (AI-Thinker) Pin Configuration
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// WiFi credentials
const char* ssid = "Demo2";
const char* password = "12345678";

// RTSP Server Configuration
const int RTSP_PORT = 554;
const char* RTSP_PATH = "/stream";

// Video Quality Settings
#define FRAMESIZE_CONFIG FRAMESIZE_UXGA  // Options: FRAMESIZE_QVGA, FRAMESIZE_VGA, FRAMESIZE_SVGA, FRAMESIZE_UXGA
#define JPEG_QUALITY 8                   // 0-63, lower is better quality (8 = high quality)
#define TARGET_FPS 3                      // Target frames per second
#define FRAME_DELAY_MS (1000 / TARGET_FPS) // ~333ms delay for 3 FPS

// HTTP MJPEG Stream (for browser viewing)
WebServer server(80);

// Stream Statistics
struct StreamStats {
  unsigned long frameCount;
  unsigned long totalBytes;
  unsigned long lastFrameTime;
  float currentFPS;
  unsigned long lastFrameSize;
};

StreamStats streamStats = {0, 0, 0, 0.0, 0};

// Camera initialization
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_CONFIG;
    config.jpeg_quality = JPEG_QUALITY;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_brightness(s, 0);
  s->set_contrast(s, 0);
  s->set_saturation(s, 0);
  s->set_special_effect(s, 0);
  s->set_whitebal(s, 1);
  s->set_awb_gain(s, 1);
  s->set_wb_mode(s, 0);
  s->set_exposure_ctrl(s, 1);
  s->set_aec2(s, 0);
  s->set_ae_level(s, 0);
  s->set_aec_value(s, 300);
  s->set_gain_ctrl(s, 1);
  s->set_agc_gain(s, 0);
  s->set_gainceiling(s, (gainceiling_t)0);
  s->set_bpc(s, 0);
  s->set_wpc(s, 1);
  s->set_raw_gma(s, 1);
  s->set_lenc(s, 1);
  s->set_hmirror(s, 0);
  s->set_vflip(s, 0);
  s->set_dcw(s, 1);
  s->set_colorbar(s, 0);

  return true;
}

// WiFi Connection
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

// RTSP Server (Simple UDP-based implementation)
WiFiUDP udp;
bool rtspClientConnected = false;
IPAddress rtspClientIP;

void handleRTSP() {
  uint8_t buffer[1024];
  int packetSize = udp.parsePacket();
  
  if (packetSize) {
    int len = udp.read(buffer, sizeof(buffer));
    String request = String((char*)buffer);
    
    if (request.indexOf("OPTIONS") >= 0) {
      String response = "RTSP/1.0 200 OK\r\n"
                       "CSeq: 1\r\n"
                       "Public: DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN\r\n\r\n";
      udp.beginPacket(udp.remoteIP(), udp.remotePort());
      udp.write((const uint8_t*)response.c_str(), response.length());
      udp.endPacket();
    }
    else if (request.indexOf("DESCRIBE") >= 0) {
      rtspClientIP = udp.remoteIP();
      rtspClientConnected = true;
      String sdp = "v=0\r\n"
                   "o=- 0 0 IN IP4 " + WiFi.localIP().toString() + "\r\n"
                   "s=ESP32-CAM Stream\r\n"
                   "c=IN IP4 " + WiFi.localIP().toString() + "\r\n"
                   "t=0 0\r\n"
                   "m=video 0 RTP/AVP 96\r\n"
                   "a=rtpmap:96 H264/90000\r\n"
                   "a=fmtp:96 packetization-mode=1\r\n";
      String response = "RTSP/1.0 200 OK\r\n"
                       "CSeq: 2\r\n"
                       "Content-Type: application/sdp\r\n"
                       "Content-Length: " + String(sdp.length()) + "\r\n\r\n" + sdp;
      udp.beginPacket(rtspClientIP, udp.remotePort());
      udp.write((const uint8_t*)response.c_str(), response.length());
      udp.endPacket();
    }
    else if (request.indexOf("SETUP") >= 0) {
      String response = "RTSP/1.0 200 OK\r\n"
                       "CSeq: 3\r\n"
                       "Transport: RTP/AVP/UDP;unicast;client_port=5004-5005;server_port=5006-5007\r\n"
                       "Session: 12345678\r\n\r\n";
      udp.beginPacket(rtspClientIP, udp.remotePort());
      udp.write((const uint8_t*)response.c_str(), response.length());
      udp.endPacket();
    }
    else if (request.indexOf("PLAY") >= 0) {
      String response = "RTSP/1.0 200 OK\r\n"
                       "CSeq: 4\r\n"
                       "Session: 12345678\r\n"
                       "Range: npt=0.000-\r\n\r\n";
      udp.beginPacket(rtspClientIP, udp.remotePort());
      udp.write((const uint8_t*)response.c_str(), response.length());
      udp.endPacket();
    }
  }
}

// HTTP MJPEG Stream Handler
void handleStream() {
  WiFiClient client = server.client();
  
  if (!client) {
    Serial.println("No client available");
    return;
  }
  
  Serial.println("New streaming client connected");
  
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n";
  response += "Access-Control-Allow-Origin: *\r\n";
  response += "Cache-Control: no-cache, no-store, must-revalidate\r\n";
  response += "Pragma: no-cache\r\n\r\n";
  client.print(response);
  client.flush();

  camera_fb_t * fb = NULL;
  unsigned long lastFrameTime = 0;
  unsigned long nextFrameTime = 0;
  bool firstFrame = true;

  while (client.connected()) {
    unsigned long currentTime = millis();
    
    if (firstFrame || currentTime >= nextFrameTime) {
      fb = esp_camera_fb_get();
      if (!fb) {
        Serial.println("Camera capture failed");
        delay(10);
        continue;
      }

      // Calculate FPS
      if (lastFrameTime > 0) {
        unsigned long frameInterval = currentTime - lastFrameTime;
        if (frameInterval > 0) {
          streamStats.currentFPS = 1000.0 / frameInterval;
        }
      }
      
      // Update statistics
      streamStats.frameCount++;
      streamStats.totalBytes += fb->len;
      streamStats.lastFrameTime = currentTime;
      streamStats.lastFrameSize = fb->len;
      lastFrameTime = currentTime;

      // Send frame header
      client.print("--frame\r\n");
      client.print("Content-Type: image/jpeg\r\n");
      client.print("Content-Length: " + String(fb->len) + "\r\n\r\n");
      
      // Send frame data in chunks for smooth continuous delivery
      size_t sent = 0;
      const size_t chunkSize = 1024;
      while (sent < fb->len && client.connected()) {
        size_t toSend = (fb->len - sent < chunkSize) ? (fb->len - sent) : chunkSize;
        client.write(fb->buf + sent, toSend);
        client.flush();
        sent += toSend;
        yield();
      }
      
      client.print("\r\n");
      client.flush();

      esp_camera_fb_return(fb);
      fb = NULL;
      
      // Schedule next frame for smooth timing
      nextFrameTime = currentTime + FRAME_DELAY_MS;
      firstFrame = false;
    } else {
      // Small delay to prevent CPU spinning, but allow other tasks
      delay(1);
      yield();
    }
  }
  
  Serial.println("Streaming client disconnected");
}

// Stats endpoint handler
void handleStats() {
  Serial.println("Stats request received");
  String resolution = "Unknown";
  
  // Get resolution string based on frame size
  framesize_t frameSize = FRAMESIZE_CONFIG;
  if (frameSize == FRAMESIZE_QVGA) resolution = "QVGA (320x240)";
  else if (frameSize == FRAMESIZE_CIF) resolution = "CIF (400x296)";
  else if (frameSize == FRAMESIZE_VGA) resolution = "VGA (640x480)";
  else if (frameSize == FRAMESIZE_SVGA) resolution = "SVGA (800x600)";
  else if (frameSize == FRAMESIZE_XGA) resolution = "XGA (1024x768)";
  else if (frameSize == FRAMESIZE_SXGA) resolution = "SXGA (1280x1024)";
  else if (frameSize == FRAMESIZE_UXGA) resolution = "UXGA (1600x1200)";
  else if (frameSize == FRAMESIZE_HD) resolution = "HD (1280x720)";
  else if (frameSize == FRAMESIZE_FHD) resolution = "FHD (1920x1080)";
  
  String json = "{";
  json += "\"fps\":" + String(streamStats.currentFPS, 2) + ",";
  json += "\"quality\":" + String(JPEG_QUALITY) + ",";
  json += "\"resolution\":\"" + resolution + "\",";
  json += "\"frameSize\":" + String(streamStats.lastFrameSize) + ",";
  json += "\"totalFrames\":" + String(streamStats.frameCount) + ",";
  json += "\"totalBytes\":" + String(streamStats.totalBytes) + ",";
  json += "\"avgFrameSize\":" + String(streamStats.frameCount > 0 ? streamStats.totalBytes / streamStats.frameCount : 0) + ",";
  json += "\"targetFPS\":" + String(TARGET_FPS) + ",";
  json += "\"uptime\":" + String(millis() / 1000) + ",";
  json += "\"wifiRSSI\":" + String(WiFi.RSSI()) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap());
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleRoot() {
  Serial.println("Root request received");
  String html = R"HTML_STRING(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-CAM Stream Viewer</title>
  <style>
    body {
      margin: 0;
      padding: 20px;
      font-family: Arial, sans-serif;
      background: #1a1a1a;
      color: #fff;
    }
    .container {
      max-width: 1200px;
      margin: 0 auto;
    }
    h1 {
      text-align: center;
      color: #4CAF50;
    }
    .stream-container {
      background: #2a2a2a;
      border-radius: 10px;
      padding: 20px;
      margin: 20px 0;
      box-shadow: 0 4px 6px rgba(0,0,0,0.3);
    }
    img {
      width: 100%;
      height: auto;
      border-radius: 8px;
      display: block;
    }
    .controls {
      margin: 20px 0;
      text-align: center;
    }
    button {
      background: #4CAF50;
      color: white;
      border: none;
      padding: 12px 24px;
      margin: 5px;
      border-radius: 5px;
      cursor: pointer;
      font-size: 16px;
      transition: background 0.3s;
    }
    button:hover {
      background: #45a049;
    }
    .info {
      background: #333;
      padding: 15px;
      border-radius: 5px;
      margin: 10px 0;
    }
    .rtsp-info {
      background: #2d4a22;
      padding: 15px;
      border-radius: 5px;
      margin: 10px 0;
    }
    .stats-panel {
      background: #1e3a5f;
      padding: 15px;
      border-radius: 5px;
      margin: 10px 0;
    }
    .stats-panel h3 {
      margin-top: 0;
      color: #64b5f6;
    }
    .stats-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 15px;
      margin-top: 15px;
    }
    .stat-item {
      background: rgba(0,0,0,0.3);
      padding: 10px;
      border-radius: 5px;
    }
    .stat-label {
      font-size: 0.85em;
      color: #aaa;
      margin-bottom: 5px;
    }
    .stat-value {
      font-size: 1.3em;
      font-weight: bold;
      color: #4CAF50;
    }
    .stat-value.warning {
      color: #ff9800;
    }
    .stat-value.error {
      color: #f44336;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>ESP32-CAM Stream Viewer</h1>
    <div class="stream-container">
      <img src="/stream" id="stream" alt="Camera Stream">
    </div>
    <div class="controls">
      <button onclick="refreshStream()">Refresh Stream</button>
      <button onclick="toggleFullscreen()">Fullscreen</button>
    </div>
    <div class="info">
      <h3>Stream Information</h3>
      <p><strong>HTTP MJPEG Stream:</strong> http://IP_ADDRESS/stream</p>
      <p><strong>RTSP Stream:</strong> rtsp://IP_ADDRESS:554/stream</p>
    </div>
    <div class="stats-panel">
      <h3>Stream Statistics</h3>
      <div class="stats-grid">
        <div class="stat-item">
          <div class="stat-label">Current FPS</div>
          <div class="stat-value" id="statFPS">0.00</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Target FPS</div>
          <div class="stat-value" id="statTargetFPS">3</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">JPEG Quality</div>
          <div class="stat-value" id="statQuality">8</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Resolution</div>
          <div class="stat-value" id="statResolution" style="font-size: 1.1em;">UXGA</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Frame Size</div>
          <div class="stat-value" id="statFrameSize">0 KB</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Avg Frame Size</div>
          <div class="stat-value" id="statAvgFrameSize">0 KB</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Total Frames</div>
          <div class="stat-value" id="statTotalFrames">0</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Total Data</div>
          <div class="stat-value" id="statTotalBytes">0 MB</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Uptime</div>
          <div class="stat-value" id="statUptime">0s</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">WiFi Signal</div>
          <div class="stat-value" id="statRSSI">0 dBm</div>
        </div>
        <div class="stat-item">
          <div class="stat-label">Free Heap</div>
          <div class="stat-value" id="statFreeHeap">0 KB</div>
        </div>
      </div>
    </div>
    <div class="rtsp-info">
      <h3>RTSP Viewer Instructions</h3>
      <p>To view RTSP stream, use one of these methods:</p>
      <ul>
        <li><strong>VLC Media Player:</strong> Media -> Open Network Stream -> rtsp://IP_ADDRESS:554/stream</li>
        <li><strong>ffplay:</strong> ffplay rtsp://IP_ADDRESS:554/stream</li>
        <li><strong>GStreamer:</strong> gst-launch-1.0 rtspsrc location=rtsp://IP_ADDRESS:554/stream ! autovideosink</li>
      </ul>
    </div>
  </div>
  <script>
    function refreshStream() {
      const img = document.getElementById('stream');
      const timestamp = new Date().getTime();
      img.src = '/stream?t=' + timestamp;
    }
    function toggleFullscreen() {
      const img = document.getElementById('stream');
      if (img.requestFullscreen) {
        img.requestFullscreen();
      } else if (img.webkitRequestFullscreen) {
        img.webkitRequestFullscreen();
      } else if (img.mozRequestFullScreen) {
        img.mozRequestFullScreen();
      }
    }
    
    function formatBytes(bytes) {
      if (bytes < 1024) return bytes + ' B';
      if (bytes < 1048576) return (bytes / 1024).toFixed(2) + ' KB';
      return (bytes / 1048576).toFixed(2) + ' MB';
    }
    
    function formatTime(seconds) {
      if (seconds < 60) return seconds + 's';
      if (seconds < 3600) return Math.floor(seconds / 60) + 'm ' + (seconds % 60) + 's';
      const hours = Math.floor(seconds / 3600);
      const mins = Math.floor((seconds % 3600) / 60);
      return hours + 'h ' + mins + 'm';
    }
    
    function updateStats() {
      fetch('/stats')
        .then(response => response.json())
        .then(data => {
          document.getElementById('statFPS').textContent = data.fps.toFixed(2);
          document.getElementById('statTargetFPS').textContent = data.targetFPS;
          document.getElementById('statQuality').textContent = data.quality;
          document.getElementById('statResolution').textContent = data.resolution;
          document.getElementById('statFrameSize').textContent = formatBytes(data.frameSize);
          document.getElementById('statAvgFrameSize').textContent = formatBytes(data.avgFrameSize);
          document.getElementById('statTotalFrames').textContent = data.totalFrames.toLocaleString();
          document.getElementById('statTotalBytes').textContent = formatBytes(data.totalBytes);
          document.getElementById('statUptime').textContent = formatTime(data.uptime);
          document.getElementById('statRSSI').textContent = data.wifiRSSI + ' dBm';
          document.getElementById('statFreeHeap').textContent = formatBytes(data.freeHeap);
          
          // Color code FPS based on target
          const fpsElement = document.getElementById('statFPS');
          if (data.fps < data.targetFPS * 0.8) {
            fpsElement.className = 'stat-value error';
          } else if (data.fps < data.targetFPS * 0.95) {
            fpsElement.className = 'stat-value warning';
          } else {
            fpsElement.className = 'stat-value';
          }
          
          // Color code WiFi signal
          const rssiElement = document.getElementById('statRSSI');
          if (data.wifiRSSI > -70) {
            rssiElement.className = 'stat-value';
          } else if (data.wifiRSSI > -85) {
            rssiElement.className = 'stat-value warning';
          } else {
            rssiElement.className = 'stat-value error';
          }
        })
        .catch(error => console.error('Error fetching stats:', error));
    }
    
    setInterval(refreshStream, 30000);
    setInterval(updateStats, 3000);
    updateStats();
  </script>
</body>
</html>
)HTML_STRING";
  
  String ipAddress = WiFi.localIP().toString();
  while (html.indexOf("IP_ADDRESS") >= 0) {
    html.replace("IP_ADDRESS", ipAddress);
  }
  
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nESP32-CAM RTSP Streaming Server");
  
  if (!initCamera()) {
    Serial.println("Camera initialization failed!");
    return;
  }
  Serial.println("Camera initialized successfully");

  connectWiFi();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot start server");
    return;
  }

  server.on("/", handleRoot);
  server.on("/stream", handleStream);
  server.on("/stats", handleStats);
  server.onNotFound([]() {
    server.send(404, "text/plain", "Not Found");
  });
  
  server.begin();
  delay(100);
  Serial.println("HTTP server started");
  Serial.print("Access the web interface at: http://");
  Serial.println(WiFi.localIP());
  Serial.println("Waiting for client connections...");

  udp.begin(RTSP_PORT);
  Serial.print("RTSP server started on rtsp://");
  Serial.print(WiFi.localIP());
  Serial.print(":");
  Serial.print(RTSP_PORT);
  Serial.println(RTSP_PATH);
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
    handleRTSP();
  } else {
    Serial.println("WiFi disconnected, reconnecting...");
    connectWiFi();
  }
  
  // Send RTP video packets if client is connected
  if (rtspClientConnected) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (fb) {
      // Simple RTP packetization (H.264 would require proper encoding)
      // For now, send JPEG frames as RTP packets
      uint8_t rtpPacket[1460];
      rtpPacket[0] = 0x80; // V=2, P=0, X=0, CC=0
      rtpPacket[1] = 0x60; // M=0, PT=96 (dynamic)
      rtpPacket[2] = 0x00;
      rtpPacket[3] = 0x00; // Sequence number
      rtpPacket[4] = 0x00;
      rtpPacket[5] = 0x00;
      rtpPacket[6] = 0x00;
      rtpPacket[7] = 0x00; // Timestamp
      rtpPacket[8] = 0x00;
      rtpPacket[9] = 0x00;
      rtpPacket[10] = 0x00;
      rtpPacket[11] = 0x00; // SSRC

      // Send JPEG data in chunks
      size_t offset = 0;
      while (offset < fb->len) {
        size_t chunkSize = (fb->len - offset < 1440) ? (fb->len - offset) : 1440;
        memcpy(rtpPacket + 12, fb->buf + offset, chunkSize);
        
        udp.beginPacket(rtspClientIP, 5004);
        udp.write(rtpPacket, 12 + chunkSize);
        udp.endPacket();
        
        offset += chunkSize;
        delay(10);
      }

      esp_camera_fb_return(fb);
    }
    delay(FRAME_DELAY_MS); // ~3 FPS
  }
}
