# ESP32-Gas-Sensor-Telemetry-Node

![ESP32](https://img.shields.io/badge/ESP32-100000?style=for-the-badge&logo=espressif&logoColor=white)
![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)
![MQTT](https://img.shields.io/badge/MQTT-660066?style=for-the-badge&logo=mqtt&logoColor=white)

## 📡 Overview
This repository contains the firmware for a secure, two-way environmental monitoring IoT node. Built on the ESP32 platform, this device reads analog data from a MICS-5524 gas sensor, mathematically converts the raw voltage into a Parts Per Million (PPM) value using a custom power-law curve, and streams the telemetry over a secure TLS MQTT connection.

### ✨ Key Features
* **Two-Way Telemetry:** Streams real-time gas concentrations and accepts dynamic, over-the-air threshold updates via JSON payloads.
* **Self-Healing Network:** Features dual-layer reconnection logic for both Wi-Fi dropouts and MQTT broker disconnects.
* **Dynamic Wi-Fi Provisioning:** Uses `WiFiManager` to spin up a local captive portal (`Gas Sensor Set-up`) for headless Wi-Fi configuration.
* **Non-Blocking Architecture:** Uses `millis()` timers instead of `delay()` to ensure the network heartbeat and data processing never freeze.

---

**[🔌 View Interactive Circuit Diagram on Cirkit Designer](https://app.cirkitdesigner.com/project/92c8706a-018b-42b5-a8bb-7b8d3b30e54d)**

## ⚡ Hardware Architecture

| Component | ESP32 Pin | Engineering Notes |
| :--- | :--- | :--- |
| **MICS-5524 (AOUT)** | `GPIO 35` | 12-bit ADC input. |
| **Buzzer Alarm** | `GPIO 25` | Driven via a 2N2222 NPN transistor (Low-Side Switch) with a 1kΩ base resistor. |
| **Power Supply** | `VIN` / `5V` | 5V supplied via MT3608 Boost Converter. |
| **Main Power Switch** | Inline | Placed on the 5V line between MT3608 `VOUT+` and ESP32 `VIN`. |

> ⚠️ **CRITICAL SAFETY WARNING:** Always flip the main rocker switch to the **OFF** position to isolate the battery circuit before plugging the ESP32 into a computer via USB. Failure to do so will result in a power collision that can permanently damage the USB controller.

---

## 🔄 MQTT API Contract
The device communicates with the frontend dashboard via a HiveMQ Cloud broker over Port `8883`. All payloads are formatted as JSON.

### Outbound Telemetry (Device ➔ Cloud)
The frontend application should `subscribe` to these topics:

#### Topic: `Sensor/Readings` (Published every 2 seconds)
```json
{
  "GAS_PPM": 4.52,
  "Status": "Safe" 
}
```
Status enum: "Safe", "Warning", "Critical Alert"

#### Topic: `Sensor/HourlyAverage` (Published every 60 minutes)
```json
{
  "GAS_PPM": 4.52,
  "Status": "Safe" 
}
```

#### Topic: `Sensor/Status`
System lifecycle events (e.g., startup sequence, 60-second sensor warmup phase). Payload is standard text.

### Inbound Commands (Cloud ➔ Device)
The frontend application should `publish` to this topic to dynamically update hardware parameters.

### Topic: `Sensor/Commands`
Accepts a JSON payload to update the warning and critical thresholds. The device checks for specific keys independently, allowing partial or complete updates.

```json
{
  "newWarningThreshold": 6.5,
  "newCriticalThreshold": 12.0
}
```

### 🚀 Local Installation & Setup
## 1. Clone the repository:
```bash
git clone [https://github.com/yourusername/ESP32-Gas-Sensor-Telemetry-Node.git](https://github.com/yourusername/ESP32-Gas-Sensor-Telemetry-Node.git)
```

## 2. Install Required Libraries (Arduino IDE / PlatformIO):

 `WiFiManager` by tzapu  
 
 `PubSubClient` by Nick O'Leary
 
 `ArduinoJson` by Benoit Blanchon

 ## 3. Configure Secrets (Do NOT commit to version control):
Create a file named secrets.h in the root directory. This file is ignored by .gitignore to protect your broker credentials.

```cpp
// secrets.h
#define SECRET_MQTT_SERVER "your_broker_address.hivemq.cloud"
#define SECRET_MQTT_PORT 8883
#define SECRET_MQTT_USER "your_username"
#define SECRET_MQTT_PASS "your_password"
```

## 4. Compile and Upload to the ESP32. Upon first boot, connect to the Gas Sensor Set-up Wi-Fi network from your phone to input your local router credentials.

