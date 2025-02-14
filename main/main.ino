#include <charMap.h>
#include <GyverOLED.h>
#include <icons_7x7.h>
#include <icons_8x8.h>

GyverOLED<SSH1106_128x64> oled;

const float SENSOR_MAP[11][2] = {
  {0.1, -0.1},
  {0.2, -0.08},
  {0.4, -0.06},
  {0.6, -0.04},
  {0.8, -0.02},
  {1, 0},
  {1.23, 0.02},
  {1.47, 0.04},
  {1.7, 0.06},
  {1.93, 0.08},
  {2.17, 0.1}
};

const int SENSOR_MAP_SIZE = 11; 

void setup() {
  Serial.begin(9600);
  oled.init(0x3C);
  

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static unsigned long timer;
  float volts = readVolts();
  if (millis() - timer > 50){
    updateDisplay(volts);
    Serial.println(volts);
  }
} 

void updateDisplay(float volts){
  oled.clear();
  oled.home();
  oled.print(volts);
  oled.println();
  oled.print(getPressureFromVolts(volts));
  oled.update();
}

float readVolts(){
  int readFromAnalog = analogRead(A0);
  float volt = (float)(readFromAnalog * 5.0) / 1024;
  return volt;
}

float getPressureFromVolts(float volts){
  if (volts > SENSOR_MAP[SENSOR_MAP_SIZE - 1][0]){
    return SENSOR_MAP[SENSOR_MAP_SIZE][1];
  }

  int target = 0;
  for (int i = 1; i < SENSOR_MAP_SIZE; i++){
    if (SENSOR_MAP[i][0] > volts){
      target = i;
      break;
    }
  }
  float linCoef = (volts - SENSOR_MAP[target - 1][0]) / (SENSOR_MAP[target][0] - SENSOR_MAP[target - 1][0]);
  float res = ((SENSOR_MAP[target][1] - SENSOR_MAP[target - 1][1]) * linCoef) + SENSOR_MAP[target - 1][1];
  return res;
}

void blinkLED() {
  digitalWrite(LED_BUILTIN, HIGH);  
  delay(1000);                      
  digitalWrite(LED_BUILTIN, LOW);   
  delay(1000);      
}                