const byte irSensor = 2; 
volatile unsigned long bladeCount = 0;
volatile unsigned long lastInterruptTime = 0;

// Debounce threshold: 500 microseconds (limits max detectable pulse rate to 2000 Hz)
const unsigned long debounceMicros = 500; 

unsigned long lastSampleTime = 0;
const unsigned long sampleInterval = 250; // 0.25s cadence

float currentSpeed = 0.0;
float previousSpeed = 0.0;
float alpha = 0.25;
float smoothedDecel = 0.0;

bool testRunning = false;
int zeroSpeedCount = 0;

// Interrupt Service Routine with Hardware Debounce Guard
void countBlade() {
  unsigned long currentTime = micros();
  if (currentTime - lastInterruptTime > debounceMicros) {
    bladeCount++;
    lastInterruptTime = currentTime;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(irSensor, INPUT_PULLUP); // Keeps input pin held HIGH to prevent floating noise
  attachInterrupt(digitalPinToInterrupt(irSensor), countBlade, FALLING);
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input == "start") {
      noInterrupts();
      bladeCount = 0;
      interrupts();

      currentSpeed = 0.0;
      previousSpeed = 0.0;
      smoothedDecel = 0.0;
      zeroSpeedCount = 0;
      testRunning = true;
      lastSampleTime = millis();
      Serial.println("{\"status\":\"Monitoring Started (Debounced)...\"}");
    }
  }

  if (testRunning && (millis() - lastSampleTime >= sampleInterval)) {
    unsigned long elapsedMs = millis() - lastSampleTime;
    lastSampleTime = millis();

    noInterrupts();
    unsigned long pulses = bladeCount;
    bladeCount = 0;
    interrupts();

    previousSpeed = currentSpeed;
    float elapsedSec = (float)elapsedMs / 1000.0;

    // Calculate actual RPS (4 blades per rotation)
    float calculatedSpeed = ((float)pulses / 4.0) / elapsedSec;

    // Sanity Cap: Ignore impossible RPM spikes (> 10,000 RPM / 166.6 RPS)
    if (calculatedSpeed > 166.6) {
      calculatedSpeed = previousSpeed; 
    }

    currentSpeed = calculatedSpeed;

    // Deceleration calculation
    float rawDecel = (previousSpeed - currentSpeed) / elapsedSec;
    smoothedDecel = (alpha * rawDecel) + ((1.0 - alpha) * smoothedDecel);

    float timeToStop = 0.0;
    String state = "Decelerating";

    if (currentSpeed <= 0.1) {
      zeroSpeedCount++;
      timeToStop = 0.0;
      state = "Stopped";

      if (zeroSpeedCount >= 4) {
        testRunning = false;
        Serial.println("{\"status\":\"Fan Completely Stopped\"}");
        return;
      }
    } else {
      zeroSpeedCount = 0;
      if (smoothedDecel > 0.1) {
        timeToStop = currentSpeed / smoothedDecel;
        state = "Decelerating";
      } else if (smoothedDecel < -0.1) {
        timeToStop = 0.0;
        state = "Accelerating";
      } else {
        timeToStop = 0.0;
        state = "Constant Speed";
      }
    }

    Serial.print("{\"speed\":");
    Serial.print(currentSpeed, 2);
    Serial.print(",\"rpm\":");
    Serial.print(currentSpeed * 60.0, 0);
    Serial.print(",\"decel\":");
    Serial.print(smoothedDecel > 0 ? smoothedDecel : 0.0, 2);
    Serial.print(",\"timeToStop\":");
    Serial.print(timeToStop, 2);
    Serial.print(",\"state\":\"");
    Serial.print(state);
    Serial.println("\"}");
  }
}