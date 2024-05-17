#include <FastLED.h>

#define PIN_LEDS 4
#define PIN_SPINDLE A0
#define PIN_LASER A1
#define PIN_AIR 13
#define PIN_PRESSURE A7
#define PIN_PUMP_ENA 8
#define PIN_PUMP_DIR 7
#define PIN_PUMP_STEP 6
#define PIN_PWM A2
#define PIN_DOOR 9
#define PIN_LASER_HEAD 12
#define PIN_VACUUM 11
#define PIN_VACUUM_FORCE 2
#define PIN_HOOD 10

#define NUM_LEDS 3

CRGB leds[NUM_LEDS];

static const int CMD_BUFFER_MAX_SIZE = 64;
String cmd_buffer = "";

// The current speed of the pump
int pump_speed = 0;

void setup() {
  // Serial
  Serial.begin(115200);

  // LEDs
  FastLED.addLeds<WS2812B, PIN_LEDS, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  leds[0] = CRGB::Red;
  leds[1] = CRGB::Green;
  leds[2] = CRGB::Blue;
  FastLED.show();

  // Spindle
  pinMode(PIN_SPINDLE, OUTPUT);
  digitalWrite(PIN_SPINDLE, LOW);

  // Laser
  pinMode(PIN_LASER, OUTPUT);
  digitalWrite(PIN_LASER, LOW);

  // Air Solenoid
  pinMode(PIN_AIR, OUTPUT);
  digitalWrite(PIN_AIR, LOW);

  // Mist Pump Stepper
  pinMode(PIN_PUMP_ENA, OUTPUT);
  digitalWrite(PIN_PUMP_ENA, HIGH);
  pinMode(PIN_PUMP_DIR, OUTPUT);
  digitalWrite(PIN_PUMP_DIR, LOW);
  pinMode(PIN_PUMP_STEP, OUTPUT);
  digitalWrite(PIN_PUMP_STEP, LOW);

  // Switches
  pinMode(PIN_DOOR, INPUT_PULLUP);
  pinMode(PIN_LASER_HEAD, INPUT_PULLUP);
  pinMode(PIN_VACUUM_FORCE, INPUT_PULLUP);

  // Vacuum and Hood
  pinMode(PIN_VACUUM, OUTPUT);
  digitalWrite(PIN_VACUUM, LOW);
  pinMode(PIN_HOOD, OUTPUT);
  digitalWrite(PIN_HOOD, LOW);

}

void processCmd() {
  // Process the command in the buffer
  if (cmd_buffer.startsWith("status")) {
    // Send status
    Serial.println("door=" + String(!digitalRead(PIN_DOOR)));
    Serial.println("head=" + String(!digitalRead(PIN_LASER_HEAD)));
    Serial.println("vacuum_force=" + String(!digitalRead(PIN_VACUUM_FORCE)));
    Serial.println("vacuum=" + String(digitalRead(PIN_VACUUM)));
    Serial.println("hood=" + String(digitalRead(PIN_HOOD)));
    Serial.println("pressure=" + String(analogRead(PIN_PRESSURE)));
    Serial.println("pwm=" + String(analogRead(PIN_PWM)));
    Serial.println("spindle=" + String(digitalRead(PIN_SPINDLE)));
    Serial.println("laser=" + String(digitalRead(PIN_LASER)));
    Serial.println("air=" + String(digitalRead(PIN_AIR)));
    Serial.println("pump_speed=" + String(pump_speed));
    Serial.println("led0=" + String(leds[0].r) + "," + String(leds[0].g) + "," + String(leds[0].b));
    Serial.println("led1=" + String(leds[1].r) + "," + String(leds[1].g) + "," + String(leds[1].b));
    Serial.println("led2=" + String(leds[2].r) + "," + String(leds[2].g) + "," + String(leds[2].b));
    return;
  }
  if (cmd_buffer.startsWith("led0=")) {
    // Set LED 0
    int r, g, b;
    sscanf(cmd_buffer.c_str(), "led0=%d,%d,%d", &r, &g, &b);
    leds[0] = CRGB(r, g, b);
    FastLED.show();
    return;
  }
  Serial.print("Command: ");
  Serial.println(cmd_buffer);



}


void loop() {
  // Wait for new command
  if (Serial.available() > 0) {
    // read the incoming byte and add it to the current command buffer
    char c = Serial.read();
    if (c == '\n') {
      // This command is ready to go
      processCmd();
      cmd_buffer = "";
    } else {
      // Add the character to the buffer
      if (cmd_buffer.length() < CMD_BUFFER_MAX_SIZE) {
        cmd_buffer += c;
      } else {
        // Buffer overflow, reset buffer
        cmd_buffer = "";
      }
    }
  }



//  // put your main code here, to run repeatedly:
//  Serial.println("Test");
//
//  leds[0] = CRGB::Red;
//  leds[1] = CRGB::Green;
//  leds[2] = CRGB::Blue;
//
//  FastLED.show();
//
//  digitalWrite(PIN_PUMP_ENA, HIGH);
//  digitalWrite(PIN_PUMP_DIR, HIGH);
//  
//
//  while(0) {
//    digitalWrite(PIN_PUMP_STEP, HIGH);
//    digitalWrite(PIN_PUMP_STEP, LOW);
//    delay(1);
//  }
//
//
//  // //digitalWrite(PIN_AIR, 1);
//  // delay(100);
//  // int pressure = analogRead(PIN_PRESSURE);
//  // Serial.println(pressure);
//  // delay(1000);
//  // //digitalWrite(PIN_AIR, 0);
//  // delay(500);
//  // pressure = analogRead(PIN_PRESSURE);
//  // Serial.println(pressure);
//  // delay(1000);
//
//  // delay(1000);
//  // digitalWrite(PIN_SPINDLE, 1);
//  // delay(200);
//  // digitalWrite(PIN_LASER, 1);
//  // delay(1000);
//  // digitalWrite(PIN_SPINDLE, 0);
//  // delay(200);
//  // digitalWrite(PIN_LASER, 0);
//
//  Serial.print("Door: ");
//  Serial.println(digitalRead(PIN_DOOR));
//  Serial.print("Head: ");
//  Serial.println(digitalRead(PIN_LASER_HEAD));
//  Serial.print("Vacuum Force: ");
//  Serial.println(digitalRead(PIN_VACUUM_FORCE));
//
//  digitalWrite(PIN_SPINDLE, 1);
//  int pwm = analogRead(PIN_PWM);
//  Serial.println(pwm);
//  delay(500);
//
//  //delay(10000);
}
