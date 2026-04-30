#include <FastLED.h>
#define USE_TIMER_1 true
#include <TimerInterrupt.h>

#include <limits.h>

#define PIN_LEDS 4
#define PIN_SPINDLE A0
#define PIN_LASER A1
#define PIN_AIR 13
#define PIN_PRESSURE A6
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

// Various delays, all in ms
// laser off to air off
#define LASER_OFF_TO_AIR_OFF_MS 5000
// laser off to hood off
#define LASER_OFF_TO_HOOD_OFF_MS 240000
// spindle off to vacuum off
#define SPINDLE_OFF_TO_VACUUM_OFF_MS 10000
// spindle off to mist off
#define SPINDLE_OFF_TO_MIST_OFF_MS 3000

// The LED strip
CRGB leds[NUM_LEDS];

// The buffer that contains the current command
static const int CMD_BUFFER_MAX_SIZE = 64;
String cmd_buffer = "";

// The current interval between pump steps, in ms
int pump_interval_ms = 0;

// The mode in which we operate the machine
typedef enum {
  MODE_IDLE = 0,
  MODE_ROUTER = 1,
  MODE_LASER = 2,
  MODE_MANUAL = 3
} MachineMode;
static MachineMode mode = MODE_IDLE;

// This is the last time at which the mode was set by the computer
static int32_t mode_set_time = LONG_MIN;
// This is the last time at which the laser was on
static int32_t laser_on_time = LONG_MIN;
// This is the last time at which the spindle was on
static int32_t spindle_on_time = LONG_MIN;
// This is the last time at which we ran the main loop
static int32_t last_loop_time = LONG_MIN;

static String debug = "";

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
    Serial.println("mode=" + String(mode));
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
    Serial.println("debug=" + debug);
    return;
  }
  if (cmd_buffer.startsWith("mode=")) {
    // Set mode
    int new_mode;
    sscanf(cmd_buffer.c_str(), "mode=%d", &new_mode);
    if (new_mode == MODE_IDLE || new_mode == MODE_LASER || new_mode == MODE_ROUTER || new_mode == MODE_MANUAL) {
      mode = (MachineMode)new_mode;
      mode_set_time = millis();
      sendDone();
    } else {
      Serial.println("args_error");
    }
    return;
  }
  // If we are in IDLE mode, then all other commands are ignored
  if (mode == MODE_IDLE) {
    Serial.println("unknown");
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
  uint32_t now = millis();

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

  // This tells us when the laser was last on
  bool laser_is_on = digitalRead(PIN_LASER) && analogRead(PIN_PWM);
  if (laser_is_on) {
    laser_on_time = now;
  }
  // This tells us when the spindle was last on
  bool spindle_is_on = digitalRead(PIN_SPINDLE) && analogRead(PIN_PWM);
  if (spindle_is_on) {
    spindle_on_time = now;
  }

  // There are some things that we want to enforce. This is done here:
  // - If we are not in router mode, the spindle should be off
  if (mode != MODE_ROUTER) {
    digitalWrite(PIN_SPINDLE, LOW);
  }
  // - If we are not in laser mode, the laser should be off
  if (mode != MODE_LASER) {
    digitalWrite(PIN_LASER, LOW);
  }
  // - If we are in laser mode,
  if (mode == MODE_LASER) {
    // The laser should be on when the door is closed
    if (digitalRead(PIN_DOOR)) {
      digitalWrite(PIN_LASER, LOW);
    } else {
      digitalWrite(PIN_LASER, HIGH);
    }
    // The air and the hood should be on when the laser is on
    if (laser_is_on) {
      digitalWrite(PIN_AIR, HIGH);
      digitalWrite(PIN_HOOD, HIGH);
    }
    // We can turn off the air and hood some time after the laser turns off
    if ((now - laser_on_time) >= LASER_OFF_TO_AIR_OFF_MS && (last_loop_time - laser_on_time) < LASER_OFF_TO_AIR_OFF_MS) {
      digitalWrite(PIN_AIR, LOW);
    }
    if ((now - laser_on_time) >= LASER_OFF_TO_HOOD_OFF_MS && (last_loop_time - laser_on_time) < LASER_OFF_TO_HOOD_OFF_MS) {
      digitalWrite(PIN_HOOD, LOW);
    }
  }

  last_loop_time = now;
}
