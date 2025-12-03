// ESP32 передатчик - перчатка управления светом

#include <WiFi.h>
#include <PubSubClient.h>

// Настройки WiFi - заменить на свое
const char* ssid = "HONOR X8c";
const char* password = "09876543";

// Настройки MQTT брокера
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_color = "flexglove/finger_state";
const char* mqtt_topic_brightness = "flexglove/brightness";

WiFiClient espClient;
PubSubClient client(espClient);

// Датчики на перчатке
#define COLOR_SENSOR_PIN 32      // Указательный палец - цвет
#define BRIGHTNESS_SENSOR_PIN 35 // Средний палец - яркость
#define LOCK_SENSOR_PIN 33       // Большой палец - фиксация
#define LED_PIN 2                // Светодиод на ESP32

// Калибровочные значения для датчиков
int colorStraightValue = 0;     // Разогнутый палец
int colorBentValue = 0;         // Согнутый палец
int colorRangeBoundary1 = 0;    // Граница синий/зеленый (25%)
int colorRangeBoundary2 = 0;    // Граница зеленый/красный (70%)

int brightnessStraightValue = 0;
int brightnessBentValue = 0;
int brightnessRangeBoundary1 = 0; // Граница 15%/50% яркости
int brightnessRangeBoundary2 = 0; // Граница 50%/80% яркости

int lockStraightValue = 0;
int lockBentValue = 0;
int lockThreshold = 0;          // Порог для определения нажатия

// Состояния системы (конечный автомат)
enum SystemState {
  STATE_CALIBRATE_COLOR,      // Калибруем цвет
  STATE_CALIBRATE_BRIGHTNESS, // Калибруем яркость
  STATE_CALIBRATE_LOCK,       // Калибруем фиксацию
  STATE_SELECT_COLOR,         // Выбираем цвет
  STATE_LOCK_COLOR,           // Фиксируем цвет
  STATE_SELECT_BRIGHTNESS,    // Выбираем яркость
  STATE_LOCK_BRIGHTNESS,      // Фиксируем яркость
  STATE_OPERATIONAL           // Рабочий режим
};

SystemState currentState = STATE_CALIBRATE_COLOR;

// Финальные настройки после фиксации
String selectedColor = "BLUE";
int selectedBrightness = 128;

// Значения предпросмотра (меняются в реальном времени)
String previewColor = "BLUE";
int previewBrightness = 128;

// Флаги фиксации
bool colorLocked = false;
bool brightnessLocked = false;

// Таймер для фиксации
unsigned long lockStartTime = 0;
#define LOCK_HOLD_TIME 3000      // 3 секунды удержания

// Флаги для пропуска первого чтения
bool firstColorRead = true;
bool firstBrightnessRead = true;

// Функции индикации

// Мигаем светодиодом заданное число раз
void blinkLED(int times, int interval) {
  for(int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(interval);
    digitalWrite(LED_PIN, LOW);
    if(i < times - 1) delay(interval);
  }
}

// Показываем текущее состояние системы
void indicateState(SystemState state) {
  switch(state) {
    case STATE_CALIBRATE_COLOR:
      blinkLED(3, 200);
      Serial.println("КАЛИБРОВКА ЦВЕТА (указательный палец)");
      break;
      
    case STATE_CALIBRATE_BRIGHTNESS:
      blinkLED(2, 300);
      Serial.println("КАЛИБРОВКА ЯРКОСТИ (средний палец)");
      break;
      
    case STATE_CALIBRATE_LOCK:
      digitalWrite(LED_PIN, HIGH);
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      Serial.println("КАЛИБРОВКА ФИКСАЦИИ (большой палец)");
      break;
      
    case STATE_SELECT_COLOR:
      digitalWrite(LED_PIN, HIGH);
      Serial.println("ВЫБЕРИТЕ ЦВЕТ (указательный палец)");
      Serial.println("Разогнутый=СИНИЙ, Полусогнутый=ЗЕЛЕНЫЙ, Согнутый=КРАСНЫЙ");
      break;
      
    case STATE_LOCK_COLOR:
      Serial.println("ФИКСИРУЙТЕ ЦВЕТ (большой палец - 3 сек)");
      break;
      
    case STATE_SELECT_BRIGHTNESS:
      Serial.println("ВЫБЕРИТЕ ЯРКОСТЬ (средний палец)");
      Serial.println("Разогнутый=15%, Полусогнутый=50%, Согнутый=80%");
      break;
      
    case STATE_LOCK_BRIGHTNESS:
      Serial.println("ФИКСИРУЙТЕ ЯРКОСТЬ (большой палец - 3 сек)");
      break;
      
    case STATE_OPERATIONAL:
      digitalWrite(LED_PIN, HIGH);
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      delay(500);
      digitalWrite(LED_PIN, HIGH);
      delay(1000);
      digitalWrite(LED_PIN, LOW);
      Serial.println("СИСТЕМА ГОТОВА!");
      break;
  }
}

