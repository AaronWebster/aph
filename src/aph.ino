/*
 * Advanced Prototype Headlamp (APH) Controller
 *
 * Hardware:
 * - Adafruit Feather 32u4 (3.3V, 8MHz)
 * - 128x32 SSD1306 OLED Display (I2C)
 * - MAX471 3A Current Sensor Module
 * - APDS9930 Digital Proximity and Ambient Light Sensor (I2C)
 * - Rotary encoder with pushbutton
 * - External MOSFET driving high power LED
 *
 * Note: For a constant current source design, consider using TPS92365x driver.
 */

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EncoderButton.h>
#include <PWMFrequency.h>
#include <SPI.h>
#include <Wire.h>
#include <APDS9930.h>

// ==== Pin Definitions ====
#define PWM_PIN 10              // PWM output to MOSFET (Timer4 on 32u4)
#define ENCODER_PIN_A 2         // Rotary encoder A
#define ENCODER_PIN_B 3         // Rotary encoder B
#define ENCODER_BUTTON_PIN 4    // Rotary encoder button
#define CURRENT_SENSE_PIN A0    // MAX471 current sensor output
#define BATTERY_VOLTAGE_PIN A1  // Battery voltage divider input

// ==== Display Configuration ====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// ==== Constants ====
#define VREF 3.3                  // Reference voltage (3.3V system)
#define ADC_MAX 1023.0            // 10-bit ADC
#define MAX471_SENSITIVITY 1.0    // MAX471 output: 1V per 1A
#define VOLTAGE_DIVIDER_RATIO 2.0 // Voltage divider ratio for battery reading
#define MAX_POWER_WATTS 10.0      // Maximum LED power setpoint (watts)
#define MIN_POWER_WATTS 0.1       // Minimum non-zero power setpoint
#define POWER_STEP 0.1            // Power adjustment step (watts)
#define PWM_MAX 255               // Maximum PWM value

// ==== Ambient Light Thresholds ====
#define AMBIENT_LUX_THRESHOLD 10.0  // Start dimming at this lux level
#define AMBIENT_LUX_MAX 100.0       // Full dim at this lux level

// ==== Proximity Sensor Settings ====
#define PROXIMITY_THRESHOLD 100     // Proximity detection threshold
#define PROXIMITY_HOLD_TIME 500     // Hold time in ms for toggle

// ==== Control Loop Settings ====
#define CONTROL_LOOP_INTERVAL 50    // Control loop interval in ms
#define DISPLAY_UPDATE_INTERVAL 200 // Display update interval in ms
#define KP 0.5                      // Proportional gain for power control

// ==== Global Objects ====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
EncoderButton encoder(ENCODER_PIN_A, ENCODER_PIN_B, ENCODER_BUTTON_PIN);
APDS9930 apds;

// ==== State Variables ====
float powerSetpoint = 0.0;        // Current power setpoint (watts)
float lastNonZeroSetpoint = 1.0;  // Last non-zero setpoint for toggle
bool ledEnabled = false;          // LED on/off state
uint8_t currentDutyCycle = 0;     // Current PWM duty cycle (0-255)

// Timing variables
unsigned long lastControlUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long proximityStartTime = 0;
bool proximityTriggered = false;
bool proximityHeld = false;

// Measured values
float measuredCurrent = 0.0;      // Measured current (A)
float batteryVoltage = 0.0;       // Battery voltage (V)
float estimatedPower = 0.0;       // Estimated power delivered (W)
float ambientLux = 0.0;           // Ambient light level (lux)
uint16_t proximityValue = 0;      // Proximity sensor value

// ==== Function Prototypes ====
void onEncoderClicked(EncoderButton& eb);
void onEncoderRotated(EncoderButton& eb);
void setupPWM();
void readSensors();
void updatePowerControl();
void updateDisplay();
void toggleLED();
void checkProximitySensor();

// ==== Encoder Callbacks ====
void onEncoderClicked(EncoderButton& eb) {
  toggleLED();
  Serial.println(F("Button pressed - LED toggled"));
}

