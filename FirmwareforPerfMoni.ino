#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "FS.h"
#include <LittleFS.h> 

// --- PINS ---
const int buttonPin = 13;
const int vibroPin  = 46; 
const int sdaPin    = 16; 
const int sclPin    = 15; 
const int ecgPin    = 4;  
const int loPlus    = 7;  
const int loMinus   = 6;  

Adafruit_MPU6050 mpu;
File dataFile;       

// --- VARIABLES ---
bool isMonitoring = false;      
int lastButtonState = HIGH;     
int currentButtonState;

// Physics Trackers
float velocityZ = 0.0; float pitchAngle = 0.0;     
float biasZ = 0.0; float biasX = 0.0; float initialTilt = 0.0; 
unsigned long prevTime = 0; 
unsigned long stopTimer = 0; 

// --- NEW: BPM VARIABLES ---
const int Threshold = 2600;  // Threshold for identifying a "Beat" (Adjust if needed)
unsigned long lastBeatTime = 0;
int bpm = 0;
int beatIndex = 0;
int beats[10]; // Array to average the last 10 beats
int averageBPM = 0;

// --- HELPER: VIBRATION ---
void vibrateTwice() {
  digitalWrite(vibroPin, HIGH); delay(400);
  digitalWrite(vibroPin, LOW);  delay(200);
  digitalWrite(vibroPin, HIGH); delay(400);
  digitalWrite(vibroPin, LOW);
}

// --- HELPER: DUMP FILES ---
void dumpFile(String fname) {
  File file = LittleFS.open(fname, "r");
  if (!file) return;
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void dumpAllFiles() {
  Serial.println("\n\n>>> DUMP MODE ACTIVATED <<<");
  delay(500);
  int i = 1;
  while (LittleFS.exists("/session_" + String(i) + ".csv")) {
    String fName = "/session_" + String(i) + ".csv";
    Serial.print("=== START OF FILE: "); Serial.print(fName); Serial.println(" ===");
    dumpFile(fName);
    Serial.println();
    Serial.println("=== END OF DATA ===");
    i++;
  }
  Serial.println(">>> ALL FILES DUMPED <<<");
}

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(vibroPin, OUTPUT); digitalWrite(vibroPin, LOW);
  pinMode(loPlus, INPUT); pinMode(loMinus, INPUT);

  Wire.begin(sdaPin, sclPin);
  
  if (!LittleFS.begin(true)) { Serial.println("Memory Error"); return; }
  if (!mpu.begin()) { Serial.println("MPU Not Found"); while (1); }
  
  mpu.setAccelerometerRange(MPU6050_RANGE_2_G); 
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("System Ready. Press Button.");
  digitalWrite(vibroPin, HIGH); delay(200); digitalWrite(vibroPin, LOW);
}

