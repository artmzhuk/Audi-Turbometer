#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <string.h>


const int  MAX_VAL_PERIOD_SEC = 30;
const float BAR_MIN = 0.8;
const float BAR_MAX = 3.3;
const int EXHAUST_BUTTON_THRESHOLD = 300; // ms
const int EXHAUST_ON_PIN = 3; //D3
const int EXHAUST_OFF_PIN = 7; //D7


// Структура для кольцевой очереди с поиском максимума
struct CircularQueue {
    float* values;      // Массив значений
    int* maxDeque;      // Дек индексов для поиска максимума (кольцевой)
    int capacity;       // Размер буфера
    int qHead;          // Голова очереди значений
    int qTail;          // Хвост очереди значений
    int qSize;          // Текущий размер очереди
    int dHead;          // Голова дека максимумов
    int dTail;          // Хвост дека максимумов
};

struct CircularQueue *queue = NULL;
int qError = 0;

float maxPressInSec = 0;

// Прототипы функций
void initCircularQueue(struct CircularQueue **q, int capacity);
void freeCircularQueue(struct CircularQueue *q);
int isQueueEmpty(struct CircularQueue *q);
int isQueueFull(struct CircularQueue *q);
void enqueue(struct CircularQueue *q, float value);
float dequeue(struct CircularQueue *q);
float getMaximum(struct CircularQueue *q);
int getQueueSize(struct CircularQueue *q);

LiquidCrystal_I2C lcd(0x27 , 16, 2);

const int SENSOR_MAP_SIZE = 10; 
const float SENSOR_MAP[SENSOR_MAP_SIZE][2] = {
  {25, -1},
  {45, -0.8},
  {202, 0},
  {302, 0.5},
  {399, 1.0},
  {457, 1.3},
  {516, 1.6},
  {554, 1.8},
  {595, 2.0},
  {791, 3.0}
};

void initCircularQueue(struct CircularQueue **q, int capacity) {
  Serial.print("Allocating memory for circular queue, capacity: ");
  Serial.println(capacity);
  
  *q = (struct CircularQueue*)malloc(sizeof(struct CircularQueue));
  if(*q == NULL) {
    Serial.println("ERROR: malloc queue failed");
    qError = 1;
    return;
  }
  
  (*q)->values = (float*)calloc(capacity, sizeof(float));
  if((*q)->values == NULL) {
    Serial.println("ERROR: calloc values failed");
    free(*q);
    *q = NULL;
    qError = 1;
    return;
  }
  
  (*q)->maxDeque = (int*)calloc(capacity, sizeof(int));
  if((*q)->maxDeque == NULL) {
    Serial.println("ERROR: calloc maxDeque failed");
    free((*q)->values);
    free(*q);
    *q = NULL;
    qError = 1;
    return;
  }
  
  (*q)->capacity = capacity;
  (*q)->qHead = 0;
  (*q)->qTail = 0;
  (*q)->qSize = 0;
  (*q)->dHead = 0;
  (*q)->dTail = 0;
  
  Serial.println("Circular queue initialization complete");
}

void freeCircularQueue(struct CircularQueue *q) {
  if(q != NULL) {
    if(q->values != NULL) free(q->values);
    if(q->maxDeque != NULL) free(q->maxDeque);
    free(q);
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000); // Задержка для инициализации Serial порта
  Serial.println("=== Audi Turbometer Starting ===");
  
  lcd.init();
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");
  
  initBar2();
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(EXHAUST_ON_PIN, INPUT_PULLUP);
  pinMode(EXHAUST_OFF_PIN, INPUT_PULLUP);
  
  int queueCapacity = MAX_VAL_PERIOD_SEC + 5;
  initCircularQueue(&queue, queueCapacity);
  
  if(qError == 0){
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Ready!");
    delay(500);
    lcd.clear();
    Serial.println("=== Setup Complete ===");
  } else {
    lcd.setCursor(0, 1);
    lcd.print("Init Error!");
  }
}

