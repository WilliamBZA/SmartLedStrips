// #include <WiFi.h>
#include <WiFiManager.h>
#define WEBSERVER_H
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <DNSServer.h>
#include "time.h"
#include "Timer.h"

#include <WS2812FX.h>
#define LED_COUNT 150
#define LED_PIN 14

#if defined(ESP8266)
#include <ESP8266mDNS.h>
#endif

#if defined(ESP32)
#include <SPIFFS.h>
#include <ESPmDNS.h>
#endif

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;

DNSServer dnsServer;
AsyncWebServer server(80);

String deviceName = "UnknownDevice";
String ssid = "";
String wifiPassword = "";

int brightness = 100;
int colour = 0xFFFFFF;

bool connectToWifi() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  if (ssid != "") {
    Serial.println("Using saved SSID and Password to attempt WiFi Connection.");
    Serial.print("Saved SSID is "); Serial.println(ssid);
    Serial.print("Saved Password is "); Serial.println(wifiPassword);

    WiFi.mode(WIFI_STA);
    Serial.println("\nConnecting to WiFi Network ..");
    WiFi.begin(ssid, wifiPassword);

    int attempt = 0;
    while(WiFi.status() != WL_CONNECTED){
      attempt++;
      Serial.print(".");
      delay(100);

      if (attempt++ >= 200) {
        WiFi.disconnect();
        return false;
      }
    }

    Serial.println("\nConnected to the WiFi network");
    Serial.print("Local IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  return false;
}

void configureCaptivePortal() {
  Serial.println("Starting captive portal");
  
  WiFi.mode(WIFI_AP);
  WiFi.softAP("smart-leds");
  Serial.print("AP IP address: ");Serial.println(WiFi.softAPIP());
    
  Serial.println("Starting DNS Server");
  dnsServer.start(53, "*", WiFi.softAPIP());
}

void configureOTA() {
  // Make sure the flash size matches. For ESP8266 12F it should be 4MB.
  ArduinoOTA.setHostname(deviceName.c_str());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() {
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
}

String getSettings() {
  String settings = "{\"devicename\": \"";
  settings += deviceName;

  if (ssid != "") { // only send the time if connected to the internet

    settings += "\", \"deviceTime\": \"";
    settings += getLocalTime();
  }

  settings += "\"}";
  return settings;
}

String valueProcessor(const String& var) {
  Serial.print("Var: "); Serial.println(var);

  if (var == "DEVICE_NAME") {
    return deviceName;
  }

  if (var == "START_SETTINGS") {
    return getSettings();
  }

  return var;
}

void configureUrlRoutes() {
  server.serveStatic("/", SPIFFS, "").setTemplateProcessor(valueProcessor).setDefaultFile("index.html");

  server.on("/api/resetsettings", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.print("Resetting settings..."); Serial.println("");
    
    ssid = "";
    wifiPassword = "";
    deviceName = "";

    saveCurrentSettings();

    request->send(200, "text/json", "OK");
    delay(500);
    ESP.restart();
  });

  server.on("/api/currentsettings", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.print("Sending settings..."); Serial.println("");
    request->send(200, "text/json", getSettings());
  });

  server.onRequestBody([](AsyncWebServerRequest * request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.printf("[REQUEST]\t%s\r\n", (const char*)data);
    Serial.print("URL: "); Serial.println(request->url());

    if (request->url() == "/api/savesettings") {
      saveSettings(request, data);
      request->send(200, "text/json", "OK");

      delay(500);
      ESP.restart();
    } else if (request->url() == "/api/setcolour") {
      setColour(request, data);
      request->send(200, "text/json", "OK");
    } else {
      request->send(404);
    }
  });

  server.onFileUpload(onUpload);

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->beginResponse(SPIFFS, "/index.htm");
  });
}

