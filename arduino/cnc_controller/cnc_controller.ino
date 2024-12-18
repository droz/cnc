#include <FastLED.h>
// The two following libraries are both called "Timer" but have very different use:
// The first one is TimerInterrupt, which is used to trigger the pump
//  stepper motor from a hardware timer. It does all the wiring to use the
//  interrupt of the timer.
#define USE_TIMER_1 true
#include <TimerInterrupt.h>
// The second one is Arduino-Timer, which is used to delay some actions (like
//  turning off the air after a while). It is a software timer that is not
//  using any hardware interrupt. We tickle it at every loop iteration.
#include <arduino-timer.h>
#include <limits.h>
#include <CRC.h>

// Various delays, all in ms
// laser off to air off
#define LASER_OFF_TO_AIR_OFF_MS 5000
// laser off to hood off
#define LASER_OFF_TO_HOOD_OFF_MS 240000
// spindle off to hood off
#define SPINDLE_OFF_TO_HOOD_OFF_MS 6000
// spindle off to vacuum off
#define SPINDLE_OFF_TO_VACUUM_OFF_MS 10000
// spindle off to mist off
#define SPINDLE_OFF_TO_MIST_OFF_MS 3000
// delay after which we check the air pressure
#define AIR_PRESSURE_CHECK_DELAY_MS 1000

// Minimum air pressure for the laser
#define MIN_AIR_PRESSURE 309
// Maximum air pressure for the laser
#define MAX_AIR_PRESSURE 513

// LED color scheme
#define LED_IDLE_MODE CRGB::Green
#define LED_LASER_MODE CRGB::Red
#define LED_ROUTER_MODE CRGB::Blue
#define LED_MANUAL_MODE CRGB::Yellow

// Pin mapping
#define PIN_LEDS         4
#define PIN_SPINDLE      A0
#define PIN_LASER        A1
#define PIN_AIR          13
#define PIN_PRESSURE     A6
#define PIN_PUMP_ENA     8
#define PIN_PUMP_DIR     7
#define PIN_PUMP_STEP    6
#define PIN_PWM          A2
#define PIN_DOOR         9
#define PIN_LASER_HEAD   12
#define PIN_VACUUM       11
#define PIN_VACUUM_FORCE 2
#define PIN_HOOD         10

// The LEDs
#define NUM_LEDS         3
CRGB leds[NUM_LEDS];

// Last time at which we received a command from the PC
static uint32_t last_cmd_time = 0;
#define CMD_TIMEOUT_MS 1000

// A debug string
static String debug_string = "";

// This struct is used to report status to the computer
struct Status {
  // The current mode
  uint8_t mode;
  // The current submode
  uint8_t submode;
  // The status of the pins
  //  BIT0: door
  //  BIT1: laser_head
  //  BIT2: force_vacuum
  //  BIT3: vacuum
  //  BIT4: hood
  //  BIT5: spindle
  //  BIT6: laser
  //  BIT7: air
  //  BIT8: pump_enable
  //  BIT9-15: reserved for future use
  uint16_t pins;
  // The current measured air pressure (0-1023), max range is 100psi
  uint16_t pressure;
  // The current measured PWM (0-1023), max range is 100%
  uint16_t pwm;
  // The current pump interval, in ms
  uint16_t pump_interval_ms;
  // TODO(report LEDs)
  // The current error code
  uint16_t error;
  // The length of the debug string
  uint8_t debug_length;
};

// This timer is used to delay some actions (like turning off the air after a while)
Timer<16, millis> timer;
Timer<16, millis>::Task air_task;
Timer<16, millis>::Task hood_task;
Timer<16, millis>::Task vacuum_task;

// This class is used to track the status and history of some variables
class OnOffVariable {
 public:
  OnOffVariable(const String& name) {
    name_ = name;
    last_state_ = false;
    last_off_on_time_ = LONG_MIN;
    last_on_off_time_ = LONG_MIN;
  }

  // Update the state of the variable. Should be called at every loop iteration
  void update(bool new_state) {
    uint32_t now = millis();
    last_state_ = state_;
    if (new_state != state_) {
      if (new_state) {
        last_off_on_time_ = now;
      } else {
        last_on_off_time_ = now;
      }
      state_ = new_state;
    }
  }

