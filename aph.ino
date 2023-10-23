#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EncoderButton.h>
#include <MAX471.h>
#include <SPI.h>
#include <Wire.h>

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 32  // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// The pins for I2C are defined by the Wire-library.
// On an arduino UNO:       A4(SDA), A5(SCL)
// On an arduino MEGA 2560: 20(SDA), 21(SCL)
// On an arduino LEONARDO:   2(SDA),  3(SCL), ...
#define OLED_RESET -1  // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS \
  0x3C  ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

EncoderButton eb1(2, 3, 4);

// 4. 12 bit ADc and 3.3 V
MAX471 myMax471(ADC_12_bit, VCC_3_3, AT_PIN, VT_PIN);

void onEb1Clicked(EncoderButton& eb) {
  Serial.print("eb1 clickCount: ");
  Serial.println(eb.clickCount());
}

void onEb1Encoder(EncoderButton& eb) {
  myValue += eb.increment() * abs(eb.increment());
  Serial.print("eb1 incremented by: ");
  Serial.print(eb.increment());
  Serial.print(" accelerated to: ");
  Serial.print(eb.increment() * abs(eb.increment()));
  Serial.print(" myValue is now: ");
  Serial.println(myValue);
}

void setup() {
  Serial.begin(9600);

  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  // put your setup code here, to run once:
  b1.setClickHandler(onEb1Clicked);

  // Link the event(s) to your function
  eb1.setEncoderHandler(onEb1Encoder);
  // Set the rate limit on your encoder.
  eb1.setRateLimit(200);

  Serial.print("mA: ");
  Serial.println(myMax471.CurrentRead() * 1000, 1);
  Serial.print("mV: ");
  Serial.println(myMax471.VoltageRead() * 1000, 1);
  Serial.println();
  // If set to 200 milliseconds, the increment() will be > 1 if the encoder
  // rotation clicks more than 5 time/second
}

void loop() { eb1.update(); }
