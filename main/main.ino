#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const int  MAX_VAL_PERIOD_SEC = 30;
const float BAR_MIN = 0.8;
const float BAR_MAX = 3.3;

struct dStack *Q = NULL;
struct dStack *D = NULL;
int qError = 0;

float maxPressInSec = 0;


struct dStack{
    float* data;
    int numElems;
    long cap, top1, top2;
};


/*
vvvvvvvvvvvvvvvvvvvvvvv*/
void initStack(struct dStack *s, long nel, float *data);
int stackEmpty1(struct dStack* s);
int stackEmpty2(struct dStack* s);
void push1(struct dStack* s, float x);
void push2(struct dStack* s, float x);
float pop1(struct dStack* s);
float pop2(struct dStack* s);
int queueEmpty(struct dStack* s);
float popQueue(struct dStack* s);
/*deque functions
vvvvvvvvvvvvvvvvvvvvvvv*/
float getRearElementDeq(struct dStack* D);
float getFirstElementDeq(struct dStack* D);
float getFirstElementQue(struct dStack* Q);
float popFrontDeque(struct dStack* D);
void Enqueue(struct dStack* Q, struct dStack* D, float x);
float Dequeue(struct dStack* Q, struct dStack* D);
float Maximum(struct dStack* D);
/*vvvvvvvvvvvvvvvvvvvvvvvv*/

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

void initializeQ(struct dStack **Q, struct dStack **D){
  int sizeOfStack = MAX_VAL_PERIOD_SEC + 5;
  
  Serial.print("Allocating memory for stacks, size: ");
  Serial.println(sizeOfStack);
  
  *Q = (dStack*)malloc(sizeof(struct dStack));
  if(*Q == NULL){
    Serial.println("ERROR: malloc Q failed");
    qError = 1;
    return;
  }
  
  *D = (dStack*)malloc(sizeof(struct dStack));
  if(*D == NULL){
    Serial.println("ERROR: malloc D failed");
    qError = 1;
    return;
  }
  
  float* dataQ = (float*)calloc(sizeOfStack, sizeof(float));
  if(dataQ == NULL){
    Serial.println("ERROR: calloc dataQ failed");
    qError = 1;
    return;
  }
  
  float* dataD = (float*)calloc(sizeOfStack, sizeof(float));
  if(dataD == NULL){
    Serial.println("ERROR: calloc dataD failed");
    qError = 1;
    return;
  } 
  
  Serial.println("Memory allocation successful!");
  initStack(*Q, sizeOfStack, dataQ);
  initStack(*D, sizeOfStack, dataD);
  (*Q)->numElems = 0;
  Serial.println("Stack initialization complete");
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
  
  initializeQ(&Q, &D);
  
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
  
  int readFromAnalog = analogRead(A0);
  float currentPressure = getPressureFromAnalog(readFromAnalog);
  
  if (millis() - queueTimer > 1000){
    // сюда заходим раз в секунду
    if(Q != NULL && D != NULL){
      if(firstRun){
        Serial.println("First enqueue operation");
        firstRun = false;
      }
      
      Enqueue(Q, D, maxPressInSec);
      Serial.print("ENQ: val=");
      Serial.print(maxPressInSec, 2);
      Serial.print(" | qSize=");
      Serial.print(Q->numElems);
      
      if (Q->numElems > MAX_VAL_PERIOD_SEC){
         float dequeued = Dequeue(Q, D);
         Serial.print(" | DEQ=");
         Serial.print(dequeued, 2);
      }
      
      float maxVal = Maximum(D);
      Serial.print(" | MAX=");
      Serial.println(maxVal, 2);
      
      updateMax(maxVal);
    } else {
      Serial.println("ERROR: Q or D is NULL in loop");
    }
    maxPressInSec = 0;
    queueTimer = millis();
  }
  
  if (currentPressure > maxPressInSec){
    maxPressInSec = currentPressure;
  }
  
  if (millis() - displayTimer > 50){
    updateDisplay(readFromAnalog);
    displayTimer = millis();
  }
} 

