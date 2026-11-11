#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>

// ===== Pin connections: match diagram.json =====
#define DHT_PIN 15
#define DHT_TYPE DHT22
#define MIST_PIN 25
#define PUMP_PIN 26
#define FAN_PIN 27

// Wokwi simulated Wi-Fi
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// Public demo MQTT broker (not for production use)
const char* MQTT_HOST = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;

// Must match the web dashboard DEVICE_ID and topics
const char* TOPIC_TELEMETRY = "gardenlab3-esp32/garden/telemetry";
const char* TOPIC_PUMP = "gardenlab3-esp32/garden/command/pump";
const char* TOPIC_FAN = "gardenlab3-esp32/garden/command/fan";

struct SensorData {
  float temperature;
  float humidity;
  uint32_t uptimeMs;
};

DHT dht(DHT_PIN, DHT_TYPE);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
QueueHandle_t dataQueue = nullptr;
QueueHandle_t alertQueue = nullptr;
volatile bool pumpState = false;
volatile bool fanState = false;

void setOutput(int pin, bool on) {
  digitalWrite(pin, on ? HIGH : LOW);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String command;
  for (unsigned int i = 0; i < length; i++) {
    command += (char)payload[i];
  }
  command.trim();
  command.toUpperCase();
  bool turnOn = (command == "ON");

  if (strcmp(topic, TOPIC_PUMP) == 0) {
    pumpState = turnOn;
    setOutput(PUMP_PIN, pumpState);
    Serial.printf("Pump command: %s\n", pumpState ? "ON" : "OFF");
  } else if (strcmp(topic, TOPIC_FAN) == 0) {
    fanState = turnOn;
    setOutput(FAN_PIN, fanState);
    Serial.printf("Fan command: %s\n", fanState ? "ON" : "OFF");
  }
}

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.printf("Connecting Wi-Fi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint32_t startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 15000) {
    vTaskDelay(pdMS_TO_TICKS(300));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi timeout; retrying later.");
  }
}

void connectMQTT() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) {
    return;
  }

  String clientId = "gardenlab3-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  Serial.printf("Connecting MQTT %s:%u ...\n", MQTT_HOST, MQTT_PORT);

  if (mqtt.connect(clientId.c_str())) {
    Serial.println("MQTT connected.");
    mqtt.subscribe(TOPIC_PUMP);
    mqtt.subscribe(TOPIC_FAN);
  } else {
    Serial.printf("MQTT connect failed. State: %d\n", mqtt.state());
  }
}

void TaskReadSensor(void* parameter) {
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    if (isnan(humidity) || isnan(temperature)) {
      Serial.println("DHT22 read failed.");
    } else {
      SensorData sample;
      sample.temperature = temperature;
      sample.humidity = humidity;
      sample.uptimeMs = millis();

      Serial.printf("Temperature: %.1f C | Humidity: %.1f %%\n",
                    temperature, humidity);

      if (xQueueSend(dataQueue, &sample, 0) != pdPASS) {
        Serial.println("Warning: dataQueue full; sample dropped.");
      }

      // Queue depth is one: retain the latest humidity for the alert task.
      xQueueOverwrite(alertQueue, &humidity);
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(2000));
  }
}

void TaskAlert(void* parameter) {
  float humidity = 0.0f;

  for (;;) {
    if (xQueueReceive(alertQueue, &humidity, portMAX_DELAY) == pdPASS) {
      int dutyPercent = 0;
      int runSeconds = 0;

      if (humidity < 30.0f) {
        dutyPercent = 100;
        runSeconds = 30;
      } else if (humidity < 35.0f) {
        dutyPercent = 50;
        runSeconds = 20;
      } else if (humidity < 40.0f) {
        dutyPercent = 25;
        runSeconds = 10;
      }

      if (dutyPercent == 0) {
        setOutput(MIST_PIN, false);
        Serial.printf("Mister OFF (humidity %.1f%%).\n", humidity);
        continue;
      }

      Serial.printf("Mister %d%% for %d seconds (humidity %.1f%%).\n",
                    dutyPercent, runSeconds, humidity);

      // Duty-cycle LED approximation for Wokwi. Real loads need a driver.
      uint32_t endAt = millis() + (uint32_t)runSeconds * 1000UL;
      while ((int32_t)(millis() - endAt) < 0) {
        setOutput(MIST_PIN, true);
        vTaskDelay(pdMS_TO_TICKS(dutyPercent * 10));
        setOutput(MIST_PIN, false);
        vTaskDelay(pdMS_TO_TICKS((100 - dutyPercent) * 10));
      }
      setOutput(MIST_PIN, false);
    }
  }
}

void TaskMqtt(void* parameter) {
  SensorData sample;

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connectWiFi();
    }
    if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
      connectMQTT();
    }

    if (mqtt.connected()) {
      mqtt.loop();

      if (xQueueReceive(dataQueue, &sample, pdMS_TO_TICKS(100)) == pdPASS) {
        char payload[160];
        snprintf(payload, sizeof(payload),
                 "{\"temperature\":%.1f,\"humidity\":%.1f,\"uptime_ms\":%lu,\"pump\":%s,\"fan\":%s}",
                 sample.temperature,
                 sample.humidity,
                 (unsigned long)sample.uptimeMs,
                 pumpState ? "true" : "false",
                 fanState ? "true" : "false");

        if (mqtt.publish(TOPIC_TELEMETRY, payload)) {
          Serial.printf("Published: %s\n", payload);
        } else {
          Serial.println("MQTT publish failed.");
        }
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(MIST_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  setOutput(MIST_PIN, false);
  setOutput(PUMP_PIN, false);
  setOutput(FAN_PIN, false);

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(256);

  dataQueue = xQueueCreate(8, sizeof(SensorData));
  alertQueue = xQueueCreate(1, sizeof(float));
  if (dataQueue == nullptr || alertQueue == nullptr) {
    Serial.println("ERROR: queue allocation failed.");
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }

  BaseType_t mqttTaskCreated = xTaskCreatePinnedToCore(
      TaskMqtt, "TaskMqtt", 6144, nullptr, 2, nullptr, 0);
  BaseType_t readTaskCreated = xTaskCreatePinnedToCore(
      TaskReadSensor, "TaskRead", 4096, nullptr, 2, nullptr, 1);
  BaseType_t alertTaskCreated = xTaskCreatePinnedToCore(
      TaskAlert, "TaskAlert", 4096, nullptr, 3, nullptr, 1);

  if (mqttTaskCreated != pdPASS || readTaskCreated != pdPASS ||
      alertTaskCreated != pdPASS) {
    Serial.println("ERROR: one or more FreeRTOS tasks failed to start.");
  }
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