  // Check if the variable has been on for a certain duration
  bool hasBeenOnFor(uint32_t duration_ms) {
    if(!state_) {
      return false;
    }
    if (last_off_on_time_ == LONG_MIN) {
      return true;
    }
    return (millis() - last_off_on_time_) >= duration_ms;
  }

  // Check if the variable has been off for a certain duration
  bool hasBeenOffFor(uint32_t duration_ms) {
    if(state_) {
      return false;
    }
    if (last_on_off_time_ == LONG_MIN) {
      return true;
    }
    return (millis() - last_on_off_time_) >= duration_ms;
  }

  // Return the current state of the variable
  bool isOn() {
    return state_;
  }

  // Return true if the variable has just been turned on (in the last cycle)
  bool justTurnedOn() {
    return state_ && !last_state_;
  }

  // Return true if the variable has just been turned off (in the last cycle)
  bool justTurnedOff() {
    return !state_ && last_state_;
  }

 private:
  String name_;
  int32_t last_off_on_time_;
  int32_t last_on_off_time_;
  bool state_;
  bool last_state_;
};

// Laser status
static OnOffVariable laser_status("laser");
// Spindle status
static OnOffVariable spindle_status("spindle");
// Air status
static OnOffVariable air_status("air");

// The buffer that contains the current command
static const int CMD_BUFFER_MAX_SIZE = 64;
String cmd_buffer = "";

// The current interval between pump steps, in ms
uint16_t pump_interval_ms = 0;

// The mode in which we operate the machine
typedef enum {
  // IDLE: Nothing should happen
  MODE_IDLE = 0,
  // Router: Laser cannot be used, spindle can be used
  MODE_ROUTER = 1,
  // Laser: Laser can be used, spindle cannot be used
  MODE_LASER = 2,
  // Manual: Everything can be used
  MODE_MANUAL = 3
} Mode;
static Mode mode = MODE_IDLE;

// The submode in which we operate the machine
typedef enum {
  // NOTHING: Nothing should happen
  SUBMODE_NOTHING = 0,
  // AIR: Air is on
  SUBMODE_AIR = 1,
  // PUMP: Pump is on
  SUBMODE_PUMP = 2,
  // VACUUUM: Vacuum is on
  SUBMODE_VACUUM = 4
} Submode;
static uint8_t submode = SUBMODE_NOTHING;

// The error codes
typedef enum {
  ERROR_NONE               = 0,
  ERROR_LOW_AIR_PRESSURE   = 1,
  ERROR_HIGH_AIR_PRESSURE  = 2,
  ERROR_LASER_HEAD_MISSING = 4,
  ERROR_LASER_HEAD_PRESENT = 8,
  ERROR_DOOR_OPEN          = 16,
} MachineError;
static uint16_t error = ERROR_NONE;


