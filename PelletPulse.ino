// Required libraries: WiFi, WebServer, HTTPClient, ArduinoJson, FS, LittleFS, WiFiManager, ESPmDNS, Update
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <time.h>
#include <Update.h>
#include <WiFiClientSecure.h>

#define CONFIG_FILE "/config.json"
#define FW_VERSION "v1.2.1"  // 👈 must match your build tag
#define GITHUB_USER "benbonn"
#define GITHUB_REPO "pelletpulse"

WebServer server(80);
unsigned long lastPollTime = 0;
String lastStatusMessage = "No activity yet.";

// Config structure
struct Config {
  String wifiSSID;
  String wifiPassword;
  String oekofenIp;
  uint16_t oekofenPort;
  String oekofenPassword;
  uint32_t pollInterval;
  String apiUrl;
  String apiKey;
  String format;
  String heatMeter;
} config;

String getIsoTimestamp() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "";
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

String mapHeaterState(String value) {
  if (value == "Aus" || value == "2147483648" || value == "1073741840") return "Off";
  if (value == "Softstart" || value == "8") return "Softstart";
  if (value == "Zündung" || value == "4") return "Ignition";
  if (value == "Leistungsbrand" || value == "16") return "High-Performance Combustion";
  if (value == "Nachlauf" || value == "32") return "Trailing";
  return value;
}

bool loadConfig() {
  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS");
    setDefaultConfig();
    return false;
  }

  if (!LittleFS.exists(CONFIG_FILE)) {
    Serial.println("Config file not found. Using defaults.");
    setDefaultConfig();
    return false;
  }

  File file = LittleFS.open(CONFIG_FILE, "r");
  if (!file) {
    Serial.println("Failed to open config file. Using defaults.");
    setDefaultConfig();
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    Serial.println("Failed to parse config. Using defaults.");
    setDefaultConfig();
    return false;
  }

  config.wifiSSID = doc["wifiSSID"] | "";
  config.wifiPassword = doc["wifiPassword"] | "";
  config.oekofenIp = doc["oekofenIp"] | "192.168.1.100";
  config.oekofenPort = doc["oekofenPort"] | 4321;
  config.oekofenPassword = doc["oekofenPassword"] | "";
  config.pollInterval = doc["pollInterval"] | 60;
  config.apiUrl = doc["apiUrl"] | "";
  config.apiKey = doc["apiKey"] | "";
  config.format = doc["format"] | "HeatGeniusAI";
  config.heatMeter = doc["heatMeter"] | "";

  Serial.println("Config loaded:");
  Serial.println("Port: " + String(config.oekofenPort));
  Serial.println("Poll Interval: " + String(config.pollInterval));
  return true;
}

bool isNewerVersion(String latest, String current) {
  latest.replace("v", "");
  current.replace("v", "");

  int lMajor = 0, lMinor = 0, lPatch = 0;
  int cMajor = 0, cMinor = 0, cPatch = 0;

  sscanf(latest.c_str(), "%d.%d.%d", &lMajor, &lMinor, &lPatch);
  sscanf(current.c_str(), "%d.%d.%d", &cMajor, &cMinor, &cPatch);

  if (lMajor != cMajor) return lMajor > cMajor;
  if (lMinor != cMinor) return lMinor > cMinor;
  return lPatch > cPatch;
}



void setDefaultConfig() {
  config.wifiSSID = "";
  config.wifiPassword = "";
  config.oekofenIp = "192.168.1.100";
  config.oekofenPort = 4321;
  config.oekofenPassword = "";
  config.pollInterval = 60;
  config.apiUrl = "";
  config.apiKey = "";
  config.format = "HeatGeniusAI";
  config.heatMeter = "";
}

void saveConfig() {
  StaticJsonDocument<512> doc;
  doc["wifiSSID"] = config.wifiSSID;
  doc["wifiPassword"] = config.wifiPassword;
  doc["oekofenIp"] = config.oekofenIp;
  doc["oekofenPort"] = config.oekofenPort;
  doc["oekofenPassword"] = config.oekofenPassword;
  doc["pollInterval"] = config.pollInterval;
  doc["apiUrl"] = config.apiUrl;
  doc["apiKey"] = config.apiKey;
  doc["format"] = config.format;
  doc["heatMeter"] = config.heatMeter;

  File file = LittleFS.open(CONFIG_FILE, "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return;
  }

  serializeJson(doc, file);
  file.flush();  // Ensure it writes to flash
  file.close();
  Serial.println("Config saved");
}

