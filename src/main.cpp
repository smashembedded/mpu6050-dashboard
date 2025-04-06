#include <WiFi.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

const char* ssid = "";
const char* password = "";

Adafruit_MPU6050 mpu;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

float alpha = 0.2;
float filtAccelX = 0, filtAccelY = 0, filtAccelZ = 0;
float filtGyroX = 0, filtGyroY = 0, filtGyroZ = 0;

#define SAMPLE_COUNT 10
#define SAMPLE_COUNT 10
String sampleStrings[SAMPLE_COUNT];
int sampleIndex = 0;

void collectSensorData() {
    sensors_event_t accel, gyro, temp;
    mpu.getEvent(&accel, &gyro, &temp);

    // Apply low-pass filtering
    filtAccelX = alpha * accel.acceleration.x + (1 - alpha) * filtAccelX;
    filtAccelY = alpha * accel.acceleration.y + (1 - alpha) * filtAccelY;
    filtAccelZ = alpha * accel.acceleration.z + (1 - alpha) * filtAccelZ;
    filtGyroX = alpha * gyro.gyro.x + (1 - alpha) * filtGyroX;
    filtGyroY = alpha * gyro.gyro.y + (1 - alpha) * filtGyroY;
    filtGyroZ = alpha * gyro.gyro.z + (1 - alpha) * filtGyroZ;

    // Create and serialize the JSON doc
    DynamicJsonDocument doc(200);
    doc["accelX"] = filtAccelX;
    doc["accelY"] = filtAccelY;
    doc["accelZ"] = filtAccelZ;
    doc["gyroX"] = filtGyroX;
    doc["gyroY"] = filtGyroY;
    doc["gyroZ"] = filtGyroZ;
    doc["timestamp"] = millis();

    String json;
    serializeJson(doc, json);
    sampleStrings[sampleIndex++] = json;

    // When full, build array and send
    if (sampleIndex >= SAMPLE_COUNT) {
        if (ws.count() > 0) {
            String fullJson = "[";
            for (int i = 0; i < SAMPLE_COUNT; i++) {
                fullJson += sampleStrings[i];
                if (i < SAMPLE_COUNT - 1) fullJson += ",";
            }
            fullJson += "]";

            ws.textAll(fullJson);
            //Serial.println("Batch sent:");
            //Serial.println(fullJson);
        } else {
            Serial.println("No WebSocket clients connected.");
        }

        sampleIndex = 0;  // Reset for next batch
    }
}
void setup() {
    Serial.begin(115200);
    Wire.begin();

    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS Mount Failed!");
        return;
    }

    if (!SPIFFS.exists("/index.html")) {
        Serial.println("index.html NOT FOUND!");
    } else {
        Serial.println("index.html loaded successfully!");
    }

    if (!mpu.begin()) {
        Serial.println("Failed to find MPU6050 chip!");
        while (1);
    }
    Serial.println("MPU6050 Found!");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
    Serial.println(WiFi.localIP());

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/index.html", "text/html");
    });

    ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_CONNECT) {
            Serial.println("WebSocket Client Connected");
        }
    });

    server.addHandler(&ws);
    server.begin();

    xTaskCreatePinnedToCore([](void*) {
        while (true) {
            collectSensorData();
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }, "SensorTask", 4096, NULL, 1, NULL, 1);
}

void loop() {
    ws.cleanupClients();
}