// Upload files by using curl:
// curl -F 'data=@index.html' http://192.168.88.36/api/upload
void onUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (request->url() == "/api/upload") {
    static unsigned long startTimer;
    if (!index) {
      startTimer = millis();
      request->_tempFile = SPIFFS.open("/" + filename, "w");
      const char* FILESIZE_HEADER{"FileSize"};
  
      Serial.printf("UPLOAD: Receiving: '%s'\n", filename.c_str());
    }
  
    if (len) {
      request->_tempFile.write(data, len);
    }

    if (final) {
      request->_tempFile.close();
      Serial.printf("UPLOAD: Done. Received %i bytes in %.2fs which is %.2f kB/s.\n", index + len, (millis() - startTimer) / 1000.0, 1.0 * (index + len) / (millis() - startTimer));
    }
  } else {
    request->send(404);
  }
}

void setColour(AsyncWebServerRequest *request, uint8_t *data) {
  Serial.println("Setting colour...");

  StaticJsonDocument<256> doc;

  DeserializationError error = deserializeJson(doc, (const char *)data, request->contentLength());

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  Serial.println("Extracting colours");
  
  int receivedBrightness = doc["brightness"];
  Serial.print("Brightness: "); Serial.println(receivedBrightness);
  int receivedColour = doc["colour"];
  Serial.print("Colour: "); Serial.println(receivedColour);
  
  brightness = receivedBrightness;
  colour = receivedColour;

  ws2812fx.setBrightness(brightness);
  ws2812fx.setColor(colour);
  ws2812fx.service();
}

void saveSettings(AsyncWebServerRequest *request, uint8_t *data) {
  Serial.println("Saving settings...");

  StaticJsonDocument<256> doc;

  DeserializationError error = deserializeJson(doc, (const char *)data, request->contentLength());

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char* devicename = doc["devicename"];
  const char* settingsSSID = doc["ssid"];
  const char* settingsWifiPassword = doc["wifipassword"];

  deviceName = devicename;
  ssid = settingsSSID;
  wifiPassword = settingsWifiPassword;

  Serial.print("Device name: "); Serial.println(deviceName);

  saveCurrentSettings();
}

void saveCurrentSettings() {
  File settingsFile = SPIFFS.open("/settings.json", "w");
  StaticJsonDocument<1024> settingsDoc;
  
  settingsDoc["devicename"] = deviceName;
  settingsDoc["ssid"] = ssid;
  settingsDoc["wifipassword"] = wifiPassword;

  if (serializeJson(settingsDoc, settingsFile) == 0) {
    Serial.println("Failed to write to file");
  }

  settingsFile.close();
}

void loadDeviceSettings() {
  File settingsFile = SPIFFS.open("/settings.json", "r");
  if (!settingsFile) {
    Serial.println("No settings file found");
    deviceName = "UnknownDevice";
    return;
  }

  StaticJsonDocument<384> doc;

  DeserializationError error = deserializeJson(doc, settingsFile);
  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  const char* devicename = doc["devicename"];
  const char* settingsSSID = doc["ssid"];
  const char* settingsWifiPassword = doc["wifipassword"];

  deviceName = devicename;
  ssid = settingsSSID;
  wifiPassword = settingsWifiPassword;

  Serial.print("Device name: "); Serial.println(deviceName);

  settingsFile.close();
}

String getLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "00:00";
  }

  char timeHour[6];
  strftime(timeHour, 6, "%R", &timeinfo);
  Serial.print("Hour: ");
  Serial.println(timeHour);
  return timeHour;
}

void setup() {
  Serial.begin(115200);

  ws2812fx.init();
  ws2812fx.setBrightness(brightness);
  ws2812fx.setSpeed(10);
  ws2812fx.setColor(colour);
  ws2812fx.setMode(FX_MODE_STATIC);
  ws2812fx.start();

  ws2812fx.service();
  
  SPIFFS.begin();

  loadDeviceSettings();

  if (connectToWifi()) {
    String dnsName = deviceName;
    dnsName.replace(" ", "");
    if (!MDNS.begin(dnsName)) {
      Serial.println("Error starting mDNS");
    }

    configTime(gmtOffset_sec, 0, ntpServer);
  } else {
    configureCaptivePortal();
  }

  configureUrlRoutes();
  configureOTA();

  server.begin();
  Serial.println("Setup complete");
}

void loop() {
  ArduinoOTA.handle();
  dnsServer.processNextRequest();
}