void handleSave() {
  Serial.println("====== handleSave() called ======");

  // Debug raw form inputs
  Serial.println("Raw input values:");
  Serial.println("oekofenIp: " + server.arg("oekofenIp"));
  Serial.println("oekofenPort: " + server.arg("oekofenPort"));
  Serial.println("oekofenPassword: " + server.arg("oekofenPassword"));
  Serial.println("pollInterval: " + server.arg("pollInterval"));
  Serial.println("apiUrl: " + server.arg("apiUrl"));
  Serial.println("apiKey: " + server.arg("apiKey"));
  Serial.println("format: " + server.arg("format"));
  Serial.println("heatMeter: " + server.arg("heatMeter"));

  // Assign with safety for numeric values
  String ip = server.arg("oekofenIp"); ip.trim();
  config.oekofenIp = ip;

  config.oekofenPort = server.arg("oekofenPort").toInt();
  config.oekofenPort = config.oekofenPort > 0 ? config.oekofenPort : 4321;

  String pw = server.arg("oekofenPassword"); pw.trim();
  config.oekofenPassword = pw;

  config.pollInterval = server.arg("pollInterval").toInt();
  config.pollInterval = config.pollInterval > 0 ? config.pollInterval : 60;

  config.apiUrl = server.arg("apiUrl");
  config.apiKey = server.arg("apiKey");

  String fmt = server.arg("format"); fmt.trim();
  config.format = fmt;

  String meter = server.arg("heatMeter"); meter.trim();
  config.heatMeter = meter;

  Serial.println("Parsed config:");
  Serial.println("Port: " + String(config.oekofenPort));
  Serial.println("Poll Interval: " + String(config.pollInterval));

  saveConfig();
  delay(500);  // Flush LittleFS

  // Modern confirmation page with redirect
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset='UTF-8'>
      <meta name='viewport' content='width=device-width, initial-scale=1.0'>
      <meta http-equiv='refresh' content='5;url=/' />
      <title>Settings Saved</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background: #f4f4f4;
          margin: 0;
          padding: 20px;
        }
        .container {
          max-width: 500px;
          margin: auto;
          background: white;
          padding: 20px;
          border-radius: 8px;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
          text-align: center;
        }
        h2 {
          color: #28a745;
        }
        p {
          color: #555;
        }
        a {
          color: #007bff;
          text-decoration: none;
        }
        a:hover {
          text-decoration: underline;
        }
      </style>
    </head>
    <body>
      <div class='container'>
        <h2>✅ Settings Saved</h2>
        <p>The device will reboot shortly.</p>
        <p>Redirecting to the <a href='/'>configuration page</a> in 5 seconds...</p>
      </div>
    </body>
    </html>
  )rawliteral";

  server.send(200, "text/html", html);
  delay(1500);
  ESP.restart();
}





void handleResetWiFi() {
  WiFi.disconnect(true, true);
  server.send(200, "text/html", "<p>WiFi settings erased. Rebooting...</p>");
  delay(1000);
  ESP.restart();
}