void updateDisplay(int rawAnalog){
  float volts = convertToVolts(rawAnalog);
  int currentPercent = (int) (((float)rawAnalog / 1023.0) * 100);
  float currentPressure = getPressureFromAnalog(rawAnalog);
  
  // Строка 0: символы + текущее давление
  lcd.setCursor(0, 0);
  lcd.write(6);
  lcd.write(7);
  lcd.print(" ");
  
  // Форматируем текущее давление (фиксированная ширина)
  if (currentPressure >= 0.0){
    lcd.print(" "); // пробел для выравнивания
  }
  lcd.print(currentPressure, 2); // 2 знака после запятой
  lcd.print("    "); // очистка остатков
  
  // Строка 1: прогресс-бар или другая информация
  //fillBar2(0, 1, 16, currentPercent);
}

void updateMax(float maxValue){
  // Выводим MAX на второй строке
  lcd.setCursor(0, 1);
  if (qError != 0){
    lcd.print("Error: E");
    lcd.print(qError);
    lcd.print("       "); // очистка остатков
  } else {
    lcd.print("MAX:");
    if (maxValue < 0.0){
      maxValue = 0.0;
    }
    lcd.print(maxValue, 2); // 2 знака после запятой
    lcd.print("       "); // очистка остатков
  }
}

float convertToVolts(int x){
  float volt = (float)(x * 5.0) / 1024;
  return volt;
}