// Калибровка датчиков

// Калибруем один датчик
void calibrateSensor(int pin, const char* fingerName, const char* state1, const char* state2, 
                     int* straightValue, int* bentValue) {
  Serial.print("\n=== КАЛИБРОВКА ");
  Serial.print(fingerName);
  Serial.println(" ===");
  
  // Шаг 1: палец разогнут
  Serial.print("\n1. ");
  Serial.print(state1);
  Serial.println(" палец полностью");
  delay(3000);
  
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  
  // Делаем 30 измерений и усредняем
  long sum = 0;
  int measurements = 30;
  
  Serial.print("Измерение: ");
  for(int i = 0; i < measurements; i++) {
    sum += analogRead(pin);
    
    if(i % 5 == 0) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      Serial.print(".");
    }
    
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  Serial.println();
  
  *straightValue = sum / measurements;
  Serial.print(state1);
  Serial.print(": ");
  Serial.println(*straightValue);
  
  delay(1000);
  
  // Шаг 2: палец согнут
  Serial.print("\n2. ");
  Serial.print(state2);
  Serial.println(" палец полностью");
  delay(3000);
  
  for(int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(100);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  }
  
  sum = 0;
  Serial.print("Измерение: ");
  for(int i = 0; i < measurements; i++) {
    sum += analogRead(pin);
    
    if(i % 5 == 0) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      Serial.print(".");
    }
    
    delay(50);
  }
  digitalWrite(LED_PIN, LOW);
  Serial.println();
  
  *bentValue = sum / measurements;
  Serial.print(state2);
  Serial.print(": ");
  Serial.println(*bentValue);
  
  // Если значения перепутаны - меняем местами
  if(*straightValue > *bentValue) {
    int temp = *straightValue;
    *straightValue = *bentValue;
    *bentValue = temp;
    Serial.println("Значения исправлены!");
  }
  
  Serial.print("Диапазон: ");
  Serial.print(*straightValue);
  Serial.print(" - ");
  Serial.println(*bentValue);
  
  blinkLED(2, 200);
  delay(500);
}

// Калибруем все три датчика
void calibrateAllSensors() {
  // Калибруем цвет
  currentState = STATE_CALIBRATE_COLOR;
  indicateState(currentState);
  calibrateSensor(COLOR_SENSOR_PIN, "ЦВЕТА (указательный)", 
                  "Разогните", "Согните", 
                  &colorStraightValue, &colorBentValue);
  
  // Разбиваем диапазон на три части для трех цветов
  int colorRange = colorBentValue - colorStraightValue;
  colorRangeBoundary1 = colorStraightValue + (colorRange * 25 / 100);  // 25%
  colorRangeBoundary2 = colorStraightValue + (colorRange * 70 / 100);  // 70%
  
  Serial.print("Границы цвета: ");
  Serial.print(colorRangeBoundary1);
  Serial.print(" / ");
  Serial.println(colorRangeBoundary2);
  Serial.println("Разогнутый=СИНИЙ, Средний=ЗЕЛЕНЫЙ, Согнутый=КРАСНЫЙ");
  
  // Калибруем яркость
  currentState = STATE_CALIBRATE_BRIGHTNESS;
  indicateState(currentState);
  calibrateSensor(BRIGHTNESS_SENSOR_PIN, "ЯРКОСТИ (средний)", 
                  "Разогните", "Согните", 
                  &brightnessStraightValue, &brightnessBentValue);
  
  int brightnessRange = brightnessBentValue - brightnessStraightValue;
  brightnessRangeBoundary1 = brightnessStraightValue + (brightnessRange * 33 / 100);  // 33%
  brightnessRangeBoundary2 = brightnessStraightValue + (brightnessRange * 66 / 100);  // 66%
  
  Serial.print("Границы яркости: ");
  Serial.print(brightnessRangeBoundary1);
  Serial.print(" / ");
  Serial.println(brightnessRangeBoundary2);
  Serial.println("Разогнутый=15%, Средний=50%, Согнутый=80%");
  
  // Калибруем фиксацию
  currentState = STATE_CALIBRATE_LOCK;
  indicateState(currentState);
  calibrateSensor(LOCK_SENSOR_PIN, "ФИКСАЦИИ (большой)", 
                  "Разогните", "Согните", 
                  &lockStraightValue, &lockBentValue);
  
  lockThreshold = (lockStraightValue + lockBentValue) / 2;
  Serial.print("Порог фиксации: ");
  Serial.println(lockThreshold);
  
  Serial.println("\nКАЛИБРОВКА ЗАВЕРШЕНА!");
}

