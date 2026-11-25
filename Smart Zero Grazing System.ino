#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <time.h> 
#include <HTTPClient.h> 
#include <WiFiClientSecure.h> 
#include "HX711.h" 

// --- WI-FI & NTP SETTINGS ---
const char* ssid = "ENTER WIFI NAME HERE";
const char* password = "ENTER PASSWORD HERE";
WebServer server(80);

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800; // UTC+3 for EAT
const int   daylightOffset_sec = 0;

// --- GOOGLE SHEETS WEB APP URL ---
String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbwjnZdQ04j64_ddft7yWCBdmUo1KWPeVVoNoYnOlky3E7tbXt7bXoA3UbKY0A8AD65l/exec";

// --- PIN DEFINITIONS ---
#define RST_PIN 4
#define SS_WATER_PIN 5 

#define WATER_TRIG 2
#define WATER_ECHO 34
#define RELAY_PIN 27   
#define SEWER_TRIG 25  
#define SEWER_ECHO 26  

#define SERVO_PIN 13
#define GSM_RX 16
#define GSM_TX 17
#define DHTPIN 14     
#define DHTTYPE DHT11 

#define LOADCELL_DOUT_PIN 32 
#define LOADCELL_SCK_PIN 33  

// --- DESTINATION PHONE NUMBER ---
String phoneNumber = "+254795212392"; 

// ==========================================
// --- 🐄 HERD DATABASE ---
// ==========================================
String UID_ANIMAL_1 = "95 F6 28 B6"; 
String UID_ANIMAL_2 = "17 14 80 85"; 
String UID_ANIMAL_3 = "A5 2B 72 B6"; 
String UID_ANIMAL_4 = "D3 EF 65 31"; 
// ==========================================

// --- OBJECT INITIALIZATION ---
MFRC522 rfidWater(SS_WATER_PIN, RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2); 
Servo feederServo;
DHT dht(DHTPIN, DHTTYPE); 
HX711 scale;

// --- SYSTEM VARIABLES & THRESHOLDS ---
const float MAX_EMPTY_WATER = 10.0; 
const float MIN_FULL_WATER = 5.0;   
const float MIN_FULL_SEWER = 4.0;  

// UPDATED: 3 seconds Dry-Run Protection limit
const unsigned long MAX_PUMP_RUN_TIME = 3000; 

const float TARGET_FEED_WEIGHT = 200.0; 

// --- SERVO HARDWARE CALIBRATION ---
const int SERVO_CLOSED_ANGLE = 60;  
const int SERVO_OPEN_ANGLE = 0;   

float CALIBRATION_FACTOR = -100.0; 

bool isFeederGateOpen = false;
bool isPumpRunning = false;
bool sewerWarningActive = false;
bool pumpErrorActive = false; 

// --- LCD DISPLAY STATE ---
// 0 = IP Address, 1 = Climate/Feed, 2 = Big Error Message
int displayState = 0; 

unsigned long pumpStartTime = 0; 
unsigned long bootSmsTimer = 0;
bool pendingSecondBootSMS = false;

float currentWaterDist = 0.0;
float currentSewerDist = 0.0;
float currentTemp = 0.0; 
float currentHum = 0.0;  
float currentFeedWeight = 0.0; 

// Timers
unsigned long previousWaterCheck = 0;
unsigned long previousSewerCheck = 1000; 
unsigned long previousDHTCheck = 2000; 
unsigned long previousScaleCheck = 3000;
unsigned long previousStatusReport = 0;
unsigned long previousLcdToggle = 0;
unsigned long previousRfidInit = 0; 
unsigned long previousSerialUpdate = 0; 

const long checkInterval = 2000; 
const long dhtInterval = 5000;         
const unsigned long statusInterval = 1800000; // 30 minutes

// --- SMART DELAY ---
void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    server.handleClient(); 
    delay(1);              
  }
}

// --- HELPER FUNCTIONS ---
String getLocalTimeStr() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return "Syncing...";
  }
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timeStringBuff);
}