void loop() {
  static unsigned long displayTimer;
  static unsigned long queueTimer;
  static bool firstRun = true;

  static bool currentExhaustShowState = false;
  static bool exhaustOnIsPressed = false;
  static bool exhaustOffIsPressed = false;
  static byte lastOffReading = 0;
  static byte lastOnReading = 0;
  static unsigned long lastOffDebounceTime;
  static unsigned long lastOnDebounceTime;
  static unsigned long lastExhaustStateChangeTime;
  static char exhaustString[] = "     ";

  int readFromAnalog = analogRead(A0);
  float currentPressure = getPressureFromAnalog(readFromAnalog);
  
  if (millis() - queueTimer > 1000){
    // сюда заходим раз в секунду
    if(queue != NULL){
      if(firstRun){
        Serial.println("First enqueue operation");
        firstRun = false;
      }
      
      enqueue(queue, maxPressInSec);
      Serial.print("ENQ: val=");
      Serial.print(maxPressInSec, 2);
      Serial.print(" | qSize=");
      Serial.print(getQueueSize(queue));
      
      if (getQueueSize(queue) > MAX_VAL_PERIOD_SEC){
         float dequeued = dequeue(queue);
         Serial.print(" | DEQ=");
         Serial.print(dequeued, 2);
      }
      
      float maxVal = getMaximum(queue);
      Serial.print(" | MAX=");
      Serial.println(maxVal, 2);
      
      updateMax(maxVal);
    } else {
      Serial.println("ERROR: queue is NULL in loop");
    }
    maxPressInSec = 0;
    queueTimer = millis();
  }
  
  if (currentPressure > maxPressInSec){
    maxPressInSec = currentPressure;
  }
  
  if (millis() - displayTimer > 50){
    if (millis() - lastExhaustStateChangeTime < 10 * 1000 && currentExhaustShowState == true){
      strcpy(exhaustString, "SPORT");
    } else if (millis() - lastExhaustStateChangeTime < 3 * 1000 && currentExhaustShowState == false){
      strcpy(exhaustString, "  OFF");
    } else {
      strcpy(exhaustString, "     ");
    }
    updateDisplay(readFromAnalog, currentExhaustShowState, exhaustString);
    displayTimer = millis();
  }

  // ======= debounce logic 

  byte currentOffReading = digitalRead(EXHAUST_OFF_PIN);
  if (currentOffReading == 1){
    exhaustOffIsPressed = false;
  }
  if (currentOffReading != lastOffReading) {
    lastOffDebounceTime = millis();
  }

  if (millis() - lastOffDebounceTime > EXHAUST_BUTTON_THRESHOLD) {
    if (!exhaustOffIsPressed && currentOffReading == 0){
      currentExhaustShowState = false;
      lastExhaustStateChangeTime = millis();
      exhaustOffIsPressed = true;
    }
  }
  lastOffReading = currentOffReading;



  byte currentOnReading = digitalRead(EXHAUST_ON_PIN);
  if (currentOnReading == 1){
    exhaustOnIsPressed = false;
  }
  if (currentOnReading != lastOnReading) {
    lastOnDebounceTime = millis();
  }

  if (millis() - lastOnDebounceTime > EXHAUST_BUTTON_THRESHOLD) {
    if (!exhaustOnIsPressed && currentOnReading == 0){
      currentExhaustShowState = true;
      lastExhaustStateChangeTime = millis();
      exhaustOnIsPressed = true;
    }
  }
  lastOnReading = currentOnReading;
} 


void updateDisplay(int rawAnalog, bool currentExhaustShowState, char* exhaustString){
  float volts = convertToVolts(rawAnalog);
  int currentPercent;
  if (rawAnalog - 202.0 >= 0){
    currentPercent = (int) ((((float)rawAnalog - 202.0) / 197.0) * 100); // bar limit is 1 bar, 0 percent is atmo, 255 percent is negative pressure
  } else {
    currentPercent = 255;
  }
  float currentPressure = getPressureFromAnalog(rawAnalog);
  
  // Строка 0: символы + текущее давление
  lcd.setCursor(0, 0);
  lcd.write(4);  // left
  lcd.write(5);  // right
  lcd.print(" ");
  
  // Форматируем текущее давление (фиксированная ширина)
  if (currentPressure >= 0.0){
    lcd.print(" "); // пробел для выравнивания
  }
  lcd.print(currentPressure, 2); // 2 знака после запятой  
  fillBar2(8, 0, 8, currentPercent);
  lcd.setCursor(9, 1);
  lcd.print(exhaustString);

  lcd.setCursor(14, 1);
  if (currentExhaustShowState == true) {
    lcd.write(6);  // left
    lcd.write(7); 
  } else {
    lcd.print("  ");
  }
}

