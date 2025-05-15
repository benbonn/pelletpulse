# Pellet Pulse

**Pellet Pulse** is an ESP32-based monitoring system for Ökofen pellet heating systems. It retrieves live data from the heater's internal HTTP interface and publishes it for use in smart home automation, energy tracking, and more. The system is designed to be standalone, Wi-Fi enabled, and easily configurable via a built-in web interface.

---

## 🔧 Features

  
- 🔌 Connects to Ökofen pellet heating systems via HTTP  
- 📊 Parses heating metrics like temperature, power, energy, and storage levels  
- ☁️ Sends data to a configurable HTTP endpoint (e.g., Supabase, Node-RED)  
- 🌐 Web interface for:
  - Configuring IP, port, password, and polling interval
  - Viewing live status
  - Testing API key and data sending
- 📶 ESP32 firmware with OTA update support
- 💾 Persistent configuration via LittleFS
---

## 💻 Screenshots

- Configuration:

![image](https://github.com/user-attachments/assets/cec63fcf-e1e4-4e39-8c21-353e84c1a1c9)
---

- Status Page:

![image](https://github.com/user-attachments/assets/c5801895-64ad-4f66-875a-ace3f345951b)

---

- OTA Update:

![image](https://github.com/user-attachments/assets/bdb65ade-2775-4e10-a32b-0f002a90f270)
---

- Example result in HeatGeniusAI:
- 
![image](https://github.com/user-attachments/assets/0ab6aaa1-8568-4977-bb58-c764cff8a1a9)

  ---

## 🧰 Hardware Requirements

- ESP32 development board (USB-C recommended)  
- Case optional

---

## 💻 Software Requirements

- Arduino IDE (or PlatformIO)  
- ESP32 board support installed  
- Libraries:
  - `WiFi.h`, `WebServer.h`, `HTTPClient.h`
  - `ArduinoJson`
  - `Update.h`
  - `LittleFS`
  

---

## 🚀 Getting Started

1. **Flash the Firmware**  
   Upload the code to your ESP32 via Arduino IDE or PlatformIO.

2. **Wi-Fi Setup**  
   On first boot, connect to the ESP32's access point (`PelletPulse`) and open the configuration portal. Connect to your Wi-Fi.

3. **Device Configuration**  
   Browse to http://pelletpulse.local/ Enter your Ökofen IP, port, password, polling interval, and endpoint URL

4. **Save and Start**  
   After saving, the ESP32 reboots and begins polling and uploading data.

---

## 🧾 Example JSON Payload

🛡️ Security
API key-based authentication via Authorization or x-api-key header

Configuration is stored securely on-device with LittleFS

🤝 Contributing
Pull requests, bug reports, and feature requests are welcome. Fork the repo and help us improve Pellet Pulse!

📜 License
This project is licensed under the MIT License.
