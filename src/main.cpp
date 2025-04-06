#include <WiFi.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>

#include "wifi_credentials.h"

// Configuration Constants
#define DEBUG 0
#define SAMPLE_COUNT 10
constexpr const char *ssid = WIFI_SSID;
constexpr const char *password = WIFI_PASSWORD;

// MPU6050 IMU
Adafruit_MPU6050 mpu;

// Low-pass filter parameters
float alpha = 0.2;
float filtAccelX = 0, filtAccelY = 0, filtAccelZ = 0;
float filtGyroX = 0, filtGyroY = 0, filtGyroZ = 0;

// Web Server
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
volatile bool clientConnected = false;

// Sample buffering
String sampleStrings[SAMPLE_COUNT];
int sampleIndex = 0;

// Read sensor data
void readSensorData()
{
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

    if (sampleIndex >= SAMPLE_COUNT)
    {
        if (ws.count() > 0)
        {
            String fullJson;
            fullJson.reserve(2048); // Pre-allocate enough memory for smoother append performance
            fullJson = "[";

            for (int i = 0; i < SAMPLE_COUNT; i++)
            {
                fullJson += sampleStrings[i];
                if (i < SAMPLE_COUNT - 1)
                    fullJson += ",";
            }

            fullJson += "]";

            ws.textAll(fullJson);

#if DEBUG
            Serial.println("Batch sent:");
            Serial.println(fullJson);
#endif
        }
        else
        {
            Serial.println("No WebSocket clients connected.");
        }

        sampleIndex = 0; // Reset for next batch
    }
}

void setup()
{
    Serial.begin(115200);
    Wire.begin();

    // Initalize IMU
    if (!mpu.begin())
    {
        Serial.println("Failed to find MPU6050 chip!");
        while (1)
            ;
    }
    Serial.println("MPU6050 Found!");

    // Mount SPIFFS
    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS Mount Failed!");
        return;
    }

    // Conect to WiFi
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to WiFi!");
    Serial.println(WiFi.localIP());

    // Serve index.html from SPIFFS
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
        if (SPIFFS.exists("/index.html")) {
            Serial.println("index.html loaded successfully!");
            request->send(SPIFFS, "/index.html", "text/html");
        } else {
            Serial.println("index.html NOT FOUND!");
            request->send(200, "text/html", "<h1>index.html not found!</h1>");
        } });

    // Handle WebSocket events
    ws.onEvent([](AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
               {
        if (type == WS_EVT_CONNECT) {
            Serial.println("WebSocket Client Connected");
            clientConnected = true;
        } else if (type == WS_EVT_DISCONNECT) {
            Serial.println("WebSocket Client Disconnected");
            clientConnected = false;
        } });

    // Start Server and WebSocket
    server.addHandler(&ws);
    server.begin();

    // Start sensor sampling task (runs on core 1)
    xTaskCreatePinnedToCore([](void *)
                            {
        while (true) {
            if (clientConnected) {
                readSensorData();
            }
            vTaskDelay(25 / portTICK_PERIOD_MS);
        } }, "SensorTask", 4096, NULL, 1, NULL, 1);
}

void loop()
{
    ws.cleanupClients(); // Handles disconnected clients
}
