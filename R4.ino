#include <usbhub.h>
#include <usbhid.h>
#include <hidboot.h>
#include "WiFiS3.h"
#include <ArduinoHttpClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

// --- ตั้งค่า WiFi & Firebase ---
char ssid[] = "🌾"; 
char pass[] = "pitchapa-duangsut";
const char* serverAddress = "firestore.googleapis.com";
const char* googleAddress = "script.google.com";
const char* projectId = "cabinet-medicine";

// --- ตั้งค่า Google Sheets (Deployment ID) ---
const String googleDeployId = "AKfycbyHXXV9KvxoRPWaKGoKNisB96t9daqJhe0uqV4-n6g1mqpXi9iOt6pM_RERE505hx8D"; // **แก้ตรงนี้**

// --- ตั้งค่า USB Host & Parser ---
USB Usb;
HIDBoot<USB_HID_PROTOCOL_KEYBOARD> HidKeyboard(&Usb);
String scannedData = "";
unsigned long lastScanTime = 0;

void checkMedicine(String barcodeId);
void openRelay(int slot);
void sendToGoogle(String barcode, int slot);

class KbdRptParser : public KeyboardReportParser {
    void OnKeyDown(uint8_t mod, uint8_t key);
};

void KbdRptParser::OnKeyDown(uint8_t mod, uint8_t key) {
    uint8_t c = OemToAscii(mod, key);
    if (c) {
        lastScanTime = millis();
        if (c == 13 || c == 10) {
            if (scannedData.length() > 0) {
                scannedData.trim();
                Serial.println("\n[Scanner] ID: " + scannedData);
                checkMedicine(scannedData);
                scannedData = "";
            }
        } else {
            scannedData += (char)c;
            Serial.print((char)c);
        }
    }
}

KbdRptParser Prs;
WiFiSSLClient wifi;
HttpClient client = HttpClient(wifi, serverAddress, 443);
HttpClient googleClient = HttpClient(wifi, googleAddress, 443);

void setup() {
    Serial.begin(115200);
    Wire.begin();
    
    // เริ่ม USB Host Shield
    Serial.println("Initializing USB Host...");
    if (Usb.Init() == -1) Serial.println("USB Host Shield did not start.");
    HidKeyboard.SetReportParser(0, &Prs);

    // เชื่อมต่อ WiFi
    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) { 
        delay(500); 
        Serial.print("."); 
    }
    Serial.println("\nWiFi Connected!");
    
    // ปิด Relay ทั้งหมด (Active Low)
    Wire.beginTransmission(0x20);
    Wire.write(0xFF);
    Wire.endTransmission();
}

void loop() {
    Usb.Task();

    // Timeout กรณีเครื่องแสกนไม่ส่ง Enter
    if (scannedData.length() > 0 && (millis() - lastScanTime > 500)) {
        scannedData.trim();
        Serial.println("\n[Timeout] ID: " + scannedData);
        checkMedicine(scannedData);
        scannedData = "";
    }
}

void checkMedicine(String barcodeId) {
    String path = "/v1/projects/" + String(projectId) + "/databases/(default)/documents/inventory/" + barcodeId;
    Serial.println("--- Requesting Firestore ---");
    
    client.get(path);
    int statusCode = client.responseStatusCode();
    String response = client.responseBody();

    if (statusCode == 200) {
        StaticJsonDocument<1024> doc;
        deserializeJson(doc, response);
        
        if (doc.containsKey("fields")) {
            int slot = doc["fields"]["slot_number"]["integerValue"].as<int>();
            Serial.print("Medicine Found! Opening Slot: ");
            Serial.println(slot);
            
            openRelay(slot);
            sendToGoogle(barcodeId, slot); // ส่งไป Google Sheets
        }
    } else {
        Serial.println("Error: " + String(statusCode) + " (Not Found)");
    }
}

void openRelay(int slot) {
    Wire.beginTransmission(0x20);
    Wire.write(~(1 << slot)); 
    Wire.endTransmission();
    
    delay(3000); // เปิด 3 วินาที
    
    Wire.beginTransmission(0x20);
    Wire.write(0xFF); 
    Wire.endTransmission();
    Serial.println("Slot Closed.");
}

void sendToGoogle(String barcode, int slot) {
    Serial.println("--- Connecting to Google Sheets ---");
    
    // เคลียร์การเชื่อมต่อเก่า
    wifi.stop(); 
    
    if (wifi.connect(googleAddress, 443)) {
        Serial.println("Connected to Google Server!");
        
        // สร้าง HTTP Request แบบแมนนวล
        String url = "/macros/s/" + googleDeployId + "/exec?barcode=" + barcode + "&slot=" + String(slot);
        
        wifi.print(String("GET ") + url + " HTTP/1.1\r\n" +
                   "Host: " + googleAddress + "\r\n" +
                   "User-Agent: ArduinoWiFi/1.1\r\n" +
                   "Connection: close\r\n\r\n");

        Serial.println("Data Sent!");
        
        // อ่านการตอบกลับเล็กน้อย (เพื่อให้ Google รู้ว่าเราได้รับแล้ว)
        unsigned long timeout = millis();
        while (wifi.available() == 0) {
            if (millis() - timeout > 5000) {
                Serial.println(">>> Client Timeout !");
                wifi.stop();
                return;
            }
        }
        
        Serial.println("Backup Success!");
        wifi.stop(); // ปิดการเชื่อมต่อ
    } else {
        Serial.println("Connection Failed to Google.");
    }
}