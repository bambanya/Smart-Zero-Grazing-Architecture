<img width="2976" height="3968" alt="IMG_20260520_135137_365@-1710966157" src="https://github.com/user-attachments/assets/0b338fb0-329f-4762-be9b-7daebaacf4c3" /># Smart Zero Grazing Telemetry & Automation Architecture

![C++](https://img.shields.io/badge/Language-C++-blue.svg)
![Platform](https://img.shields.io/badge/Platform-ESP32-green.svg)
![Status](https://img.shields.io/badge/Status-Prototype_Validated-success.svg)
![License](https://img.shields.io/badge/License-MIT-lightgray.svg)

**Engineer:** Matthew Kaingu Saro (Registration: ECE/B/01-02327/2021)  
**Institution:** Masinde Muliro University of Science and Technology (MMUST)  
**Location:** Kenya

## 📖 Project Overview
The **Smart Zero Grazing System** is an autonomous, fault-tolerant Cyber-Physical System (CPS) engineered for precision livestock farming. Traditional automated agricultural systems deployed in rural areas frequently suffer from catastrophic hardware failures due to power grid instability, dry-running pumps, and inductive electrical noise. 

This architecture solves these electromechanical vulnerabilities by bridging an ESP32 edge-controller with a robust hardware-interlocked network. It transitions farming from broad, herd-level estimations to precise, individual-level automated management, tracking climate, fluid levels, feed mass, and individual cattle attendance via RFID.
<img width="2976" height="3968" alt="IMG_20260520_135137_365@-1710966157" src="https://github.com/user-attachments/assets/dbadac73-9613-428a-95ec-b6dc01028651" />



## ⚡ Core Engineering Features & Fault Tolerance

This system was specifically designed to survive harsh electrical and environmental conditions through strict software algorithms and hardware isolation.

### 1. Algorithmic Dry-Run Protection (Watchdog Timer)
Standard closed-loop systems will run a water pump indefinitely if the source tank is empty, leading to thermal runaway and motor destruction. This firmware utilizes a strict **3-Second Watchdog Timer**. If the primary HC-SR04 ultrasonic sensor does not verify fluid reaching the 5.0 cm shutoff mark within 3000 milliseconds of pump activation, the ESP32 mathematically deduces an empty source, forcefully cuts relay power, and locks the system into an error state.

### 2. Electromagnetic Interference (EMI) Mitigation
High-frequency switching noise from the 12V DC pump frequently corrupted the delicate clock signals on the I2C (LCD) and SPI (RFID) buses. 
* **The Heartbeat Protocol:** A software routine silently re-initializes the MFRC522 SPI registers every 3 seconds, ensuring the 13.56 MHz antenna field remains energized and immune to EMI crashes.
* **Bus Isolation:** The I2C display receives a localized decoupling capacitor and a 500ms software delay during pump actuation to allow inductive noise to dissipate before refreshing the screen.

### 3. Power Isolation & Brownout Protection
The SIM800L cellular module demands instantaneous 2.0A current spikes during tower handshakes, causing fatal logic brownouts on the ESP32. This is mitigated by isolating the GSM module on a dedicated **LM2596 DC-DC Buck Converter** (tuned exactly to 4.0V) paired with a 470µF localized charge reservoir, establishing 100% immunity to transmission voltage sags.

### 4. Hardware Interlocking & Inductive Kickback Firewall
To prevent the collapsing magnetic field of the DC motor from welding the relay contacts or destroying the microcontroller, the system employs a **TIP120 Darlington Transistor** as an active-HIGH logic firewall, shunted by a **1N4007 Flyback Diode** to safely circulate high-voltage reverse spikes.


<img width="990" height="645" alt="Screenshot_20260521-191840" src="https://github.com/user-attachments/assets/755aeb26-e1c0-4ed3-ac03-fc34c6561cb6" />



## 📡 Dual-Path Telemetry Architecture

The system provides operators with both immediate on-site diagnostics and long-term remote analytics.

* **On-Site Dynamic UI:** A 16x2 I2C LCD operates in a 3-state carousel, displaying standard fluid levels, individual cattle ID scans, and immediately overriding to a high-priority `"ERR: PUMP DRY!"` or `"WARNING: SEWER FULL!"` visual and SMS alert when hardware intervention is required.
* **Remote Cloud Database:** Bypassing third-party servers, the ESP32 executes secure HTTPS GET/POST requests to a custom Google Apps Script. Telemetry is parsed into an 8-column Google Sheet (Date, Time, Temp, Humidity, Water Depth, Sewer Depth, Tag ID, Feed Mass) for continuous data logging.

---

## 🧰 Primary Bill of Materials (BOM)
* **Microcontroller:** ESP32 DevKit V1
* **Connectivity:** SIM800L GSM/GPRS Module (Custom 8.3cm $\lambda/4$ tuned antenna)
* **Identification:** MFRC522 13.56 MHz RFID Reader
* **Sensors:** 2x HC-SR04 (Ultrasonic), DHT11 (Climate), 5kg Load Cell + HX711 24-bit ADC
* **Actuators:** 12V DC Water Pump, MG996R High-Torque PWM Micro Servo
* **Power Regulation:** LM2596 Buck Converter, TIP120, Opto-isolated 5V Relay

---

## 🚀 Setup & Execution

1. **Hardware Assembly:** Wire the components strictly according to the `/assets/circuit_diagram.png` schematic. Ensure the LM2596 is tuned to 4.0V *before* connecting the SIM800L.
2. **Dependencies:** Install `MFRC522.h`, `HX711.h`, and `LiquidCrystal_I2C.h` via the Arduino Library Manager.
3. **Calibration:** The load cell firmware contains an inverted calibration scalar (`-100.0`) to account for specific physical strain-gauge mounting. Adjust this based on your specific HX711 hardware setup.
4. **Cloud Hook:** Insert your specific Google Apps Script Web App URL and local Wi-Fi credentials into the ESP32 header definitions before flashing.
