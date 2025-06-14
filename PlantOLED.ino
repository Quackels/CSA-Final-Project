#include <Wire.h>
#include <SoftwareWire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <U8g2lib.h>

#define DHTPIN 2
#define DHTTYPE DHT11
#define LIGHTPIN A0

#define SOILPIN1 A1
#define SOILPIN2 A2
#define SOILPIN3 A3
#define SOILPIN4 A6

const int trigPin = 9;
const int echoPin = 10;

#define FIREFLY_PIN 5

DHT dht(DHTPIN, DHTTYPE);

LiquidCrystal_I2C statsLCD(0x27, 20, 4);           
SoftwareWire myWire(3, 4);                        
LiquidCrystal_I2C dialogueLCD(0x26, 20, 4);      

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

unsigned long previousSensorUpdate = 0;
unsigned long previousOLEDUpdate = 0;
unsigned long previousDialogueUpdate = 0;
unsigned long previousEyeUpdate = 0;
unsigned long previousFireflyUpdate = 0;
unsigned long emotionStartTime = 0;

const long sensorUpdateInterval = 3000;    
const long oledUpdateInterval = 50;       
const long dialogueUpdateInterval = 100;   
const long fireflyUpdateInterval = 20;     
const long openEyeDuration = 3000;        
const long blinkDuration = 200;           
const long emotionDuration = 3000;        

struct FireflyLight {
  int pin;
  int brightness;
  int targetBrightness;
  float fadeSpeed;
  unsigned long lastUpdate;
  bool fadingUp;
};

FireflyLight firefly = {FIREFLY_PIN, 0, 150, 1.5, 0, true};

enum EmotionState {
  NEUTRAL,
  HAPPY,
  SAD,
  SLEEPING 
};

String lastMessage = "";
bool eyesOpen = true;
bool needsDialogueUpdate = false;
int distance = 0;
EmotionState currentEmotion = NEUTRAL;
EmotionState previousEmotion = NEUTRAL;

float temp = 0;
float humidity = 0;
int lightRaw = 0;

int soilRaw1 = 0;
int soilRaw2 = 0;
int soilRaw3 = 0;
int soilRaw4 = 0;
int soilRaw = 0; 
int tempPercent = 0;
int humidPercent = 0;
int lightPercent = 0;
int soilPercent = 0;

const int LOW_SOIL_THRESHOLD = 50;  
const int HIGH_TEMP_THRESHOLD = 60; 
const int LOW_HUMIDITY_THRESHOLD = 50;  
const int LOW_LIGHT_THRESHOLD = 40;  

void initializeFireflies() {
  pinMode(firefly.pin, OUTPUT);
  firefly.targetBrightness = random(100, 200); 
}

void updateFireflyLights() {
  if (firefly.brightness < firefly.targetBrightness) {
    firefly.brightness += firefly.fadeSpeed;
    if (firefly.brightness >= firefly.targetBrightness) {
      firefly.brightness = firefly.targetBrightness;
      firefly.fadingUp = false;
      firefly.targetBrightness = random(10, 60);
    }
  } else if (firefly.brightness > firefly.targetBrightness) {
    firefly.brightness -= firefly.fadeSpeed;
    if (firefly.brightness <= firefly.targetBrightness) {
      firefly.brightness = firefly.targetBrightness;
      firefly.fadingUp = true;
      // Set new bright target when reaching dim level
      firefly.targetBrightness = random(120, 255);
    }
  }
  
  firefly.brightness = constrain(firefly.brightness, 0, 255);
  analogWrite(firefly.pin, firefly.brightness);
}

int readSoilSensors() {
  soilRaw1 = analogRead(SOILPIN1);
  soilRaw2 = analogRead(SOILPIN2);
  soilRaw3 = analogRead(SOILPIN3);
  soilRaw4 = analogRead(SOILPIN4);
  
  delay(10);
  
  int totalReading = soilRaw1 + soilRaw2 + soilRaw3 + soilRaw4;
  int averageReading = totalReading / 4;

  int validReadings = 0;
  int validTotal = 0;
  
  int readings[] = {soilRaw1, soilRaw2, soilRaw3, soilRaw4};
  
  for (int i = 0; i < 4; i++) {
    if (readings[i] >= 200 && readings[i] <= 900) {
      validTotal += readings[i];
      validReadings++;
    }
  }

  if (validReadings >= 2) {
    averageReading = validTotal / validReadings;
    Serial.print("Using filtered average from ");
    Serial.print(validReadings);
    Serial.println(" sensors");
  } else {
    Serial.println("Using simple average (some sensors may be faulty)");
  }
  
  Serial.print("Soil sensors - S1: "); Serial.print(soilRaw1);
  Serial.print(", S2: "); Serial.print(soilRaw2);
  Serial.print(", S3: "); Serial.print(soilRaw3);
  Serial.print(", S4: "); Serial.print(soilRaw4);
  Serial.print(", Average: "); Serial.println(averageReading);
  
  return averageReading;
}

