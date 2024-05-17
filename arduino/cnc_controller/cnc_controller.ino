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
char cmd_buffer[CMD_BUFFER_SIZE];
int cmd_buffer_index = 0;

void setup() {
  // Serial
  Serial.begin(115200);

  // LEDs
  FastLED.addLeds<WS2812B, PIN_LEDS, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);

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
  Serial.print("Command: ");
  Serial.println(cmd_buffer);


  // Reset the buffer
  cmd_buffer_index = 0;
}


void loop() {
  // Wait for new command
  if (Serial.available() > 0) {
    // read the incoming byte and add it to the current command buffer
    char command = Serial.read();
    if (char == '\n') {
      // This command is ready to go
      processCmd();
    } else {
      if (cmd_buffer_index < CMD_BUFFER_MAX_SIZE) {
        cmd_buffer[cmd_buffer_index] = command;
        cmd_buffer_index++;
      } else {
        // Buffer overflow, reset buffer
        cmd_buffer_index = 0;
      }
    }
  }



  // put your main code here, to run repeatedly:
  Serial.println("Test");

  leds[0] = CRGB::Red;
  leds[1] = CRGB::Green;
  leds[2] = CRGB::Blue;

  FastLED.show();

  digitalWrite(PIN_PUMP_ENA, HIGH);
  digitalWrite(PIN_PUMP_DIR, HIGH);
  

  while(0) {
    digitalWrite(PIN_PUMP_STEP, HIGH);
    digitalWrite(PIN_PUMP_STEP, LOW);
    delay(1);
  }


  // //digitalWrite(PIN_AIR, 1);
  // delay(100);
  // int pressure = analogRead(PIN_PRESSURE);
  // Serial.println(pressure);
  // delay(1000);
  // //digitalWrite(PIN_AIR, 0);
  // delay(500);
  // pressure = analogRead(PIN_PRESSURE);
  // Serial.println(pressure);
  // delay(1000);

  // delay(1000);
  // digitalWrite(PIN_SPINDLE, 1);
  // delay(200);
  // digitalWrite(PIN_LASER, 1);
  // delay(1000);
  // digitalWrite(PIN_SPINDLE, 0);
  // delay(200);
  // digitalWrite(PIN_LASER, 0);

  Serial.print("Door: ");
  Serial.println(digitalRead(PIN_DOOR));
  Serial.print("Head: ");
  Serial.println(digitalRead(PIN_LASER_HEAD));
  Serial.print("Vacuum Force: ");
  Serial.println(digitalRead(PIN_VACUUM_FORCE));

  digitalWrite(PIN_SPINDLE, 1);
  int pwm = analogRead(PIN_PWM);
  Serial.println(pwm);
  delay(500);

  //delay(10000);
}