// Вспомогательные функции

// Проверяем нажат ли большой палец
bool isLockEngaged() {
  int lockValue = analogRead(LOCK_SENSOR_PIN);
  // Если ближе к согнутому значению - значит нажат
  return abs(lockValue - lockBentValue) < abs(lockValue - lockStraightValue);
}

// Преобразуем значение датчика в состояние цвета (0,1,2)
int getColorState(int value) {
  if(value <= colorRangeBoundary1) return 0;      // СИНИЙ
  else if(value <= colorRangeBoundary2) return 1; // ЗЕЛЕНЫЙ
  else return 2;                                  // КРАСНЫЙ
}

// Преобразуем номер состояния в строку
String colorStateToString(int state) {
  switch(state) {
    case 0: return "BLUE";    // Разогнутый
    case 1: return "GREEN";   // Полусогнутый
    case 2: return "RED";     // Согнутый
    default: return "UNKNOWN";
  }
}

// Преобразуем значение датчика в уровень яркости
int getBrightnessLevel(int value) {
  if(value <= brightnessRangeBoundary1) {
    return 38;  // 15% от 255
  } else if(value <= brightnessRangeBoundary2) {
    return 128; // 50% от 255
  } else {
    return 204; // 80% от 255
  }
}

// Настройка системы

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("=== ESP32 ПЕРЧАТКА - ТРЕХПАЛЬЦЕВАЯ СИСТЕМА ===");
  Serial.println("Цвет: Разогнутый=СИНИЙ, Полусогнутый=ЗЕЛЕНЫЙ, Согнутый=КРАСНЫЙ");
  Serial.println("Яркость: Разогнутый=15%, Полусогнутый=50%, Согнутый=80%");
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(COLOR_SENSOR_PIN, INPUT);
  pinMode(BRIGHTNESS_SENSOR_PIN, INPUT);
  pinMode(LOCK_SENSOR_PIN, INPUT);
  
  blinkLED(5, 100);
  calibrateAllSensors();
  
  setupWiFi();
  client.setServer(mqtt_server, mqtt_port);
  client.setBufferSize(128);
  connectToMQTT();
  
  currentState = STATE_SELECT_COLOR;
  indicateState(currentState);
  
  Serial.println("\nИНСТРУКЦИЯ:");
  Serial.println("1. Указательный палец - выбирает ЦВЕТ");
  Serial.println("2. Большой палец - фиксирует цвет (3 сек)");
  Serial.println("3. Средний палец - выбирает ЯРКОСТЬ");
  Serial.println("4. Большой палец - фиксирует яркость (3 сек)");
  Serial.println("5. Система готова!");
  
  firstColorRead = true;
  firstBrightnessRead = true;
}

// Главный цикл

void loop() {
  // Проверяем и поддерживаем соединение
  if(!client.connected()) {
    reconnectMQTT();
  }
  client.loop();
  
  // Читаем датчики
  int colorValue = analogRead(COLOR_SENSOR_PIN);
  int brightnessValue = analogRead(BRIGHTNESS_SENSOR_PIN);
  bool lockEngaged = isLockEngaged();
  
  // Обрабатываем текущее состояние
  switch(currentState) {
    case STATE_SELECT_COLOR:
      handleColorSelection(colorValue);
      if(lockEngaged && !colorLocked) {
        handleLockStart("цвет");
      }
      break;
      
    case STATE_LOCK_COLOR:
      handleLocking(lockEngaged, "цвета");
      break;
      
    case STATE_SELECT_BRIGHTNESS:
      handleBrightnessSelection(brightnessValue);
      if(lockEngaged && !brightnessLocked) {
        handleLockStart("яркость");
      }
      break;
      
    case STATE_LOCK_BRIGHTNESS:
      handleLocking(lockEngaged, "яркости");
      break;
      
    case STATE_OPERATIONAL:
      handleOperationalMode();
      break;
  }
  
  updateLED();
  delay(50);
}

