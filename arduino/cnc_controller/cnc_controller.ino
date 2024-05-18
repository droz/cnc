#include <FastLED.h>
#define USE_TIMER_1 true
#include <TimerInterrupt.h>

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

// The current interval between pump steps, in ms
int pump_interval_ms = 0;

// The timer used to step the pump motor
//TimerInterrupt timer1(2);

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
  digitalWrite(PIN_PUMP_DIR, HIGH);
  pinMode(PIN_PUMP_STEP, OUTPUT);
  digitalWrite(PIN_PUMP_STEP, LOW);
  // Timer to trigger pump steps
  ITimer1.init();

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

void pumpStep() {
  digitalWrite(PIN_PUMP_STEP, HIGH);
  digitalWrite(PIN_PUMP_STEP, LOW);
}

void sendDone() {
  Serial.println("done");
}

void processCmd() {
  // Process the command in the buffer
  if (cmd_buffer.startsWith("status")) {
    // Send status
    Serial.println("door=" + String(!digitalRead(PIN_DOOR)));
    Serial.println("laser_head=" + String(!digitalRead(PIN_LASER_HEAD)));
    Serial.println("force_vacuum=" + String(!digitalRead(PIN_VACUUM_FORCE)));
    Serial.println("vacuum=" + String(digitalRead(PIN_VACUUM)));
    Serial.println("hood=" + String(digitalRead(PIN_HOOD)));
    Serial.println("pressure=" + String(analogRead(PIN_PRESSURE)));
    Serial.println("pwm=" + String(analogRead(PIN_PWM)));
    Serial.println("spindle=" + String(digitalRead(PIN_SPINDLE)));
    Serial.println("laser=" + String(digitalRead(PIN_LASER)));
    Serial.println("air=" + String(digitalRead(PIN_AIR)));
    Serial.println("pump_interval_ms=" + String(pump_interval_ms));
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
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("led1=")) {
    // Set LED 1
    int r, g, b;
    sscanf(cmd_buffer.c_str(), "led1=%d,%d,%d", &r, &g, &b);
    leds[1] = CRGB(r, g, b);
    FastLED.show();
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("led2=")) {
    // Set LED 2
    int r, g, b;
    sscanf(cmd_buffer.c_str(), "led2=%d,%d,%d", &r, &g, &b);
    leds[2] = CRGB(r, g, b);
    FastLED.show();
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("spindle=")) {
    // Set Spindle
    int state;
    sscanf(cmd_buffer.c_str(), "spindle=%d", &state);
    digitalWrite(PIN_SPINDLE, state);
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("laser=")) {
    // Set Laser
    int state;
    sscanf(cmd_buffer.c_str(), "laser=%d", &state);
    digitalWrite(PIN_LASER, state);
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("air=")) {
    // Set Air
    int state;
    sscanf(cmd_buffer.c_str(), "air=%d", &state);
    digitalWrite(PIN_AIR, state);
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("vacuum=")) {
    // Set Vacuum
    int state;
    sscanf(cmd_buffer.c_str(), "vacuum=%d", &state);
    digitalWrite(PIN_VACUUM, state);
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("hood=")) {
    // Set Hood
    int state;
    sscanf(cmd_buffer.c_str(), "hood=%d", &state);
    digitalWrite(PIN_HOOD, state);
    sendDone();
    return;
  }
  if (cmd_buffer.startsWith("pump_interval_ms=")) {
    // Set Pump Speed
    sscanf(cmd_buffer.c_str(), "pump_interval_ms=%d", &pump_interval_ms);
    if (pump_interval_ms == 0) {
      digitalWrite(PIN_PUMP_ENA, HIGH);
      ITimer1.stopTimer();
      sendDone();
      return;
    }
    if (pump_interval_ms < 0 || pump_interval_ms > 1000) {
      digitalWrite(PIN_PUMP_ENA, HIGH);
      Serial.println("args_error");
    }
    digitalWrite(PIN_PUMP_ENA, LOW);
    ITimer1.setInterval(pump_interval_ms, pumpStep);
    sendDone();
    return;
  }
  // Unknown command
  Serial.println("unknown");
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
  FastLED.show();
}