void setup() {
  // Serial
  Serial.begin(115200);

  // LEDs
  FastLED.addLeds<WS2812B, PIN_LEDS, RGB>(leds, NUM_LEDS);
  FastLED.setBrightness(255);
  leds[0] = CRGB::Black;
  leds[1] = CRGB::Black;
  leds[2] = CRGB::Black;
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

// One step of the pump stepper motor
void pumpStep() {
  digitalWrite(PIN_PUMP_STEP, HIGH);
  digitalWrite(PIN_PUMP_STEP, LOW);
}

// Update the pump speed
void updatePumpSpeed(int pump_interval_ms) {
    if (pump_interval_ms == 0) {
      ITimer1.stopTimer();
    } else {
      ITimer1.setInterval(pump_interval_ms, pumpStep);
    }
}

// Send "done" to the computer
void sendDone() {
  Serial.println("done");
}

// Process the command in the serial buffer
bool processCmd() {
  if (cmd_buffer.startsWith("bstatus")) {
    // Binary status
    Status status;
    status.mode = mode;
    status.submode = submode;
    status.pins = 0;
    status.pins |= !digitalRead(PIN_DOOR) << 0;
    status.pins |= !digitalRead(PIN_LASER_HEAD) << 1;
    status.pins |= !digitalRead(PIN_VACUUM_FORCE) << 2;
    status.pins |= digitalRead(PIN_VACUUM) << 3;
    status.pins |= digitalRead(PIN_HOOD) << 4;
    status.pins |= digitalRead(PIN_SPINDLE) << 5;
    status.pins |= digitalRead(PIN_LASER) << 6;
    status.pins |= digitalRead(PIN_AIR) << 7;
    status.pins |= !digitalRead(PIN_PUMP_ENA) << 8;
    status.pressure = analogRead(PIN_PRESSURE);
    status.pwm = analogRead(PIN_PWM);
    status.pump_interval_ms = pump_interval_ms;
    status.error = error;
    status.debug_length = debug_string.length();
    uint8_t size = sizeof(status);
    uint8_t crc = calcCRC8((uint8_t*) &status, sizeof(status));
    Serial.write((uint8_t*)&size, sizeof(size));
    Serial.write((uint8_t*)&status, sizeof(status));
    Serial.write((uint8_t*)&crc, sizeof(crc));
    return true;
  }

  if (cmd_buffer.startsWith("debug")) {
    Serial.println(debug_string);
    debug_string = "";
    return true;
  }

  if (cmd_buffer.startsWith("status")) {
    // Send status
    Serial.println("mode=" + String(mode));
    Serial.println("submode=" + String(submode));
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
    Serial.println("pump_enable=" + String(!digitalRead(PIN_PUMP_ENA)));
    Serial.println("led0=" + String(leds[0].r) + "," + String(leds[0].g) + "," + String(leds[0].b));
    Serial.println("led1=" + String(leds[1].r) + "," + String(leds[1].g) + "," + String(leds[1].b));
    Serial.println("led2=" + String(leds[2].r) + "," + String(leds[2].g) + "," + String(leds[2].b));
    Serial.println("error=" + String(error));
    Serial.println("debug_length=" + String(debug_string.length()));
    return true;
  }

  if (cmd_buffer.startsWith("mode=")) {
    // Set mode
    int new_mode;
    sscanf(cmd_buffer.c_str(), "mode=%d", &new_mode);
    if (new_mode == MODE_IDLE || new_mode == MODE_LASER || new_mode == MODE_ROUTER || new_mode == MODE_MANUAL) {
      mode = (Mode)new_mode;
      sendDone();
    } else {
      Serial.println("args_error");
    }
    return true;
  }
  // If we are in IDLE mode, then all other commands are ignored
  if (mode == MODE_IDLE) {
    Serial.println("unknown");
    return true;
  }
  if (cmd_buffer.startsWith("submode=")) {
    // Set submode
    int new_submode;
    sscanf(cmd_buffer.c_str(), "submode=%d", &new_submode);
    submode = new_submode;
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("led0=")) {
    // Set LED 0
    int r, g, b;
    sscanf(cmd_buffer.c_str(), "led0=%d,%d,%d", &r, &g, &b);
    leds[0] = CRGB(r, g, b);
    FastLED.show();
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("led1=")) {
    // Set LED 1
    int r, g, b;
    sscanf(cmd_buffer.c_str(), "led1=%d,%d,%d", &r, &g, &b);
    leds[1] = CRGB(r, g, b);
    FastLED.show();
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("led2=")) {
    // Set LED 2
    int r, g, b;
    sscanf(cmd_buffer.c_str(), "led2=%d,%d,%d", &r, &g, &b);
    leds[2] = CRGB(r, g, b);
    FastLED.show();
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("spindle=")) {
    // Set Spindle
    int state;
    sscanf(cmd_buffer.c_str(), "spindle=%d", &state);
    digitalWrite(PIN_SPINDLE, state);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("laser=")) {
    // Set Laser
    int state;
    sscanf(cmd_buffer.c_str(), "laser=%d", &state);
    // We can turn the laser on only if we are free of errors
    if (error != ERROR_NONE) {
      state = 0;
    }
    digitalWrite(PIN_LASER, state);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("air=")) {
    // Set Air
    int state;
    sscanf(cmd_buffer.c_str(), "air=%d", &state);
    digitalWrite(PIN_AIR, state);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("vacuum=")) {
    // Set Vacuum
    int state;
    sscanf(cmd_buffer.c_str(), "vacuum=%d", &state);
    digitalWrite(PIN_VACUUM, state);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("hood=")) {
    // Set Hood
    int state;
    sscanf(cmd_buffer.c_str(), "hood=%d", &state);
    digitalWrite(PIN_HOOD, state);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("pump_interval_ms=")) {
    // Set Pump Speed
    sscanf(cmd_buffer.c_str(), "pump_interval_ms=%d", &pump_interval_ms);
    if (pump_interval_ms < 0 || pump_interval_ms > 1000) {
      Serial.println("args_error");
      return false;
    }
    updatePumpSpeed(pump_interval_ms);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("pump_enable=")) {
    // Set Pump enable
    int state;
    sscanf(cmd_buffer.c_str(), "pump_enable=%d", &state);
    digitalWrite(PIN_PUMP_ENA, !state);
    sendDone();
    return true;
  }
  if (cmd_buffer.startsWith("error=")) {
    // Set error
    int new_error;
    sscanf(cmd_buffer.c_str(), "error=%d", &new_error);
    error = new_error;
    sendDone();
    return true;
  }
  // Unknown command
  Serial.println("unknown");

  return false;
}

void updateLed(uint8_t led, CRGB color) {
  if (leds[led] == color) {
    return;
  }
  leds[led] = color;
  FastLED.show();
}

bool turnAirOff(void*) {
  digitalWrite(PIN_AIR, LOW);
  digitalWrite(PIN_PUMP_ENA, HIGH);
  debug_string = "Air turned off after delay";
  return true;
}

bool turnHoodOff(void*) {
  digitalWrite(PIN_HOOD, LOW);
  debug_string = "Hood turned off after delay";
  return true;
}

bool turnVacuumOff(void*) {
  digitalWrite(PIN_VACUUM, LOW);
  debug_string = "Vacuum turned off after delay";
  return true;
}

void loop() {
  // Wait for new command
  if (Serial.available() > 0) {
    // read the incoming byte and add it to the current command buffer
    char c = Serial.read();
    if (c == '\n') {
      // This command is ready to go
      if (processCmd()) {
        last_cmd_time = millis();
      }
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

  // If we have not received a command for a while, we go to IDLE mode
  if (mode != MODE_IDLE && millis() - last_cmd_time > CMD_TIMEOUT_MS) {
    debug_string = "mode timeout after " + String(millis() - last_cmd_time) + "ms";
    mode = MODE_IDLE;
  }

  // Update LED2 according to the mode
  switch (mode) {
    case MODE_IDLE:
      updateLed(2, LED_IDLE_MODE);
      break;
    case MODE_LASER:
      updateLed(2, LED_LASER_MODE);
      break;
    case MODE_ROUTER:
      updateLed(2, LED_ROUTER_MODE);
      break;
    case MODE_MANUAL:
      updateLed(2, LED_MANUAL_MODE);
      break;
    default:
      updateLed(2, CRGB::Black);
      break;
  }

  // Update LED0 according to the error
  if (error != ERROR_NONE) {
    updateLed(0, CRGB::Red);
  } else {
    updateLed(0, CRGB::Black);
  }

  // Tickle the timer
  timer.tick();

  // Update some variables
  laser_status.update(digitalRead(PIN_LASER) && analogRead(PIN_PWM));
  spindle_status.update(digitalRead(PIN_SPINDLE) && analogRead(PIN_PWM));
  air_status.update(digitalRead(PIN_AIR));

  // We can clear some errors when the conditions are met
  if (digitalRead(PIN_DOOR)) {
    error &= ~ERROR_DOOR_OPEN;
  }
  if (!digitalRead(PIN_LASER_HEAD)) {
    error &= ~ERROR_LASER_HEAD_PRESENT;
  }

  // If we are in manual mode, we can skip all the following checks and automation
  if (mode == MODE_MANUAL) {
    return;
  }

  // If we are in IDLE mode, everything should be off and we can also skip all the following checks
  if (mode == MODE_IDLE) {
    digitalWrite(PIN_SPINDLE, LOW);
    digitalWrite(PIN_LASER, LOW);
    digitalWrite(PIN_AIR, LOW);
    digitalWrite(PIN_VACUUM, LOW);
    digitalWrite(PIN_HOOD, LOW);
    updatePumpSpeed(0);
    ITimer1.stopTimer();
    return;
  }

  // - If we are in laser mode,
  if (mode == MODE_LASER) {
    // The spindle should be OFF
    digitalWrite(PIN_SPINDLE, LOW);

    // The pump should be OFF
    digitalWrite(PIN_PUMP_ENA, HIGH);

    // The hood and air should be ON when the laser is on
    if (laser_status.isOn()) {
      digitalWrite(PIN_HOOD, HIGH);
      digitalWrite(PIN_AIR, HIGH);
    }

    // The door should be closed if the laser is on
    if (laser_status.isOn() && digitalRead(PIN_DOOR)) {
      error |= ERROR_DOOR_OPEN;
    }
    // The laser head should be present
    if (digitalRead(PIN_LASER_HEAD)) {
      error |= ERROR_LASER_HEAD_MISSING;
    }

    // We can turn the laser on only if we are free of errors
    if (error == ERROR_NONE) {
      digitalWrite(PIN_LASER, HIGH);
    } else {
      digitalWrite(PIN_LASER, LOW);
    }
  }

  // - If we are in router mode,
  if (mode == MODE_ROUTER) {
    // The laser should be OFF
    digitalWrite(PIN_LASER, LOW);

    // The pump should be ON when the spindle is on and the user requested it
    // We also turn the hood and the air on
    if (spindle_status.isOn() && (submode & SUBMODE_PUMP)) {
      digitalWrite(PIN_PUMP_ENA, LOW);
      digitalWrite(PIN_HOOD, HIGH);
      digitalWrite(PIN_AIR, HIGH);
    }

    // The vacuum should be ON when the spindle is on and the user requested it
    if (spindle_status.isOn() && (submode & SUBMODE_VACUUM)) {
      digitalWrite(PIN_VACUUM, HIGH);
    }

    // The air should be ON when the spindle is on and the user requested it
    if (spindle_status.isOn() && (submode & SUBMODE_AIR)) {
      digitalWrite(PIN_AIR, HIGH);
    }

    // We don't care about the door
    // The laser head should not be present
    if (!digitalRead(PIN_LASER_HEAD)) {
      error |= ERROR_LASER_HEAD_PRESENT;
    }
    // We can turn the spindle on only if we are free of errors
    if (error == ERROR_NONE) {
      digitalWrite(PIN_SPINDLE, HIGH);
    } else {
      digitalWrite(PIN_SPINDLE, LOW);
    }
  }

  // Check that the air pressure is correct when the air is on
  if (air_status.hasBeenOnFor(AIR_PRESSURE_CHECK_DELAY_MS)) {
    if (analogRead(PIN_PRESSURE) < MIN_AIR_PRESSURE) {
      error = ERROR_LOW_AIR_PRESSURE;
    }
    if (analogRead(PIN_PRESSURE) > MAX_AIR_PRESSURE) {
      error = ERROR_HIGH_AIR_PRESSURE;
    }
  }

  // Set the hood, air and pump to turn off automatically after a while
  if (laser_status.justTurnedOff()) {
    debug_string = "Laser turned off";
    air_task = timer.in(LASER_OFF_TO_AIR_OFF_MS, turnAirOff);
    hood_task = timer.in(LASER_OFF_TO_HOOD_OFF_MS, turnHoodOff);
  }
  if (spindle_status.justTurnedOff()) {
    debug_string = "Spindle turned off";
    air_task = timer.in(SPINDLE_OFF_TO_MIST_OFF_MS, turnAirOff);
    hood_task = timer.in(SPINDLE_OFF_TO_HOOD_OFF_MS, turnHoodOff);
    vacuum_task = timer.in(SPINDLE_OFF_TO_VACUUM_OFF_MS, turnVacuumOff);
  }
  if (laser_status.justTurnedOn()) {
    debug_string = "Laser turned on";
    timer.cancel(air_task);
    timer.cancel(hood_task);

  }
  if (spindle_status.justTurnedOn()) {
    debug_string = "Spindle turned on";
    timer.cancel(air_task);
    timer.cancel(hood_task);
    timer.cancel(vacuum_task);
  }


}