void updateMax(float maxValue){
  // Выводим MAX на второй строке
  lcd.setCursor(0, 1);
  if (qError != 0){
    lcd.print("Error: E");
    lcd.print(qError);
    //lcd.print("       "); // очистка остатков
  } else {
    lcd.print("MAX:");
    if (maxValue < 0.0){
      maxValue = 0.0;
    }
    lcd.print(maxValue, 2); // 2 знака после запятой
    //lcd.print("       "); // очистка остатков
  }
}

float convertToVolts(int x){
  float volt = (float)(x * 5.0) / 1024;
  return volt;
}

float getPressureFromAnalog(int x){
  if (x >= SENSOR_MAP[SENSOR_MAP_SIZE - 1][0]){
    return SENSOR_MAP[SENSOR_MAP_SIZE - 1][1];
  }
  if (x <= SENSOR_MAP[0][0]){
    return SENSOR_MAP[0][1];
  }
  
  int target = 0;
  for (int i = 1; i < SENSOR_MAP_SIZE; i++){
    if (SENSOR_MAP[i][0] > x){
      target = i;
      break;
    }
  }
  float linCoef = (x - SENSOR_MAP[target - 1][0]) / (SENSOR_MAP[target][0] - SENSOR_MAP[target - 1][0]);
  float res = ((SENSOR_MAP[target][1] - SENSOR_MAP[target - 1][1]) * linCoef) + SENSOR_MAP[target - 1][1];
  return res;
}

void initBar2() {
  // необходимые символы для работы
  // создано в http://maxpromer.github.io/LCD-Character-Creator/
  byte right_empty[8] = {0b11111,  0b00001,  0b00001,  0b00001,  0b00001,  0b00001,  0b00001,  0b11111};
  byte center_empty[8] = {0b11111, 0b00000,  0b00000,  0b00000,  0b00000,  0b00000,  0b00000,  0b11111};
  byte left_full[8] = {0b11111, 0b10000, 0b10111, 0b10111, 0b10111, 0b10111, 0b10000, 0b11111};
  byte center_full[8] = {0b11111, 0b00000, 0b11111, 0b11111, 0b11111, 0b11111, 0b00000, 0b11111};
  byte left[] = {0x00, 0x03, 0x04, 0x05, 0x05, 0x04, 0x03, 0x00};
  byte right[] = { 0x01,0x1F, 0x05, 0x14, 0x14, 0x04, 0x18, 0x00};
  byte trombon_left[] = { 0x01, 0x03, 0x07, 0x06, 0x06, 0x00, 0x0A, 0x12};
  byte trombon_right[] = { 0x10, 0x18, 0x1C, 0x0C, 0x0C, 0x00, 0x0A, 0x09};
  lcd.createChar(0, center_empty);
  lcd.createChar(1, right_empty);
  lcd.createChar(2, left_full);
  lcd.createChar(3, center_full);
  lcd.createChar(4, left);
  lcd.createChar(5, right);
  lcd.createChar(6, trombon_left);
  lcd.createChar(7, trombon_right);
}

//fillBar2 принимает аргументы (столбец, строка, длина полосы, значение в % (0 - 100) )
void fillBar2(byte start_pos, byte row, byte bar_length, byte fill_percent) {
  Serial.println(fill_percent);
  byte infill = round((float)bar_length * fill_percent / 100);
  lcd.setCursor(start_pos, row);
  lcd.write(2);   // left_full
  if (fill_percent == 255){
    infill = 0;
    lcd.write(0);
  } else {
    lcd.write(3);
  }
  for (int n = 1; n < bar_length - 2; n++) {
    if (n < infill) lcd.write(3);  // center_full
    if (n >= infill) lcd.write(0);  // center_empty
  }
  lcd.write(1);  // right_empty
}