void drawBar(int row, int value, const char* label) {
  statsLCD.setCursor(0, row);
  statsLCD.print(label);
  statsLCD.print(": ");

  int bars = map(value, 0, 100, 0, 10); 
  for (int i = 0; i < 10; i++) {
    if (i < bars) {
      statsLCD.write(byte(255));
    } else {
      statsLCD.print(" ");
    }
  }
}

void printWrappedSlow(LiquidCrystal_I2C &lcd, const String &msg, int delayMs = 40) {
  lcd.clear();
  int row = 0, col = 0;
  String word = "";

  for (unsigned int i = 0; i <= msg.length(); i++) {
    char c = msg[i];
    if (c == ' ' || c == '\0') {
      if (col + word.length() > 20) {
        row++;
        col = 0;
        if (row >= 4) break;
      }
      lcd.setCursor(col, row);
      for (char wc : word) {
        lcd.print(wc);
        delay(delayMs);
      }
      col += word.length();
      if (c == ' ') {
        if (col < 20) {
          lcd.setCursor(col, row);
          lcd.print(' ');
          delay(delayMs);
          col++;
        } else {
          row++;
          col = 0;
        }
      }
      word = "";
    } else {
      word += c;
    }
  }
}

void drawNeutralFace() {
  unsigned long currentMillis = millis();
  
  if (eyesOpen && currentMillis - previousEyeUpdate >= openEyeDuration) {
    eyesOpen = false;
    previousEyeUpdate = currentMillis;
  } else if (!eyesOpen && currentMillis - previousEyeUpdate >= blinkDuration) {
    eyesOpen = true;
    previousEyeUpdate = currentMillis;
  }

  if (eyesOpen) {
    u8g2.drawDisc(40, 32, 8);  
    u8g2.drawDisc(40, 36, 8);
    u8g2.drawDisc(40, 40, 8); 

    u8g2.drawDisc(88, 32, 8);  
    u8g2.drawDisc(88, 36, 8);
    u8g2.drawDisc(88, 40, 8);  
  } else {
    u8g2.drawLine(32, 36, 48, 36);
    u8g2.drawLine(80, 36, 96, 36);   
  }
}

void drawHappyFace() {
  u8g2.drawDisc(40, 35, 10); 
  u8g2.drawDisc(88, 35, 10);
  
  u8g2.drawCircle(64, 45, 15); 
  u8g2.drawBox(49, 30, 30, 20);
}

void drawSadFace() {
  u8g2.drawDisc(40, 35, 8);
  u8g2.drawDisc(88, 35, 8);

  u8g2.drawCircle(64, 55, 12);  
  u8g2.drawBox(52, 55, 24, 15); 
}

void drawSleepingFace() {
  // Sleeping face with closed/flat eyes
  u8g2.drawLine(32, 36, 48, 36);
  u8g2.drawLine(80, 36, 96, 36);
  u8g2.drawLine(97, 18, 106, 18);
  u8g2.drawLine(106, 18, 98, 26); 
  u8g2.drawLine(109, 5, 117, 5);
  u8g2.drawLine(97, 26, 106, 26);
  u8g2.drawLine(117, 5, 109, 13);
  u8g2.drawLine(108, 13, 117, 13);
}

bool hasLowStats() {
  return (soilPercent < LOW_SOIL_THRESHOLD || 
          tempPercent > HIGH_TEMP_THRESHOLD || 
          humidPercent < LOW_HUMIDITY_THRESHOLD ||
          lightPercent < LOW_LIGHT_THRESHOLD);
}

bool statsRecovered() {
  return (soilPercent >= LOW_SOIL_THRESHOLD + 10 && 
          tempPercent <= HIGH_TEMP_THRESHOLD - 10 && 
          humidPercent >= LOW_HUMIDITY_THRESHOLD + 10 &&
          lightPercent >= LOW_LIGHT_THRESHOLD + 10);
}

