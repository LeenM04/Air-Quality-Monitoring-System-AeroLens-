# AeroLens: IoT Air Quality Monitoring System 🌍🛰️

**AeroLens** is an integrated IoT solution designed for real-time environmental surveillance. Using the **ESP32** microcontroller and a suite of gas sensors, the system detects pollutants and streams data to the **ThingSpeak** cloud for advanced visualization and analysis.

## 🚀 Key Features
* **Real-Time Pollutant Tracking:** Monitors concentrations of CO, Smoke, LPG, and Ammonia.
* **Cloud-Native Data Logging:** Leverages Wi-Fi to transmit live readings to the **ThingSpeak** IoT platform.
* **Multi-Sensor Fusion:** Utilizes MQ-2, MQ-9, and MQ-135 sensors for comprehensive air quality assessment.
* **Sensor Stability:** Implementation of a pre-heating delay to ensure stable and accurate sensor readings before data transmission.

## 🧠 System Architecture
The framework consists of a coordinated hardware-software stack:
* **Microcontroller:** ESP32 (utilizing built-in Wi-Fi for wireless connectivity).
* **Sensors Suite:**
    * **MQ-135:** Specialized for Ammonia (NH3), Benzene, and overall Air Quality Index.
    * **MQ-2:** Sensitive to Smoke, LPG, and Methane detection.
    * **MQ-9:** Optimized for Carbon Monoxide (CO) and flammable gas monitoring.
* **Data Platform:** Real-time dashboards with automated graphical charting on ThingSpeak.

## 🛠️ Tech Stack
* **Hardware:** ESP32 Microcontroller, MQ Gas Sensors, Breadboard, and Analog wiring.
* **Firmware:** C++ (Arduino IDE Framework).
* **Libraries:** `WiFi.h` and `ThingSpeak.h`.
* **Communication:** HTTP REST API over local Wi-Fi networks.

## 📈 Impact & Results
AeroLens provides critical environmental insights through:
1. **Pollution Trend Mapping:** Identifying peaks in gas concentrations over time.
2. **Environmental Safety:** Providing a modular foundation for real-time gas leak detection and alerts.
3. **Remote Accessibility:** Continuous monitoring of air quality from any location via the cloud.

## 👥 Authors
* Jana Godieh
* **Leen Almasarweh**
* Dana Abu-Alruz