void loop() {
  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 'D' || cmd == 'd') {
      isMonitoring = false; 
      if (dataFile) dataFile.close();
      dumpAllFiles();
    }
  }

  currentButtonState = digitalRead(buttonPin);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    isMonitoring = !isMonitoring; 
    vibrateTwice(); 

    if (isMonitoring) {
      Serial.println(">>> CALIBRATING... <<<");
      float sumZ = 0; float sumX = 0;
      for (int i = 0; i < 50; i++) {
        sensors_event_t a, g, temp; mpu.getEvent(&a, &g, &temp);
        sumZ += a.acceleration.z; sumX += a.acceleration.x; delay(3);
      }
      initialTilt = atan2((sumZ/50), (sumX/50)) * 57.2958;
      pitchAngle = initialTilt;
      float rad = initialTilt * 0.0174533;
      biasX = (sumX/50) - (9.81 * cos(rad));
      biasZ = (sumZ/50) - (9.81 * sin(rad));
      velocityZ = 0; prevTime = millis();
      
      // Reset BPM
      averageBPM = 0; beatIndex = 0; lastBeatTime = millis();
      
      int index = 1;
      while (LittleFS.exists("/session_" + String(index) + ".csv")) index++;
      String fileName = "/session_" + String(index) + ".csv";
      
      dataFile = LittleFS.open(fileName, "w");
      if (dataFile) {
        // UPDATED HEADER: Added BPM
        dataFile.println("Time,Tilt,AccFwd,AccUp,SpeedZ,ECG,BPM");
        Serial.print("Recording to: "); Serial.println(fileName);
        Serial.println("Time \t Tilt \t Speed \t ECG \t BPM"); 
      }
    } else {
      Serial.println(">>> STOPPED. SAVED. <<<");
      if (dataFile) dataFile.close();
    }
  }
  lastButtonState = currentButtonState;
  delay(20); // Higher sampling rate needed for ECG (20ms)

  if (isMonitoring && dataFile) {
    sensors_event_t a, g, temp; mpu.getEvent(&a, &g, &temp);
    unsigned long currentTime = millis();
    float dt = (currentTime - prevTime) / 1000.0; prevTime = currentTime; 

    // 1. Motion Logic
    float calibratedAccelX = a.acceleration.x - biasX; 
    float gyroY = g.gyro.y * 57.2958; 
    float accelPitch = atan2(a.acceleration.z, calibratedAccelX) * 57.2958;
    pitchAngle = 0.98 * (pitchAngle + gyroY * dt) + 0.02 * accelPitch;
    float rad = pitchAngle * 0.0174533; 
    float accelForward = (a.acceleration.z - biasZ) - (9.81 * sin(rad)); 
    float accelUp = (calibratedAccelX * cos(rad) + a.acceleration.z * sin(rad)) - 9.81;

    if (abs(accelForward) < 0.5) { 
      accelForward = 0; velocityZ *= 0.9;
      stopTimer += (dt*1000); if (stopTimer > 200) velocityZ = 0;
    } else stopTimer = 0;
    if (abs(accelUp) < 0.5) accelUp = 0;
    velocityZ += (accelForward * dt); 
    if (velocityZ < 0) velocityZ = 0;

    // 2. ECG & BPM Logic
    int ecgValue = 0;
    if ((digitalRead(loPlus) == 1) || (digitalRead(loMinus) == 1)) {
      ecgValue = 0; // Flatline if disconnected
      averageBPM = 0; // Reset BPM if leads off
    } else {
      ecgValue = analogRead(ecgPin); 
      
      // BPM CALCULATION
      // Only count a beat if signal > Threshold AND it's been > 300ms since last beat (Max 200 BPM)
      if (ecgValue > Threshold && (currentTime - lastBeatTime > 300)) {
         long duration = currentTime - lastBeatTime;
         lastBeatTime = currentTime;
         
         // Calculate Instant BPM
         int instantBPM = 60000 / duration;
         
         // Smooth it (Running average)
         beats[beatIndex] = instantBPM;
         beatIndex = (beatIndex + 1) % 10; // Cycle 0-9
         
         // Calculate Average
         long total = 0;
         for(int k=0; k<10; k++) total += beats[k];
         averageBPM = total / 10;
      }
    }

    // 3. Save to File (Added BPM)
    dataFile.print(currentTime); dataFile.print(",");
    dataFile.print(pitchAngle, 1); dataFile.print(",");
    dataFile.print(accelForward, 2); dataFile.print(",");
    dataFile.print(accelUp, 2); dataFile.print(",");
    dataFile.print(velocityZ, 2); dataFile.print(",");
    dataFile.print(ecgValue); dataFile.print(",");
    dataFile.println(averageBPM); // <--- New Data

    // 4. Real Time Print
    Serial.print(currentTime); Serial.print("\t");
    Serial.print(pitchAngle, 1); Serial.print("\t");
    Serial.print(velocityZ, 2); Serial.print("\t");
    Serial.print(ecgValue); Serial.print("\t");
    Serial.println(averageBPM); 
  }
}