void updateEmotionState() {
  unsigned long currentMillis = millis();
  
  if (currentEmotion != NEUTRAL && currentEmotion != SLEEPING && 
      currentMillis - emotionStartTime >= emotionDuration) {
    currentEmotion = NEUTRAL;
    Serial.println("Emotion expired, returning to neutral");
  }
  
  if (currentEmotion == NEUTRAL || currentEmotion == SLEEPING) {
    if (distance == 0 || distance >= 80) {
      if (currentEmotion != SLEEPING) {
        currentEmotion = SLEEPING;
        Serial.println("No one nearby - sleeping face");
      }
    }
    else if (hasLowStats() && currentEmotion != SAD) {
      currentEmotion = SAD;
      emotionStartTime = currentMillis;
      Serial.print("Stats low - showing sad face. Low stats: ");
      if (soilPercent < LOW_SOIL_THRESHOLD) Serial.print("Soil ");
      if (tempPercent > HIGH_TEMP_THRESHOLD) Serial.print("Temp ");
      if (humidPercent < LOW_HUMIDITY_THRESHOLD) Serial.print("Humidity ");
      if (lightPercent < LOW_LIGHT_THRESHOLD) Serial.print("Light ");
      Serial.println();
    }
    else if (previousEmotion == SAD && statsRecovered() && currentEmotion != HAPPY) {
      currentEmotion = HAPPY;
      emotionStartTime = currentMillis;
      Serial.println("Stats recovered - showing happy face");
    }
    else if (distance > 0 && distance < 80 && currentEmotion == SLEEPING) {
      currentEmotion = NEUTRAL;
      eyesOpen = true; 
      previousEyeUpdate = currentMillis;
      Serial.println("Someone nearby - neutral face");
    }
  }
  
  Serial.print("Current emotion: ");
  switch(currentEmotion) {
    case NEUTRAL: Serial.println("NEUTRAL"); break;
    case HAPPY: Serial.println("HAPPY"); break;
    case SAD: Serial.println("SAD"); break;
    case SLEEPING: Serial.println("SLEEPING"); break;
  }
  
  previousEmotion = currentEmotion;
}

void readUltrasonicSensor() {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH);

  distance = duration * 0.034 / 2;
}

void updateSensors() {
  temp = dht.readTemperature();
  humidity = dht.readHumidity();
  lightRaw = analogRead(LIGHTPIN);
  soilRaw = readSoilSensors(); 

  tempPercent = constrain(map(temp, 0, 50, 0, 100), 0, 100);
  humidPercent = constrain(map(humidity, 0, 100, 0, 100), 0, 100);
  lightPercent = constrain(map(lightRaw, 0, 1023, 0, 100), 0, 100);
  soilPercent = constrain(map(soilRaw, 300, 800, 100, 0), 0, 100);

  Serial.print("Stats - Temp: "); Serial.print(tempPercent);
  Serial.print("%, Humidity: "); Serial.print(humidPercent);
  Serial.print("%, Light: "); Serial.print(lightPercent);
  Serial.print("%, Soil: "); Serial.print(soilPercent);
  Serial.print("%, Distance: "); Serial.println(distance);

  drawBar(0, tempPercent, "Temp ");
  drawBar(1, humidPercent, "Humid");
  drawBar(2, lightPercent, "Light");
  drawBar(3, soilPercent, "Soil ");
  
  updateEmotionState();
}

void updateDialogue() {
  String message;
  if (soilPercent < LOW_SOIL_THRESHOLD) {
    message = "Hey im kinda hungry, can u feed me ?";
  } else if (tempPercent > HIGH_TEMP_THRESHOLD) {
    message = "Is it hot in here cuz im sweating, can u turn down the heat ?";
  } else if (humidPercent < LOW_HUMIDITY_THRESHOLD) {
    message = "Im feeling kinda dry, can u mist me ?";
  } else if (lightPercent < LOW_LIGHT_THRESHOLD) {
    message = "Ahhh who turned off the lights, turn the lights on please.";
  } else {
    message = "Hii im feeling great today";
  }

  if (message != lastMessage) {
    printWrappedSlow(dialogueLCD, message);
    lastMessage = message;
  }
}

void updateOLED() {
  u8g2.clearBuffer();

  readUltrasonicSensor();
  
  updateEmotionState();

  switch (currentEmotion) {
    case HAPPY:
      drawHappyFace();
      break;
    case SAD:
      drawSadFace();
      break;
    case SLEEPING:
      drawSleepingFace();
      break;
    case NEUTRAL:
    default:
      drawNeutralFace();
      break;
  }

  u8g2.sendBuffer();
}

void setup() {
  Serial.begin(9600);
  
  dht.begin();
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  initializeFireflies();

  Wire.begin();
  statsLCD.init();
  statsLCD.backlight();
  
  delay(100);
  
  u8g2.begin();
  
  delay(100);
  
  myWire.begin();
  dialogueLCD.init();
  dialogueLCD.backlight();
  
  updateSensors();
  
  Serial.println("Plant Monitor with Firefly Ambient Lights - Ready!");
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousSensorUpdate >= sensorUpdateInterval) {
    previousSensorUpdate = currentMillis;
    updateSensors();
    needsDialogueUpdate = true; 
  }

  if (needsDialogueUpdate && currentMillis - previousDialogueUpdate >= dialogueUpdateInterval) {
    previousDialogueUpdate = currentMillis;
    updateDialogue();
    needsDialogueUpdate = false;
  }

  if (currentMillis - previousOLEDUpdate >= oledUpdateInterval) {
    previousOLEDUpdate = currentMillis;
    updateOLED();
  }

  if (currentMillis - previousFireflyUpdate >= fireflyUpdateInterval) {
    previousFireflyUpdate = currentMillis;
    updateFireflyLights();
  }

  delay(10);
}