// Обработчики состояний

// Выбор цвета (предпросмотр)
void handleColorSelection(int colorValue) {
  static int lastColorState = -1;
  static unsigned long lastSendTime = 0;
  static int stabilityCounter = 0;
  
  int currentColorState = getColorState(colorValue);
  unsigned long now = millis();
  
  // Пропускаем первое чтение
  if (firstColorRead) {
    firstColorRead = false;
    lastColorState = currentColorState;
    return;
  }
  
  // Проверяем стабильность (3 одинаковых значения подряд)
  if (currentColorState == lastColorState) {
    stabilityCounter++;
  } else {
    stabilityCounter = 0;
    lastColorState = currentColorState;
  }
  
  // Если стабильно и прошло больше 500мс с последней отправки
  if (stabilityCounter >= 3 && (now - lastSendTime) > 500) {
    String newPreviewColor = colorStateToString(currentColorState);
    
    // Если цвет изменился
    if (newPreviewColor != previewColor) {
      previewColor = newPreviewColor;
      lastSendTime = now;
      stabilityCounter = 0;
      
      // Отправляем для предпросмотра
      client.publish(mqtt_topic_color, previewColor.c_str());
      
      Serial.print("Предпросмотр цвета: ");
      Serial.println(previewColor);
      
      // Короткое мигание для подтверждения
      digitalWrite(LED_PIN, LOW);
      delay(20);
      digitalWrite(LED_PIN, HIGH);
    }
  }
}

// Выбор яркости (предпросмотр)
void handleBrightnessSelection(int brightnessValue) {
  if(brightnessLocked) return;
  
  static int lastBrightnessLevel = -1;
  static unsigned long lastSendTime = 0;
  static int stabilityCounter = 0;
  
  int currentBrightness = getBrightnessLevel(brightnessValue);
  unsigned long now = millis();
  
  // Пропускаем первое чтение
  if (firstBrightnessRead) {
    firstBrightnessRead = false;
    lastBrightnessLevel = currentBrightness;
    return;
  }
  
  // Проверяем стабильность (изменение не больше 5)
  if (abs(currentBrightness - lastBrightnessLevel) <= 5) {
    stabilityCounter++;
  } else {
    stabilityCounter = 0;
    lastBrightnessLevel = currentBrightness;
  }
  
  // Если стабильно и прошло больше 500мс
  if (stabilityCounter >= 3 && (now - lastSendTime) > 500) {
    if (abs(currentBrightness - previewBrightness) > 5) {
      previewBrightness = currentBrightness;
      lastSendTime = now;
      stabilityCounter = 0;
      
      // Отправляем для предпросмотра
      char brightnessStr[4];
      sprintf(brightnessStr, "%03d", previewBrightness);
      client.publish(mqtt_topic_brightness, brightnessStr);
      
      Serial.print("Предпросмотр яркости: ");
      Serial.print(previewBrightness);
      Serial.print("/255 (");
      Serial.print((previewBrightness * 100) / 255);
      Serial.println("%)");
      
      // Двойное мигание для подтверждения
      for(int i = 0; i < 2; i++) {
        digitalWrite(LED_PIN, LOW);
        delay(15);
        digitalWrite(LED_PIN, HIGH);
        delay(15);
      }
    }
  }
}

// Начинаем фиксацию
void handleLockStart(const char* item) {
  Serial.print("\nФИКСАЦИЯ ");
  Serial.print(item);
  Serial.println(" НАЧАТА... Удерживайте 3 секунды");
  
  if(strcmp(item, "цвет") == 0) {
    currentState = STATE_LOCK_COLOR;
    selectedColor = previewColor;  // Запоминаем выбранный цвет
    Serial.print("Выбран цвет: ");
    Serial.println(selectedColor);
  } else {
    currentState = STATE_LOCK_BRIGHTNESS;
    selectedBrightness = previewBrightness;  // Запоминаем выбранную яркость
    Serial.print("Выбрана яркость: ");
    Serial.println(selectedBrightness);
  }
  
  lockStartTime = millis();  // Засекаем время
}

