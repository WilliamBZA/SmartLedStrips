#include <WiFi.h>
#include <WiFiManager.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPmDNS.h>
#include "time.h"
#include "Timer.h"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200;

WiFiManager wifiManager;
AsyncWebServer server(80);

String deviceName = "UnknownDevice";

void connectToWifi() {
  wifiManager.autoConnect("smartlights");

  WiFi.mode(WIFI_STA);
  Serial.println("\nConnecting to WiFi Network ..");
 
  while(WiFi.status() != WL_CONNECTED){
      Serial.print(".");
      delay(100);
  }
 
  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
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

  settings += "\", \"deviceTime\": \"";
  settings += getLocalTime();

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

  server.on("/api/resetsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("Resetting settings..."); Serial.println("");
    wifiManager.resetSettings();

    request->send(200, "text/json", "OK");
    delay(500);
    ESP.restart();
  });

  server.on("/api/currentsettings", HTTP_GET, [](AsyncWebServerRequest *request) {
    Serial.print("Sending settings..."); Serial.println("");
    request->send(200, "text/json", getSettings());
  });
  
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    Serial.printf("[REQUEST]\t%s\r\n", (const char*)data);
    Serial.print("URL: "); Serial.println(request->url());
    
    if (request->url() == "/api/savesettings") {
      saveSettings(request, data);
      request->send(200, "text/json", "OK");

      delay(500);
      ESP.restart();
    } else {
      request->send(404);
    }
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
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
  
  const char* devicename = doc["devicename"]; // "Pool pump"
  deviceName = devicename;

  Serial.print("Device name: "); Serial.println(deviceName);
  
  File settingsFile = SPIFFS.open("/settings.json", "w");
  StaticJsonDocument<1024> settingsDoc;
  settingsDoc["devicename"] = deviceName;

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
  
  const char* devicename = doc["devicename"]; // "12345678901234567890123456789012"
  deviceName = devicename;
  
  Serial.print("Device name: "); Serial.println(deviceName);
  
  settingsFile.close();
}

String getLocalTime() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
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
  SPIFFS.begin();

  loadDeviceSettings();
  connectToWifi();
  configureUrlRoutes();
  configureOTA();

  String dnsName = deviceName;
  dnsName.replace(" ", "");
  if(!MDNS.begin(dnsName)) {
     Serial.println("Error starting mDNS");
  }

  configTime(gmtOffset_sec, 0, ntpServer);
  
  server.begin();
}

void loop() {
  ArduinoOTA.handle();
}