void logDataToGoogleSheets(String lastTag) {
  if (WiFi.status() == WL_CONNECTED) {
    smartDelay(100); 
    { 
      WiFiClientSecure client;
      client.setInsecure(); 
      HTTPClient http;
      
      lastTag.replace(" ", "%20");
      String url = GOOGLE_SCRIPT_URL + "?temp=" + String(currentTemp, 1) + 
                   "&hum=" + String(currentHum, 1) + 
                   "&water=" + String(currentWaterDist, 1) + 
                   "&sewer=" + String(currentSewerDist, 1) + 
                   "&feed=" + String(currentFeedWeight, 1) +
                   "&tag=" + lastTag;
                   
      Serial.println("Pushing data to Google Sheets...");
      
      http.begin(client, url);
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      http.setTimeout(15000); 
      
      int httpCode = http.GET();
      if (httpCode == 200) {
        Serial.println("SUCCESS! Data logged to Sheet.");
      }
      
      http.end(); 
      client.stop(); 
    } 
  }
}

// --- SAFEGUARDED WEB CONTROL HANDLERS ---
void handleWaterOn() {
  if (pumpErrorActive) {
    server.sendHeader("Location", "/");
    server.send(303);
    return; 
  }
  
  if (currentWaterDist <= MIN_FULL_WATER) {
    lcd.clear();
    lcd.print("Water is Full!");
    smartDelay(2000);
    updateDashboard();
    server.sendHeader("Location", "/");
    server.send(303);
    return; 
  }

  digitalWrite(RELAY_PIN, HIGH); 
  isPumpRunning = true;
  pumpStartTime = millis(); 
  
  delay(100); 
  lcd.init();
  lcd.clear();
  lcd.print("Filling Tank...");
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWaterOff() {
  digitalWrite(RELAY_PIN, LOW); 
  isPumpRunning = false;
  
  delay(100); 
  lcd.init();
  lcd.clear();
  updateDashboard();
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleWaterReset() {
  pumpErrorActive = false; 
  displayState = 0; 
  
  delay(100); 
  lcd.init();
  lcd.clear();
  updateDashboard();
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleFeedCycle() {
  if (currentFeedWeight >= TARGET_FEED_WEIGHT) {
    lcd.clear();
    lcd.print("Feed is Full!");
    smartDelay(2000);
    updateDashboard();
    server.sendHeader("Location", "/");
    server.send(303);
    return; 
  }

  isFeederGateOpen = true;
  feederServo.write(SERVO_OPEN_ANGLE);  
  smartDelay(1500);      
  feederServo.write(SERVO_CLOSED_ANGLE); 
  isFeederGateOpen = false;
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleManualLog() {
  digitalWrite(SEWER_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(SEWER_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(SEWER_TRIG, LOW);
  long duration = pulseIn(SEWER_ECHO, HIGH, 30000); 
  
  if (duration > 0) {
    currentSewerDist = duration * 0.034 / 2;
    if (currentSewerDist > 0 && currentSewerDist <= MIN_FULL_SEWER) {
      sewerWarningActive = true;
    } else {
      sewerWarningActive = false;
    }
  }

  String logTag = "MANUAL_TRIGGER";
  if (sewerWarningActive) {
    logTag += "_[SEWER_FULL]"; 
  }

  logDataToGoogleSheets(logTag);
  server.sendHeader("Location", "/");
  server.send(303);
}

// --- WEB SERVER PAGE GENERATOR ---
void handleRoot() {
  String waterState = (currentWaterDist > MAX_EMPTY_WATER) ? "LOW" : "OK";
  String sewerState = (currentSewerDist <= MIN_FULL_SEWER && currentSewerDist > 0) ? "FULL!" : "OK";
  String gateState = isFeederGateOpen ? "OPEN (0&deg;)" : "CLOSED (60&deg;)";
  String currentTime = getLocalTimeStr();
  String currentIP = WiFi.localIP().toString();
  
  String waterButton;
  if (pumpErrorActive) {
    waterButton = "<a href=\"/water/reset\" class=\"btn btn-stop\">⚠️ Reset Pump Error</a>";
  } else if (isPumpRunning) {
    waterButton = "<a href=\"/water/off\" class=\"btn btn-stop\">🛑 Stop Water Pump</a>";
  } else {
    waterButton = "<a href=\"/water/on\" class=\"btn btn-water\">💧 Start Water Pump</a>";
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Zero Grazing Dashboard</title>
  <meta http-equiv="refresh" content="10"> 
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: #ecf0f1; color: #2c3e50; margin: 0; padding: 20px; }
    h1 { text-align: center; margin-bottom: 30px; color: #2980b9; }
    .dashboard-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 20px; max-width: 900px; margin: 0 auto; }
    .card { background-color: #ffffff; border-radius: 12px; padding: 25px; text-align: center; box-shadow: 0 4px 6px rgba(0,0,0,0.1); border-bottom: 5px solid #3498db; }
    .card h2 { margin-top: 0; font-size: 1.2rem; color: #7f8c8d; text-transform: uppercase; letter-spacing: 1px; }
    .value { font-size: 2.5rem; font-weight: bold; margin: 10px 0; color: #2c3e50; }
    .status-ok { color: #27ae60; }
    .status-alert { color: #e74c3c; }
    .system-info { grid-column: 1 / -1; border-color: #95a5a6; text-align: left; padding: 15px 25px; }
    .system-info p { font-size: 1.1rem; margin: 8px 0; color: #34495e; }
    .controls { grid-column: 1 / -1; border-color: #e74c3c; }
    .btn { display: inline-block; padding: 15px 25px; margin: 10px; border-radius: 8px; text-decoration: none; font-size: 1.1rem; font-weight: bold; transition: 0.2s; color: #fff;}
    .btn:active { transform: scale(0.95); }
    .btn-water { background-color: #3498db; }
    .btn-feed { background-color: #f39c12; }
    .btn-stop { background-color: #e74c3c; }
    .btn-log { background-color: #8e44ad; } 
  </style>
</head>
<body>
  <h1>🌾 Smart Zero Grazing Dashboard</h1>
  <div class="dashboard-grid">
    <div class="card" style="border-color: #e67e22;"><h2>Temperature</h2><div class="value">)rawliteral" + String(currentTemp, 1) + R"rawliteral(&deg;C</div></div>
    <div class="card" style="border-color: #3498db;"><h2>Humidity</h2><div class="value">)rawliteral" + String(currentHum, 1) + R"rawliteral(%</div></div>
    <div class="card" style="border-color: #9b59b6;"><h2>Clean Water</h2><div class="value )rawliteral" + String(waterState == "OK" ? "status-ok" : "status-alert") + R"rawliteral(">)rawliteral" + waterState + R"rawliteral(</div></div>
    <div class="card" style="border-color: #34495e;"><h2>Sewer Tank</h2><div class="value )rawliteral" + String(sewerState == "OK" ? "status-ok" : "status-alert") + R"rawliteral(">)rawliteral" + sewerState + R"rawliteral(</div></div>
    <div class="card" style="border-color: #27ae60;"><h2>Feed Trough</h2><div class="value">)rawliteral" + String(currentFeedWeight, 0) + R"rawliteral( g</div></div>
    <div class="card" style="border-color: #f1c40f;"><h2>Feed Gate</h2><div class="value">)rawliteral" + gateState + R"rawliteral(</div></div>
    <div class="card controls">
      <h2>Manual Controls</h2>
      )rawliteral" + waterButton + R"rawliteral(
      <a href="/feed" class="btn btn-feed">🌽 Cycle Feed Gate</a>
      <a href="/log" class="btn btn-log">📊 Log Data to Sheets</a>
    </div>
    <div class="card system-info">
      <h2>System Information</h2>
      <p><strong>🕒 Server Time:</strong> )rawliteral" + currentTime + R"rawliteral(</p>
      <p><strong>🌐 ESP32 IP Address:</strong> )rawliteral" + currentIP + R"rawliteral(</p>
      <p><strong>📱 Target GSM Number:</strong> )rawliteral" + phoneNumber + R"rawliteral(</p>
    </div>
  </div>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  Serial2.setTimeout(100); 

  Wire.begin(); 
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("System Booting..");

  SPI.begin();
  rfidWater.PCD_Init();
  dht.begin(); 
  
  ESP32PWM::allocateTimer(0);
  feederServo.setPeriodHertz(50);
  feederServo.attach(SERVO_PIN, 500, 2400);
  
  feederServo.write(SERVO_CLOSED_ANGLE); 
  
  pinMode(WATER_TRIG, OUTPUT);
  pinMode(WATER_ECHO, INPUT);
  pinMode(SEWER_TRIG, OUTPUT);
  pinMode(SEWER_ECHO, INPUT);
  
  digitalWrite(RELAY_PIN, LOW); 
  pinMode(RELAY_PIN, OUTPUT);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(CALIBRATION_FACTOR);
  lcd.setCursor(0, 1);
  lcd.print("Taring Scale...");
  delay(3000); 
  scale.tare();

  lcd.clear();
  lcd.print("Init Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  server.on("/", handleRoot);
  server.on("/water/on", handleWaterOn); 
  server.on("/water/off", handleWaterOff); 
  server.on("/water/reset", handleWaterReset); 
  server.on("/feed", handleFeedCycle);   
  server.on("/log", handleManualLog); 
  server.begin();

  lcd.clear();
  lcd.print("Init GSM...");
  
  delay(5000); 
  
  Serial2.println("ATE0"); 
  delay(1000);
  Serial2.println("AT+CMGF=1"); 
  delay(1000);
  Serial2.println("AT+CNMI=2,2,0,0,0"); 
  delay(1000);

  currentTemp = dht.readTemperature();
  currentHum = dht.readHumidity();
  
  logDataToGoogleSheets("SYSTEM_BOOT");
  sendSystemStatus(); 

  pendingSecondBootSMS = true;
  bootSmsTimer = millis();

  lcd.clear();
  updateDashboard();
}

void loop() {
  unsigned long currentMillis = millis();

  server.handleClient();

  if (pendingSecondBootSMS && (currentMillis - bootSmsTimer >= 60000)) {
    sendSystemStatus(); 
    pendingSecondBootSMS = false; 
  }

  if (currentMillis - previousScaleCheck >= 1000) {
    previousScaleCheck = currentMillis;
    if (scale.is_ready()) {
      currentFeedWeight = scale.get_units(3); 
    }
  }

  if (currentMillis - previousSerialUpdate >= 2000) {
    previousSerialUpdate = currentMillis;
    printSerialStatus(); 
  }

  if (currentMillis - previousRfidInit >= 3000) {
    previousRfidInit = currentMillis;
    rfidWater.PCD_Init(); 
  }

  if (rfidWater.PICC_IsNewCardPresent()) {
    if (rfidWater.PICC_ReadCardSerial()) {
      String tagUID = "";
      for (byte i = 0; i < rfidWater.uid.size; i++) {
        tagUID += String(rfidWater.uid.uidByte[i] < 0x10 ? " 0" : " ");
        tagUID += String(rfidWater.uid.uidByte[i], HEX);
      }
      tagUID.toUpperCase();
      tagUID.trim(); 

      Serial.print(">> RFID SCANNED TAG: ");
      Serial.println(tagUID);

      String animalName = "Unknown ID";
      if (tagUID == UID_ANIMAL_1) animalName = "Animal 1";
      else if (tagUID == UID_ANIMAL_2) animalName = "Animal 2";
      else if (tagUID == UID_ANIMAL_3) animalName = "Animal 3";
      else if (tagUID == UID_ANIMAL_4) animalName = "Animal 4";

      if (!sewerWarningActive) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(animalName);
        lcd.setCursor(0, 1);
        lcd.print("at water station"); 
        
        logDataToGoogleSheets(animalName + "_" + tagUID);
        smartDelay(1500); 
        updateDashboard();
      }
      rfidWater.PICC_HaltA();
      rfidWater.PCD_StopCrypto1();
    }
  }

  // SMS Commands Listener
  if (Serial2.available()) {
    String incomingMsg = Serial2.readString();
    incomingMsg.toUpperCase(); 
    
    if (incomingMsg.indexOf("+CMT:") != -1) {
      if (incomingMsg.indexOf("WATER") != -1) {
        if (pumpErrorActive) {
          handleWaterReset(); 
          sendSMS("System: Pump error cleared via SMS.");
        }
        else if (!isPumpRunning) {
          if (currentWaterDist <= MIN_FULL_WATER) {
            sendSMS("Command Rejected: Water tank is already full.");
          } else {
            sendSMS("Command Received: Pump ON.");
            handleWaterOn();
          }
        } else {
          sendSMS("Command Received: Pump OFF.");
          handleWaterOff();
        }
      }
      else if (incomingMsg.indexOf("FEED") != -1) {
        if (currentFeedWeight >= TARGET_FEED_WEIGHT) {
          sendSMS("Command Rejected: Feed trough is already full.");
        } else {
          sendSMS("Command Received: Cycling gate.");
          handleFeedCycle(); 
        }
      }
      else if (incomingMsg.indexOf("STATUS") != -1) {
        sendSystemStatus(); 
      }
    }
  }

  if (currentMillis - previousStatusReport >= statusInterval) {
    previousStatusReport = currentMillis;
    sendSystemStatus();
    logDataToGoogleSheets("ROUTINE_CHECK"); 
  }

  if (currentMillis - previousDHTCheck >= dhtInterval) {
    previousDHTCheck = currentMillis;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      currentTemp = t;
      currentHum = h;
    }
  }

  if (currentMillis - previousWaterCheck >= checkInterval) {
    previousWaterCheck = currentMillis;
    digitalWrite(WATER_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(WATER_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(WATER_TRIG, LOW);
    long duration = pulseIn(WATER_ECHO, HIGH, 30000); 
    
    if (duration > 0) {
      currentWaterDist = duration * 0.034 / 2;
      
      if (currentWaterDist > MAX_EMPTY_WATER && !isPumpRunning && !pumpErrorActive) {
        digitalWrite(RELAY_PIN, HIGH); 
        isPumpRunning = true;
        pumpStartTime = millis(); 
        
        delay(100); 
        lcd.init();
        lcd.clear();
        lcd.print("Filling Tank...");
      }
      else if (currentWaterDist <= MIN_FULL_WATER && isPumpRunning) {
        digitalWrite(RELAY_PIN, LOW); 
        isPumpRunning = false;
        
        delay(100);
        lcd.init();
        lcd.clear();
        updateDashboard();
      }
    }
  }

  // 🔴 DRY RUN PROTECTION (THE 3 SECOND TIMEOUT)
  if (isPumpRunning && (currentMillis - pumpStartTime >= MAX_PUMP_RUN_TIME)) {
    digitalWrite(RELAY_PIN, LOW); 
    isPumpRunning = false;
    pumpErrorActive = true;       
    
    displayState = 2;             
    lcd.clear();
    updateDashboard(); 
    
    sendSMS("ALERT: Water pump timed out! Source tank is likely empty.");
  }

  if (currentMillis - previousSewerCheck >= checkInterval) {
    previousSewerCheck = currentMillis;
    digitalWrite(SEWER_TRIG, LOW);
    delayMicroseconds(2);
    digitalWrite(SEWER_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(SEWER_TRIG, LOW);
    long duration = pulseIn(SEWER_ECHO, HIGH, 30000); 
    if (duration > 0) {
      currentSewerDist = duration * 0.034 / 2;
      
      if (currentSewerDist > 0 && currentSewerDist <= MIN_FULL_SEWER) {
        if(!sewerWarningActive) {
          sewerWarningActive = true;
          
          lcd.clear();
          updateDashboard(); 
          sendSMS("ALERT: Sewer tank is 4cm from overflowing!");
        }
      } else {
        if(sewerWarningActive) {
          sewerWarningActive = false;
          
          lcd.clear();
          updateDashboard(); 
        }
      }
    }
  }

  // Dashboard Toggle (2 SECONDS)
  if (currentMillis - previousLcdToggle >= 2000 && !sewerWarningActive && !isPumpRunning) {
    previousLcdToggle = currentMillis;
    
    displayState++; 
    
    if (displayState > (pumpErrorActive ? 2 : 1)) {
      displayState = 0; 
    }
    
    updateDashboard(); 
  }
}

// --- COMMUNICATION & TELEMETRY FUNCTIONS ---
void printSerialStatus() {
  Serial.println("\n--- 🌾 SYSTEM TELEMETRY ---");
  Serial.print("IP Address:  ");
  Serial.println(WiFi.localIP());
  
  Serial.print("Water Dist:  "); Serial.print(currentWaterDist, 1);
  if (pumpErrorActive) {
    Serial.println(" cm (ERROR: SOURCE DRY!)");
  } else {
    Serial.print(" cm ("); Serial.print(isPumpRunning ? "FILLING" : (currentWaterDist > MAX_EMPTY_WATER ? "LOW" : "OK")); Serial.println(")");
  }
  
  Serial.print("Sewer Dist:  "); Serial.print(currentSewerDist, 1);
  Serial.print(" cm ("); Serial.print(sewerWarningActive ? "WARNING: FULL" : "OK"); Serial.println(")");
  
  Serial.print("Feed Mass:   "); Serial.print(currentFeedWeight, 1);
  Serial.print(" g ("); Serial.print(isFeederGateOpen ? "GATE OPEN" : "GATE CLOSED"); Serial.println(")");
  
  Serial.print("Climate:     "); Serial.print(currentTemp, 1); Serial.print(" C, ");
  Serial.print(currentHum, 1); Serial.println(" %");
  Serial.println("---------------------------");
}

void sendSystemStatus() {
  String statusMsg = "Zero Grazing Status:\n";
  statusMsg += "Temp: " + String(currentTemp, 1) + "C\n";
  statusMsg += "Humidity: " + String(currentHum, 1) + "%\n";
  
  if (pumpErrorActive) {
    statusMsg += "Water: ERR (PUMP DRY)\n";
  } else {
    statusMsg += "Water: " + String(currentWaterDist > MAX_EMPTY_WATER ? "LOW\n" : "OK\n");
  }
  
  statusMsg += "Sewer: " + String((currentSewerDist <= MIN_FULL_SEWER && currentSewerDist > 0) ? "FULL!\n" : "OK\n");
  statusMsg += "Feed: " + String(currentFeedWeight, 0) + "g\n";
  sendSMS(statusMsg);
}

void updateDashboard() {
  if (sewerWarningActive) {
    lcd.setCursor(0, 0);
    lcd.print("WARNING: SEWER  ");
    lcd.setCursor(0, 1);
    lcd.print("TANK FULL!      ");
    return;
  }
  
  if (isPumpRunning) return; 

  if (pumpErrorActive && displayState == 2) {
    lcd.setCursor(0, 0);
    lcd.print("ERR: PUMP DRY!  ");
    lcd.setCursor(0, 1);
    lcd.print("Check Source Tnk");
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("Wtr:");
  
  if (pumpErrorActive) {
    lcd.print("ERR ");
  } else {
    lcd.print(currentWaterDist > MAX_EMPTY_WATER ? "LOW " : "OK  ");
  }
  
  lcd.print(" Swr:");
  lcd.print(currentSewerDist <= MIN_FULL_SEWER && currentSewerDist > 0 ? "FULL" : "OK  ");
  
  lcd.setCursor(0, 1);
  if (displayState == 0) {
    lcd.print("IP:");
    lcd.print(WiFi.localIP());
    lcd.print("                "); 
  } else if (displayState == 1) {
    lcd.print("T:");
    lcd.print(currentTemp, 0);
    lcd.print("C Fd:");
    lcd.print(currentFeedWeight, 0);
    lcd.print("g       "); 
  }
}

void sendSMS(String message) {
  Serial2.println("AT+CMGF=1"); 
  smartDelay(500); 
  Serial2.print("AT+CMGS=\"");
  Serial2.print(phoneNumber);
  Serial2.println("\"");
  smartDelay(500); 
  Serial2.print(message);
  smartDelay(100); 
  Serial2.write(26); 
  smartDelay(5000); 
}