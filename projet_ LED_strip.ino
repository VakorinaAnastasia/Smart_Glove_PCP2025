#include <WiFi.h>
#include <PubSubClient.h>
#include <FastLED.h>

// WiFi настройки
const char* ssid = "HONOR X8c";
const char* password = "09876543";

// MQTT настройки
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_color = "flexglove/finger_state";
const char* mqtt_topic_brightness = "flexglove/brightness";

WiFiClient espClient;
PubSubClient client(espClient);

// Настройки светодиодной ленты
#define LED_PIN 13
#define NUM_LEDS 80
CRGB leds[NUM_LEDS];

// Текущие настройки
String currentColor = "BLUE";
int currentBrightness = 64;
String lastColor = "";

// Таймеры
unsigned long lastColorChange = 0;
#define COLOR_CHANGE_DELAY 200
unsigned long lastSerialPrint = 0;
#define SERIAL_PRINT_INTERVAL 2000

String previousStableColor = "BLUE";
bool firstColorReceived = true;

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== ESP32 ПРИЕМНИК ===");
  Serial.println("Топики: flexglove/finger_state и flexglove/brightness");
  
  // Настройка светодиодной ленты
  FastLED.addLeds<WS2812B, LED_PIN, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(currentBrightness);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);
  
  // Начальный цвет (синий)
  Serial.println("Инициализация... Синий цвет");
  fill_solid(leds, NUM_LEDS, CRGB::Blue);
  FastLED.show();
  
  // Подключение к сети
  setupWiFi();
  
  // Настройка MQTT
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  client.setBufferSize(128);
  
  connectToMQTT();
  
  Serial.println("Ожидание команд цвета и яркости...");
}

void loop() {
  if (!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  smoothColorTransition();
  
  // Периодический вывод статуса
  if (millis() - lastSerialPrint >= SERIAL_PRINT_INTERVAL) {
    lastSerialPrint = millis();
    Serial.print("Текущие настройки: ");
    Serial.print(currentColor);
    Serial.print(", яркость ");
    Serial.print(currentBrightness);
    Serial.print("/255 (");
    Serial.print((currentBrightness * 100) / 255);
    Serial.println("%)");
  }
  
  delay(10);
}

// Функции MQTT

void connectToMQTT() {
  while (!client.connected()) {
    Serial.print("Подключение к MQTT...");
    
    String clientId = "ESP32-LED-Receiver-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("подключено!");
      
      if (client.subscribe(mqtt_topic_color) && client.subscribe(mqtt_topic_brightness)) {
        Serial.println("Подписан на оба топика:");
        Serial.print("  1. ");
        Serial.println(mqtt_topic_color);
        Serial.print("  2. ");
        Serial.println(mqtt_topic_brightness);
      } else {
        Serial.println("Ошибка подписки!");
      }
      
    } else {
      Serial.print("ошибка, rc=");
      Serial.print(client.state());
      Serial.println(" пробуем через 5 секунд...");
      delay(5000);
    }
  }
}

void reconnectMQTT() {
  connectToMQTT();
}

// Обработка входящих MQTT сообщений
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char message[20];
  memset(message, 0, sizeof(message));
  
  if (length < sizeof(message) - 1) {
    memcpy(message, payload, length);
    message[length] = '\0';
  }
  
  Serial.print("MQTT [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(message);
  
  // Определяем топик сообщения
  if (strcmp(topic, mqtt_topic_color) == 0) {
    processColorMessage(message);
  } else if (strcmp(topic, mqtt_topic_brightness) == 0) {
    processBrightnessMessage(message);
  } else {
    Serial.println("Неизвестный топик!");
  }
}

// Обработка сообщения о цвете
void processColorMessage(char* message) {
  String newColor = String(message);
  
  if (newColor == "BLUE" || newColor == "GREEN" || newColor == "RED") {
    
    if (firstColorReceived) {
      // Первое получение цвета
      lastColor = currentColor;
      currentColor = newColor;
      previousStableColor = newColor;
      lastColorChange = millis();
      firstColorReceived = false;
      
      Serial.print("Начальный цвет: ");
      Serial.println(currentColor);
      
    } else if (newColor != currentColor) {
      // Цвет изменился
      if (newColor == previousStableColor) {
        return; // Пропускаем возврат к предыдущему цвету
      }
      
      lastColor = currentColor;
      currentColor = newColor;
      lastColorChange = millis();
      
      Serial.print("Цвет изменен: ");
      Serial.println(currentColor);
      
      previousStableColor = newColor;
    }
  } else {
    Serial.print("Неизвестный цвет: ");
    Serial.println(message);
  }
}

// Обработка сообщения о яркости
void processBrightnessMessage(char* message) {
  int newBrightness = atoi(message);
  
  if (newBrightness >= 0 && newBrightness <= 255) {
    if (abs(newBrightness - currentBrightness) > 5) {
      currentBrightness = newBrightness;
      
      // Плавное изменение яркости
      int step = (newBrightness > currentBrightness) ? 1 : -1;
      for (int b = FastLED.getBrightness(); b != newBrightness; b += step) {
        FastLED.setBrightness(b);
        FastLED.show();
        delay(2);
      }
      
      FastLED.setBrightness(currentBrightness);
      FastLED.show();
      
      Serial.print("Яркость изменена: ");
      Serial.print(currentBrightness);
      Serial.print("/255 (");
      Serial.print((currentBrightness * 100) / 255);
      Serial.println("%)");
    }
  } else {
    Serial.print("Некорректная яркость: ");
    Serial.println(message);
  }
}

// Работа со светодиодной лентой

// Плавный переход между цветами
void smoothColorTransition() {
  if (lastColor != "" && millis() - lastColorChange < COLOR_CHANGE_DELAY) {
    float progress = (float)(millis() - lastColorChange) / COLOR_CHANGE_DELAY;
    
    if (progress >= 1.0f) {
      setLEDColor(currentColor);
      lastColor = "";
      return;
    }
    
    CRGB fromColor = colorFromString(lastColor);
    CRGB toColor = colorFromString(currentColor);
    
    // Смешиваем цвета
    CRGB blendedColor = blend(fromColor, toColor, (uint8_t)(progress * 255));
    
    fill_solid(leds, NUM_LEDS, blendedColor);
    FastLED.show();
    
  } else if (lastColor != "") {
    setLEDColor(currentColor);
    lastColor = "";
  }
}

// Установка цвета на ленте
void setLEDColor(String color) {
  CRGB newColor = colorFromString(color);
  
  // Плавное заполнение ленты
  for(int i = 0; i < NUM_LEDS; i++) {
    leds[i] = newColor;
    if (i % 5 == 0) {
      FastLED.show();
      delay(1);
    }
  }
  FastLED.show();
}

// Преобразование строки в цвет
CRGB colorFromString(String color) {
  if (color == "BLUE") return CRGB::Blue;
  if (color == "GREEN") return CRGB::Green;
  if (color == "RED") return CRGB::Red;
  return CRGB::Black;
}

// Подключение к WiFi
void setupWiFi() {
  Serial.print("Подключение к WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi подключен!");
    Serial.print("IP адрес: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nНе удалось подключиться к WiFi!");
  }
}