float getPressureFromAnalog(int x){
  if (x > SENSOR_MAP[SENSOR_MAP_SIZE - 1][0]){
    return SENSOR_MAP[SENSOR_MAP_SIZE][1];
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
  byte left_empty[8] = {0b11111,  0b10000,  0b10000,  0b10000,  0b10000,  0b10000,  0b10000,  0b11111};
  byte center_empty[8] = {0b11111, 0b00000,  0b00000,  0b00000,  0b00000,  0b00000,  0b00000,  0b11111};
  byte left_full[8] = {0b11111, 0b10000, 0b10111, 0b10111, 0b10111, 0b10111, 0b10000, 0b11111};
  byte right_full[8] = {0b11111, 0b00001, 0b11101, 0b11101, 0b11101, 0b11101, 0b00001, 0b11111};
  byte center_full[8] = {0b11111, 0b00000, 0b11111, 0b11111, 0b11111, 0b11111, 0b00000, 0b11111};
  byte left[] = {0x00, 0x03, 0x04, 0x05, 0x05, 0x04, 0x03, 0x00};
  byte right[] = { 0x01,0x1F, 0x05, 0x14, 0x14, 0x04, 0x18, 0x00};
  lcd.createChar(0, left_empty);
  lcd.createChar(1, center_empty);
  lcd.createChar(2, right_empty);
  lcd.createChar(3, left_full);
  lcd.createChar(4, center_full);
  lcd.createChar(5, right_full);
  lcd.createChar(6, left);
  lcd.createChar(7, right);
}

//fillBar2 принимает аргументы (столбец, строка, длина полосы, значение в % (0 - 100) )
void fillBar2(byte start_pos, byte row, byte bar_length, byte fill_percent) {
  Serial.println(fill_percent);
  byte infill = round((float)bar_length * fill_percent / 100);
  lcd.setCursor(start_pos, row);
  if (infill == 0) lcd.write(0);
  else lcd.write(3);
  for (int n = 1; n < bar_length - 1; n++) {
    if (n < infill) lcd.write(4);
    if (n >= infill) lcd.write(1);
  }
  if (infill == bar_length) lcd.write(5);
  else lcd.write(2);
}


void initStack(struct dStack *s, long nel, float *data) {
    s->data = data;
    s->cap = nel;
    s->top1 = 0;
    s->top2 = nel - 1;
}

int stackEmpty1(struct dStack* s) {
    if(s->top1 == (long)0)
        return 1;
    else
        return 0;
}

int stackEmpty2(struct dStack *s) {
    if(s->top2 == s->cap - 1)
        return 1;
    else
        return 0;
}

void push1(struct dStack *s, float x) {
    if(s->top2 < s->top1){
        Serial.println("push1 overflow");
        return;
    }
    s->data[s->top1] = x;
    s->top1 += 1;
}

void push2(struct dStack *s, float x) {
    if(s->top2 < s->top1){
        Serial.println("push2 overflow");
        return;
    }
    s->data[s->top2] = x;
    s->top2 -= 1;
}

float pop1(struct dStack *s) {
    if(stackEmpty1(s) == 1){
        Serial.println("ERROR: pop1 empty");
        return 0.0;
    }
    s->top1 -= 1;
    float x = s->data[s->top1];
    return x;
}

float pop2(struct dStack *s) {
    if (stackEmpty2(s) == 1){
        Serial.println("ERROR: pop2 empty");
        return 0.0;
    }
    s->top2 += 1;
    float x = s->data[s->top2];
    return x;
}

int queueEmpty(struct dStack *s) {
    if(stackEmpty1(s) == 1 && stackEmpty2(s) == 1)
        return 1;
    else
        return 0;
}

float popQueue(struct dStack *s) { //also deck pop from end
    if(stackEmpty2(s) == 1)
        while (stackEmpty1(s) == 0)
            push2(s, pop1(s));
    float x = pop2(s);
    return x;
}

float getRearElementDeq(struct dStack *D) {
    if(D->top2 + 1 < D->cap){
        return D->data[D->top2 + 1];
    }
    else{
        return D->data[0];
    }
}

float getFirstElementDeq(struct dStack *D) {
    float res;
    if(D->top1 != 0)
        res =  D->data[D->top1 - 1];
    else
        res = D->data[D->cap - 1];
    return res;
}

float getFirstElementQue(struct dStack *Q) {
    if(Q->top2 == Q->cap - 1)
        return Q->data[0];
    else
        return Q->data[Q->top2 + 1];
}

float popFrontDeque(struct dStack *D) {
    if(stackEmpty1(D) == 1)
        while (stackEmpty2(D) == 0)
            push1(D, pop2(D));
    float x = pop1(D);
    return x;
}

void Enqueue(struct dStack *Q, struct dStack *D, float x) {
    if(Q == NULL || D == NULL){
        Serial.println("ERROR: Q or D is NULL");
        qError = 3;
        return;
    }
    
    if(queueEmpty(Q) == 1){
        push1(Q, x);
        push2(D, x);
    }
    else{
        push1(Q, x);
        while((getRearElementDeq(D) < x) && (queueEmpty(D) == 0)){
            if(stackEmpty2(D) == 0){
                pop2(D);
            } else{
                // Перемещаем один элемент из stack1 в stack2, затем pop
                if(stackEmpty1(D) == 0){
                    push2(D, pop1(D));
                    pop2(D);
                }
            }
        }
        push2(D, x);
    }
    Q->numElems++;
}

float Dequeue(struct dStack *Q, struct dStack *D) {
    float res;
    if(Q == NULL || D == NULL){
        Serial.println("ERROR: Q or D is NULL in Dequeue");
        qError = 3;
        return 0.0;
    }
    if (Q->numElems == 0){
      qError = 2;
      Serial.println("ERROR: Queue is empty");
      return 0.0;
    }
    if(getFirstElementDeq(D) == getFirstElementQue(Q)){
        res = popQueue(Q);
        popFrontDeque(D);
    } else{
        res = popQueue(Q);
    }
    Q->numElems--;
    return res;
}

float Maximum(struct dStack *D) {
    if(D == NULL){
        Serial.println("ERROR: D is NULL in Maximum");
        qError = 3;
        return 0.0;
    }
    if(queueEmpty(D) == 1){
        return 0.0;
    }
    return getFirstElementDeq(D);
}