void onEncoderRotated(EncoderButton& eb) {
  int increment = eb.increment();
  // Apply acceleration
  float adjustment = increment * abs(increment) * POWER_STEP;
  powerSetpoint += adjustment;

  // Clamp to valid range
  if (powerSetpoint < 0) {
    powerSetpoint = 0;
  } else if (powerSetpoint > MAX_POWER_WATTS) {
    powerSetpoint = MAX_POWER_WATTS;
  }

  // Save non-zero setpoint for toggle functionality
  if (powerSetpoint >= MIN_POWER_WATTS) {
    lastNonZeroSetpoint = powerSetpoint;
    ledEnabled = true;
  } else {
    powerSetpoint = 0;
    ledEnabled = false;
  }

  Serial.print(F("Setpoint: "));
  Serial.print(powerSetpoint);
  Serial.println(F(" W"));
}

// ==== Toggle LED Function ====
void toggleLED() {
  if (ledEnabled && powerSetpoint > 0) {
    // Turn off - save current setpoint
    if (powerSetpoint >= MIN_POWER_WATTS) {
      lastNonZeroSetpoint = powerSetpoint;
    }
    powerSetpoint = 0;
    ledEnabled = false;
  } else {
    // Turn on - restore last non-zero setpoint
    powerSetpoint = lastNonZeroSetpoint;
    ledEnabled = true;
  }
}

// ==== PWM Setup for >25kHz ====
void setupPWM() {
  pinMode(PWM_PIN, OUTPUT);
  analogWrite(PWM_PIN, 0);

  // Configure Timer4 for >25kHz PWM on pin 10 (32u4)
  // Using PWMFrequency library to set high frequency PWM
  // For 8MHz clock: prescaler of 1 gives highest frequency
  setPWMPrescaler(PWM_PIN, 1);

  Serial.println(F("PWM configured for >25kHz"));
}

// ==== Read All Sensors ====
void readSensors() {
  // Read current from MAX471 sensor
  int currentRaw = analogRead(CURRENT_SENSE_PIN);
  float currentVoltage = (currentRaw / ADC_MAX) * VREF;
  measuredCurrent = currentVoltage / MAX471_SENSITIVITY;

  // Read battery voltage (through voltage divider)
  int batteryRaw = analogRead(BATTERY_VOLTAGE_PIN);
  batteryVoltage = (batteryRaw / ADC_MAX) * VREF * VOLTAGE_DIVIDER_RATIO;

  // Read proximity and ambient light from APDS9930
  if (apds.readProximity(proximityValue)) {
    // Proximity read successful
  }

  if (apds.readAmbientLightLux(ambientLux)) {
    // Ambient light read successful
  }
}

// ==== Check Proximity Sensor for Toggle ====
void checkProximitySensor() {
  if (proximityValue > PROXIMITY_THRESHOLD) {
    if (!proximityTriggered) {
      // Start tracking proximity hold time
      proximityTriggered = true;
      proximityStartTime = millis();
      proximityHeld = false;
    } else if (!proximityHeld &&
               (millis() - proximityStartTime >= PROXIMITY_HOLD_TIME)) {
      // Proximity held for >0.5 seconds - toggle LED
      toggleLED();
      proximityHeld = true;
      Serial.println(F("Proximity toggle - LED toggled"));
    }
  } else {
    // Reset proximity tracking when object moves away
    proximityTriggered = false;
    proximityHeld = false;
  }
}