// ========== Вспомогательные функции ==========

int isQueueEmpty(struct CircularQueue *q) {
  return (q->qSize == 0);
}

int isQueueFull(struct CircularQueue *q) {
  return (q->qSize >= q->capacity);
}

int getQueueSize(struct CircularQueue *q) {
  return q->qSize;
}

// Размер дека максимумов
int dequeSize(struct CircularQueue *q) {
  if(q->dTail >= q->dHead) {
    return q->dTail - q->dHead;
  } else {
    return q->capacity - q->dHead + q->dTail;
  }
}

// Получить индекс с головы дека максимумов
int dequeFront(struct CircularQueue *q) {
  if(dequeSize(q) == 0) {
    Serial.println("ERROR: deque is empty (front)");
    return -1;
  }
  return q->maxDeque[q->dHead];
}

// Получить индекс с хвоста дека максимумов
int dequeBack(struct CircularQueue *q) {
  if(dequeSize(q) == 0) {
    Serial.println("ERROR: deque is empty (back)");
    return -1;
  }
  int idx = (q->dTail - 1 + q->capacity) % q->capacity;
  return q->maxDeque[idx];
}

// Удалить элемент с головы дека максимумов
void dequePopFront(struct CircularQueue *q) {
  if(dequeSize(q) > 0) {
    q->dHead = (q->dHead + 1) % q->capacity;
  }
}

// Удалить элемент с хвоста дека максимумов
void dequePopBack(struct CircularQueue *q) {
  if(dequeSize(q) > 0) {
    q->dTail = (q->dTail - 1 + q->capacity) % q->capacity;
  }
}

// Добавить элемент в хвост дека максимумов
void dequePushBack(struct CircularQueue *q, int idx) {
  q->maxDeque[q->dTail] = idx;
  q->dTail = (q->dTail + 1) % q->capacity;
}

// ========== Основные функции очереди ==========

void enqueue(struct CircularQueue *q, float value) {
  if(q == NULL) {
    Serial.println("ERROR: queue is NULL in enqueue");
    qError = 3;
    return;
  }
  
  if(isQueueFull(q)) {
    Serial.println("WARNING: queue is full, cannot enqueue");
    return;
  }
  
  // Добавляем значение в очередь
  q->values[q->qTail] = value;
  int newIdx = q->qTail;
  q->qTail = (q->qTail + 1) % q->capacity;
  q->qSize++;
  
  // Поддерживаем монотонно убывающий дек для максимумов
  // Удаляем с хвоста все элементы меньше текущего
  while(dequeSize(q) > 0) {
    int backIdx = dequeBack(q);
    if(q->values[backIdx] < value) {
      dequePopBack(q);
    } else {
      break;
    }
  }
  
  // Добавляем текущий индекс в хвост дека
  dequePushBack(q, newIdx);
}

float dequeue(struct CircularQueue *q) {
  if(q == NULL) {
    Serial.println("ERROR: queue is NULL in dequeue");
    qError = 3;
    return 0.0;
  }
  
  if(isQueueEmpty(q)) {
    Serial.println("ERROR: queue is empty in dequeue");
    qError = 2;
    return 0.0;
  }
  
  // Получаем значение с головы очереди
  float value = q->values[q->qHead];
  int oldIdx = q->qHead;
  q->qHead = (q->qHead + 1) % q->capacity;
  q->qSize--;
  
  // Если удаляемый элемент был максимумом, удаляем его из дека
  if(dequeSize(q) > 0 && dequeFront(q) == oldIdx) {
    dequePopFront(q);
  }
  
  return value;
}

float getMaximum(struct CircularQueue *q) {
  if(q == NULL) {
    Serial.println("ERROR: queue is NULL in getMaximum");
    qError = 3;
    return 0.0;
  }
  
  if(isQueueEmpty(q) || dequeSize(q) == 0) {
    return 0.0;
  }
  
  // Максимум всегда в голове дека
  int maxIdx = dequeFront(q);
  return q->values[maxIdx];
}