void handleRoot() {
  // Debug output
  // Serial.println("Rendering config:");
  //Serial.println("Port: " + String(config.oekofenPort));
  //Serial.println("Poll Interval: " + String(config.pollInterval));

  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset='UTF-8'>
      <meta name='viewport' content='width=device-width, initial-scale=1.0'>
      <title>Pellet Pulse Config</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background: #f4f4f4;
          margin: 0;
          padding: 20px;
        }
        .container {
          max-width: 500px;
          margin: auto;
          background: white;
          padding: 20px;
          border-radius: 8px;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        h2 {
          color: #333;
        }
        label {
          font-weight: bold;
        }
        input, select {
          width: 100%;
          padding: 10px;
          margin: 8px 0 16px 0;
          border: 1px solid #ccc;
          border-radius: 4px;
          box-sizing: border-box;
        }
        input[type="submit"], button {
          background-color: #28a745;
          color: white;
          border: none;
          cursor: pointer;
          padding: 12px;
          width: 100%;
          border-radius: 4px;
          font-size: 16px;
        }
        input[type="submit"]:hover, button:hover {
          background-color: #218838;
        }
        a {
          display: block;
          margin-top: 10px;
          color: #007bff;
          text-decoration: none;
          text-align: center;
        }
        a:hover {
          text-decoration: underline;
        }
        .footer {
          text-align: center;
          margin-top: 20px;
          color: #999;
        }
      </style>
      <script>
        document.addEventListener("DOMContentLoaded", function () {
          document.querySelector("form").addEventListener("submit", function () {
            this.querySelectorAll("input[type='text'], input:not([type])").forEach(function (el) {
              el.value = el.value.trim();
            });
          });
        });
      </script>
    </head>
    <body>
      <div class='container'>
        <h2>Ökofen Configuration</h2>
        <form method='POST' action='/save'>
          <label>IP:</label>
          <input name='oekofenIp' type='text' value='
  )rawliteral";
  html += config.oekofenIp;
  html += R"rawliteral(' placeholder='192.168.1.100'>

          <label>Port:</label>
          <input name='oekofenPort' type='text' value='
  )rawliteral";
  html += String(config.oekofenPort);
  html += R"rawliteral(' placeholder='4321'>

          <label>Password:</label>
          <input name='oekofenPassword' type='text' value='
  )rawliteral";
  html += config.oekofenPassword;
  html += R"rawliteral(' placeholder='Your password'>

          <label>Poll Interval (s):</label>
          <input name='pollInterval' type='text' value='
  )rawliteral";
  html += String(config.pollInterval);
  html += R"rawliteral(' placeholder='60'>

          <h2>API Endpoint</h2>
          <label>URL:</label>
          <input name='apiUrl' type='text' value='
  )rawliteral";
  html += config.apiUrl;
  html += R"rawliteral(' placeholder='https://...'>
          
          <label>API Key:</label>
          <input name='apiKey' type='text' value='
  )rawliteral";
  html += config.apiKey;
  html += R"rawliteral('>

          <label>Format:</label>
          <select name='format'>
            <option value='HeatGeniusAI'";
  if (config.format == "HeatGeniusAI") html += " selected";
  html += R"rawliteral(>HeatGeniusAI</option>
            <option value='Original Payload'";
  if (config.format == "Original Payload") html += " selected";
  html += R"rawliteral(>Original Payload</option>
          </select>

          <h2>Manual Entry</h2>
          <label>Heat Meter (kWh):</label>
          <input name='heatMeter' type='text' value='
  )rawliteral";
  html += config.heatMeter;
  html += R"rawliteral(' placeholder='38129.8'>

          <input type='submit' value='Save Settings'>
        </form>

        <form action='/resetwifi' method='POST'>
          <button type='submit'>Reset Wi-Fi Settings</button>
        </form>

                <a href='/status'>Go to Status Page</a>
        <a href='/httpupdate'>Check for OTA Update</a>
)rawliteral";  // 🚫 End the raw literal here!

  // ✅ Now safely insert FW_VERSION
  html += "<div class='footer'>Version " + String(FW_VERSION) + " —  See Release Notes on GitHub for details</div>";

  // ✅ Then finish the rest of the HTML
  html += R"rawliteral(
      </div>
    </body>
    </html>
  )rawliteral";



  server.send(200, "text/html", html);
}