// ==== Update Power Control Loop ====
void updatePowerControl() {
  // Calculate effective setpoint with ambient light scaling
  float effectiveSetpoint = powerSetpoint;

  // Scale down power when ambient light exceeds threshold
  if (ambientLux > AMBIENT_LUX_THRESHOLD && effectiveSetpoint > 0) {
    float scaleFactor =
        1.0 - ((ambientLux - AMBIENT_LUX_THRESHOLD) /
               (AMBIENT_LUX_MAX - AMBIENT_LUX_THRESHOLD));
    if (scaleFactor < 0) scaleFactor = 0;
    if (scaleFactor > 1) scaleFactor = 1;
    effectiveSetpoint *= scaleFactor;
  }

  // Estimate current power output
  // Power = Voltage * Current
  // Note: measuredCurrent already reflects the actual current flowing through LED
  estimatedPower = batteryVoltage * measuredCurrent;

  // Simple proportional control to match setpoint
  if (effectiveSetpoint <= 0) {
    currentDutyCycle = 0;
  } else {
    float powerError = effectiveSetpoint - estimatedPower;
    int adjustment = (int)(powerError * KP * 255.0 / MAX_POWER_WATTS);
    int newDutyCycle = currentDutyCycle + adjustment;

    // Clamp duty cycle
    if (newDutyCycle < 0) newDutyCycle = 0;
    if (newDutyCycle > PWM_MAX) newDutyCycle = PWM_MAX;

    // Minimum duty cycle for non-zero setpoint
    if (effectiveSetpoint > 0 && newDutyCycle == 0) {
      newDutyCycle = 1;
    }

    currentDutyCycle = newDutyCycle;
  }

  // Apply PWM output
  analogWrite(PWM_PIN, currentDutyCycle);
}

// ==== Update OLED Display ====
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Line 1: Power setpoint and status
  display.setCursor(0, 0);
  display.print(F("Set: "));
  display.print(powerSetpoint, 1);
  display.print(F("W "));
  display.print(ledEnabled ? F("ON") : F("OFF"));

  // Line 2: Actual power output
  display.setCursor(0, 10);
  display.print(F("Out: "));
  display.print(estimatedPower, 2);
  display.print(F("W ("));
  display.print((currentDutyCycle * 100) / 255);
  display.print(F("%)"));

  // Line 3: Battery and current
  display.setCursor(0, 20);
  display.print(F("Bat:"));
  display.print(batteryVoltage, 1);
  display.print(F("V I:"));
  display.print(measuredCurrent * 1000, 0);
  display.print(F("mA"));

  // Show ambient light indicator if scaling is active
  if (ambientLux > AMBIENT_LUX_THRESHOLD) {
    display.setCursor(100, 0);
    display.print(F("LUX"));
  }

  display.display();
}

// ==== Setup ====
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("Advanced Prototype Headlamp starting..."));

  // Initialize I2C
  Wire.begin();

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    while (1)
      ;  // Halt
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("APH Initializing..."));
  display.display();

  // Initialize APDS9930 proximity/ambient light sensor
  if (!apds.init()) {
    Serial.println(F("APDS9930 init failed"));
    display.println(F("APDS9930 error!"));
    display.display();
    delay(1000);
  } else {
    // Enable proximity and ambient light sensors
    apds.enableProximitySensor(true);
    apds.enableLightSensor(true);
    Serial.println(F("APDS9930 initialized"));
  }

  // Setup PWM for high frequency output
  setupPWM();

  // Configure rotary encoder
  encoder.setClickHandler(onEncoderClicked);
  encoder.setEncoderHandler(onEncoderRotated);
  encoder.setRateLimit(50);

  // Configure analog input pins
  pinMode(CURRENT_SENSE_PIN, INPUT);
  pinMode(BATTERY_VOLTAGE_PIN, INPUT);

  // Initial display update
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(F("APH Ready"));
  display.display();
  delay(500);

  Serial.println(F("Setup complete"));
}

// ==== Main Loop ====
void loop() {
  unsigned long currentTime = millis();

  // Update encoder state
  encoder.update();

  // Control loop update
  if (currentTime - lastControlUpdate >= CONTROL_LOOP_INTERVAL) {
    lastControlUpdate = currentTime;

    // Read all sensors
    readSensors();

    // Check proximity sensor for toggle
    checkProximitySensor();

    // Update power control
    updatePowerControl();
  }

  // Display update (less frequent to reduce I2C traffic)
  if (currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = currentTime;
    updateDisplay();
  }
}