// Процесс фиксации (удержание)
void handleLocking(bool lockEngaged, const char* item) {
  unsigned long now = millis();
  
  // Если отпустили до истечения 3 секунд
  if(!lockEngaged) {
    Serial.print("Фиксация ");
    Serial.print(item);
    Serial.println(" отменена");
    
    // Возвращаемся к выбору
    if(strcmp(item, "цвета") == 0) {
      currentState = STATE_SELECT_COLOR;
    } else {
      currentState = STATE_SELECT_BRIGHTNESS;
    }
    return;
  }
  
  // Проверяем прошло ли 3 секунды
  if(now - lockStartTime >= LOCK_HOLD_TIME) {
    Serial.print("✅ ");
    Serial.print(item);
    Serial.println(" ЗАФИКСИРОВАН!");
    
    if(strcmp(item, "цвета") == 0) {
      colorLocked = true;
      // Отправляем финальный цвет
      client.publish(mqtt_topic_color, selectedColor.c_str());
      currentState = STATE_SELECT_BRIGHTNESS;
      indicateState(currentState);
      Serial.println("\nТеперь выберите яркость СРЕДНИМ пальцем");
      firstBrightnessRead = true;
      
    } else {
      brightnessLocked = true;
      // Отправляем финальную яркость
      char brightnessStr[4];
      sprintf(brightnessStr, "%03d", selectedBrightness);
      client.publish(mqtt_topic_brightness, brightnessStr);
      currentState = STATE_OPERATIONAL;
      indicateState(currentState);
      
      Serial.println("\nСИСТЕМА ГОТОВА!");
      Serial.print("Настройки: ");
      Serial.print(selectedColor);
      Serial.print(", яркость ");
      Serial.print(selectedBrightness);
      Serial.print("/255");
    }
    
    blinkLED(3, 100);
  } else {
    // Показываем прогресс
    int progress = ((now - lockStartTime) * 100) / LOCK_HOLD_TIME;
    static int lastProgress = -1;
    if(progress / 25 != lastProgress / 25) {
      lastProgress = progress;
      Serial.print("Прогресс фиксации: ");
      Serial.print(progress);
      Serial.println("%");
    }
  }
}

// Рабочий режим (после фиксации всего)
void handleOperationalMode() {
  static unsigned long lastSend = 0;
  // Раз в 10 секунд отправляем настройки для надежности
  if(millis() - lastSend > 10000) {
    lastSend = millis();
    
    client.publish(mqtt_topic_color, selectedColor.c_str());
    
    char brightnessStr[4];
    sprintf(brightnessStr, "%03d", selectedBrightness);
    client.publish(mqtt_topic_brightness, brightnessStr);
    
    Serial.print("Обновление: ");
    Serial.print(selectedColor);
    Serial.print(", яркость ");
    Serial.println(selectedBrightness);
  }
}

// Обновляем светодиодную индикацию
void updateLED() {
  unsigned long now = millis();
  
  switch(currentState) {
    case STATE_SELECT_COLOR:
      digitalWrite(LED_PIN, HIGH);  // Постоянно горит
      break;
      
    case STATE_LOCK_COLOR:
      // Медленно мигает
      if((now / 500) % 2 == 0) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
      break;
      
    case STATE_SELECT_BRIGHTNESS:
      // Очень медленно мигает
      if((now / 1000) % 2 == 0) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
      break;
      
    case STATE_LOCK_BRIGHTNESS:
      // Медленно мигает
      if((now / 500) % 2 == 0) {
        digitalWrite(LED_PIN, HIGH);
      } else {
        digitalWrite(LED_PIN, LOW);
      }
      break;
      
    case STATE_OPERATIONAL:
      // Плавное "дыхание"
      int brightness = (sin(now / 1000.0 * PI) + 1) * 127;
      analogWrite(LED_PIN, brightness);
      break;
  }
}

// Функции WiFi и MQTT

void setupWiFi() {
  Serial.print("Подключение к WiFi...");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while(WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    attempts++;
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi подключен!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    blinkLED(3, 200);
  } else {
    Serial.println("\nОшибка WiFi!");
  }
}

void connectToMQTT() {
  while(!client.connected()) {
    Serial.print("Подключение к MQTT...");
    
    String clientId = "ESP32-ThreeFinger-Fixed-";
    clientId += String(random(0xffff), HEX);  // Уникальный ID
    
    if(client.connect(clientId.c_str())) {
      Serial.println("MQTT подключен!");
      blinkLED(2, 100);
    } else {
      Serial.print("Ошибка, rc=");
      Serial.print(client.state());
      Serial.println(", пробуем через 5 секунд...");
      
      for(int i = 0; i < 5; i++) {
        digitalWrite(LED_PIN, HIGH);
        delay(100);
        digitalWrite(LED_PIN, LOW);
        delay(100);
      }
      delay(5000);
    }
  }
}

void reconnectMQTT() {
  connectToMQTT();
}