void handleOTAUpdate() {
  String currentVersion = FW_VERSION;
  String latestVersion = "unknown";
  String releaseNotes = "";
  String binUrl = "";
  String otaMessage = "";

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient github;
  github.begin(secureClient, "https://api.github.com/repos/benbonn/pelletpulse/releases/latest");
  github.addHeader("User-Agent", "ESP32-OTA");

  int code = github.GET();
  if (code == HTTP_CODE_OK) {
    String response = github.getString();
    github.end();

    StaticJsonDocument<8192> doc;
    DeserializationError err = deserializeJson(doc, response);

    if (!err) {
      latestVersion = doc["tag_name"].as<String>();
      releaseNotes = doc["body"].as<String>();

      for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        if (asset["name"] == "firmware.bin") {
          binUrl = asset["browser_download_url"].as<String>();
          break;
        }
      }

      bool githubIsNewer = isNewerVersion(latestVersion, currentVersion);

      if (latestVersion == currentVersion) {
        otaMessage = "You're already running the latest firmware.";
      } else if (!githubIsNewer) {
        otaMessage = "You are running a newer firmware than the latest GitHub release.";
      } else {
        otaMessage = "New version available!";
      }

    } else {
      otaMessage = "Failed to parse release JSON.";
    }
  } else {
    otaMessage = "Failed to contact GitHub. HTTP code: " + String(code);
  }

  // Build response
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>OTA Check</title>"
                "<style>body{font-family:sans-serif;background:#f9f9f9;padding:2em;}div{max-width:600px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 0 10px rgba(0,0,0,0.1);}pre{white-space:pre-wrap;word-wrap:break-word;}</style>"
                "</head><body><div>";

  html += "<h2>OTA Update Status</h2>";
  html += "<p><strong>Current version:</strong> " + currentVersion + "</p>";
  html += "<p><strong>Latest version:</strong> " + latestVersion + "</p>";
  html += "<p><strong>Status:</strong> " + otaMessage + "</p>";

  bool githubIsNewer = isNewerVersion(latestVersion, currentVersion);
if (!githubIsNewer && latestVersion != currentVersion && binUrl != "") {
  html += "<div style='background:#fff3cd;border:1px solid #ffeeba;color:#856404;padding:12px;border-radius:6px;margin:16px 0;'>"
          "⚠️ You are running a development build (" + currentVersion + ") that is newer than the latest public release (" + latestVersion + ")."
          "<br>You may still choose to downgrade:</div>";
  html += "<form method='POST' action='/performupdate'>";
  html += "<input type='submit' value='Revert to " + latestVersion + "' style='background:#ffc107;color:black;border:none;padding:12px 20px;border-radius:4px;font-size:16px;cursor:pointer;'>";
  html += "</form>";
}


  if (githubIsNewer && binUrl != "") {
    html += "<form method='POST' action='/performupdate'>";
    html += "<input type='submit' value='Update Now' style='background:#28a745;color:white;border:none;padding:12px 20px;border-radius:4px;font-size:16px;cursor:pointer;'>";
    html += "</form>";
    html += "<p><strong>Release Notes:</strong><br><pre>" + releaseNotes + "</pre></p>";
  }

  html += "<p><a href='/status'>Back to Status Page</a></p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}




void handlePerformUpdate() {
  String otaMessage;
  bool shouldRespond = true;

  Serial.println("[OTA] Manual update triggered.");

  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  // Step 1: Contact GitHub for latest release
  HTTPClient github;
  github.begin(secureClient, "https://api.github.com/repos/benbonn/pelletpulse/releases/latest");
  github.addHeader("User-Agent", "ESP32-OTA");

  int code = github.GET();
  if (code != HTTP_CODE_OK) {
    otaMessage = "[OTA] GitHub API error. HTTP code: " + String(code);
  } else {
    String response = github.getString();
    github.end();

    StaticJsonDocument<8192> doc;
    DeserializationError err = deserializeJson(doc, response);

    if (err) {
      otaMessage = "[OTA] JSON parse error: " + String(err.c_str());
    } else {
      String latestVersion = doc["tag_name"].as<String>();
      String binUrl = "";

      for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        if (asset["name"] == "firmware.bin") {
          binUrl = asset["browser_download_url"].as<String>();
          break;
        }
      }

      if (binUrl == "") {
        otaMessage = "[OTA] No firmware.bin found in release assets.";
      } else if (latestVersion == FW_VERSION) {
        otaMessage = "[OTA] Already running latest version (" + String(FW_VERSION) + ").";
      } else {
        Serial.println("[OTA] New version available: " + latestVersion);
        Serial.println("[OTA] Resolving redirect for binary...");

        HTTPClient resolver;
        resolver.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        resolver.begin(secureClient, binUrl);
        int resCode = resolver.GET();
        String resolvedUrl = resolver.getLocation();
        resolver.end();

        if (resCode != HTTP_CODE_OK || resolvedUrl == "") {
          otaMessage = "[OTA] Failed to resolve firmware URL. HTTP code: " + String(resCode);
        } else {
          Serial.println("[OTA] Resolved firmware URL: " + resolvedUrl);

          HTTPClient otaHttp;
          otaHttp.setTimeout(30000);
          otaHttp.begin(secureClient, resolvedUrl);

          Serial.println("[OTA] Starting OTA update...");
          Serial.println("[OTA] Free heap: " + String(ESP.getFreeHeap()));

          t_httpUpdate_return ret = httpUpdate.update(otaHttp, resolvedUrl);

          switch (ret) {
            case HTTP_UPDATE_FAILED:
              otaMessage = "[OTA] Failed. Error (" + String(httpUpdate.getLastError()) + "): " +
                           httpUpdate.getLastErrorString();
              break;

            case HTTP_UPDATE_NO_UPDATES:
              otaMessage = "[OTA] No update available.";
              break;

            case HTTP_UPDATE_OK:
  Serial.println("[OTA] Update successful. Rebooting...");
  delay(200); // Let Serial flush
  server.send(200, "text/html", R"rawliteral(
    <!DOCTYPE html>
    <html><head><meta http-equiv='refresh' content='30;url=/status'>
    <title>OTA Success</title></head><body>
    <p>OTA update completed. Rebooting now...</p>
    <p>You’ll be redirected to the status page shortly.</p>
    </body></html>
  )rawliteral");
  delay(300); // let HTML send finish
  ESP.restart();
  return;
          }
        }
      }
    }
  }

  if (shouldRespond) {
    server.send(200, "text/html",
      "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
      "<title>OTA Result</title>"
      "<style>body{font-family:sans-serif;background:#f9f9f9;padding:2em;}div{max-width:600px;margin:auto;background:white;padding:20px;border-radius:8px;box-shadow:0 0 10px rgba(0,0,0,0.1);}</style>"
      "</head><body><div>"
      "<h2>OTA Update Result</h2>"
      "<p><strong>Status:</strong><br>" + otaMessage + "</p>"
      "<p><a href='/status'>Back to Status Page</a></p>"
      "</div></body></html>");
  }
}



void pollAndSend() {
  if (WiFi.status() != WL_CONNECTED) return;
  lastPollTime = millis();
  HTTPClient http;
  String url = "http://" + config.oekofenIp + ":" + String(config.oekofenPort) + "/" + config.oekofenPassword + "/all?";
  Serial.println("Polling URL: " + url);
  Serial.println("WiFi status: " + String(WiFi.status()));
  Serial.println("Local IP: " + WiFi.localIP().toString());
  http.begin(url);
  http.setTimeout(5000);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<20480> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
      lastStatusMessage = "[" + getIsoTimestamp() + "] Failed to parse Ökofen JSON: " + String(err.c_str());
      Serial.println(lastStatusMessage);
      return;
    }

    if (config.apiUrl != "") {
      String outJson;
      if (config.format == "HeatGeniusAI") {
        StaticJsonDocument<1024> outDoc;
        outDoc["timestamp"] = getIsoTimestamp();
        outDoc["pellets_used_yesterday"] = doc["pe1"]["L_pellets_yesterday"]["val"];
        outDoc["pellets_used_today"] = doc["pe1"]["L_pellets_today"]["val"];
        outDoc["current_storage"] = doc["pe1"]["L_storage_fill"]["val"];
        outDoc["max_pellet_storage"] = doc["pe1"]["L_storage_max"]["val"];
        outDoc["flow_id"] = "flow1";
        outDoc["flowtemperature_actual"] = doc["hk1"]["L_flowtemp_act"]["val"].as<float>() * 0.1;
        outDoc["outside_temperature"] = doc["system"]["L_ambient"]["val"].as<float>() * 0.1;
        outDoc["ww_circ_pump"] = doc["circ1"]["L_pump"]["val"];
        outDoc["ww_temperature_act"] = doc["ww1"]["L_ontemp_act"]["val"];
        outDoc["ww_circ_pump_temp"] = doc["circ1"]["L_ret_temp"]["val"].as<float>() * 0.1;
        outDoc["hc2_flow_temperature_act"] = doc["hk2"]["L_flowtemp_act"]["val"].as<float>() * 0.1;
        outDoc["heater_average_runtime"] = doc["pe1"]["L_avg_runtime"]["val"];
        outDoc["hc1_flow_temperature_set"] = doc["hk1"]["L_flowtemp_set"]["val"].as<float>() * 0.1;
        outDoc["hc1_pump"] = doc["hk1"]["L_pump"]["val"];
        outDoc["hc2_flow_temperature_set"] = doc["hk2"]["L_flowtemp_set"]["val"].as<float>() * 0.1;
        outDoc["hc2_pump"] = doc["hk2"]["L_pump"]["val"];
        outDoc["heater_state"] = mapHeaterState(String(doc["pe1"]["L_state"]["val"].as<String>()));
        if (config.heatMeter != "") {
          outDoc["heat_energy_delivered"] = config.heatMeter.toFloat();
        }
        serializeJson(outDoc, outJson);
      } else {
        outJson = payload;
      }
      HTTPClient post;
      post.begin(config.apiUrl);
      post.addHeader("Content-Type", "application/json");
      if (config.apiKey != "") post.addHeader("Authorization", config.apiKey);
      int code = post.POST(outJson);
      lastStatusMessage = (code > 0)
        ? "[" + getIsoTimestamp() + "] Ökofen pull and API send successful."
        : "[" + getIsoTimestamp() + "] API send failed with HTTP code: " + String(code);
      post.end();
    }
  } else {
    lastStatusMessage = "[" + getIsoTimestamp() + "] Ökofen poll failed, HTTP code: " + String(httpCode);
  }
  http.end();
  Serial.println(lastStatusMessage);
}

void handleStatus() {
  String html = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
      <meta charset='UTF-8'>
      <meta name='viewport' content='width=device-width, initial-scale=1.0'>
      <meta http-equiv='refresh' content='10'>
      <title>Pellet Pulse Status</title>
      <style>
        body {
          font-family: Arial, sans-serif;
          background: #f4f4f4;
          margin: 0;
          padding: 20px;
        }
        .container {
          max-width: 600px;
          margin: auto;
          background: white;
          padding: 20px;
          border-radius: 8px;
          box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        h2 {
          color: #333;
        }
        p, pre {
          font-size: 14px;
          color: #444;
        }
        a {
          display: inline-block;
          margin-top: 10px;
          color: #007bff;
          text-decoration: none;
        }
        a:hover {
          text-decoration: underline;
        }
      </style>
    </head>
    <body>
      <div class='container'>
        <h2>Pellet Pulse Status</h2>
  )rawliteral";

  html += "<p><strong>WiFi SSID:</strong> " + WiFi.SSID() + "</p>";
  html += "<p><strong>IP Address:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>Ökofen IP:</strong> " + config.oekofenIp + ":" + String(config.oekofenPort) + "</p>";
  html += "<p><strong>Polling Interval:</strong> " + String(config.pollInterval) + " seconds</p>";
  html += "<p><strong>API Endpoint:</strong> " + config.apiUrl + "</p>";
  html += "<p><strong>Uptime:</strong> " + String(millis() / 1000) + " seconds</p>";
  html += "<pre>" + lastStatusMessage + "</pre>";
  html += "<a href='/'>Back to Config</a>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}


void setup() {
  Serial.begin(115200);
  LittleFS.begin();
  loadConfig();

  WiFiManager wm;
  bool res;
  if (config.wifiSSID == "" || config.wifiPassword == "") {
    res = wm.autoConnect("PelletPulse_Config");
  } else {
    res = wm.autoConnect("PelletPulse_Config", config.wifiPassword.c_str());
  }
  if (!res) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  configTime(0, 0, "pool.ntp.org", "time.nist.gov");

  if (MDNS.begin("pelletpulse")) {
    Serial.println("mDNS responder started: http://pelletpulse.local");
  } else {
    Serial.println("Error setting up mDNS responder!");
  }

  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/resetwifi", HTTP_POST, handleResetWiFi);
  server.on("/status", handleStatus);
  server.on("/httpupdate", handleOTAUpdate);
  server.on("/httpupdate", handleOTAUpdate);
  server.on("/performupdate", HTTP_POST, handlePerformUpdate);
  server.begin();
}

void loop() {
  server.handleClient();
  static unsigned long lastMillis = 0;
  if (millis() - lastMillis > config.pollInterval * 1000UL) {
    lastMillis = millis();
    pollAndSend();
  }
}
