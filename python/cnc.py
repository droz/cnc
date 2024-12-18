""" This code is used to run a homebrewed CNC and laser cutter machine.
It connects to a GRBL controller and an Arduino board through USB.
Depending if the laser head is connected, it will either run the CNC
program (Shapeoko) or the laser program (Lightburn)."""

import logging as log
import tkinter as tk
import tkinter.messagebox as tkmessagebox
import tkdial as tkdial
import argparse
import time
import serial
import sys
import os
import re
import enum
import psutil
import subprocess
import struct
import crc8

def resource_path(relative_path):
    """ Get absolute path to resource """
    try:
        # PyInstaller creates a temp folder and stores path in _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, 'images', relative_path)


class OnOffToggle:
    """ This is used to implement a simple ON/OFF toggle button."""
    def __init__(self, window, text, row, callback, read_only=False):
        self.window = window
        self.state = False
        self.callback = callback
        self.read_only = read_only
        if self.read_only:
            self.on = tk.PhotoImage(file = resource_path("on_ro.png"))
            self.off = tk.PhotoImage(file = resource_path("off_ro.png"))
        else:
            self.on = tk.PhotoImage(file = resource_path("on.png"))
            self.off = tk.PhotoImage(file = resource_path("off.png"))
        self.button = tk.Button(window, image=self.off, command=self.switch, bd=0)
        self.button.grid(column=1, row=row, padx=10, pady=5)
        self.text = tk.Label(window, text=text, font=("Arial", 18))
        self.text.grid(column=0, row=row, padx=10, pady=5)

    def update(self, value):
        self.state = value
        if self.state:
            self.button.config(image = self.on)
        else:
            self.button.config(image = self.off)

    def switch(self):
        if self.read_only:
            return
        self.state = not self.state
        self.update(self.state)
        self.callback(self)


class ErrorText:
    """ This is used to implement a simple Error text indicator."""
    def __init__(self, window, title, row):
        self.window = window
        self.frame = tk.LabelFrame(self.window, text="Error")
        self.frame.grid(column=0, row=row, sticky=tk.W+tk.E, padx=10, pady=10)
        self.text = tk.Label(self.frame, text="", font=("Arial", 18, "bold"), fg="red")
        self.text.grid(column=0, row=0, padx=10, pady=5)

    def update(self, value):
        str = ""
        if value & 1:
            str += "Air pressure too low\n"
        if value & 2:
            str += "Air pressure too high\n"
        if value & 4:
            str += "Laser head missing\n"
        if value & 8:
            str += "Laser head present\n"
        if value & 16:
            str += "Door open\n"
        if str:
            str = str[:-1]
        self.text.config(text=str)

class MultiChoice:
    """ This is used to implement a simple multi-choice button."""
    def __init__(self, window, text, row, choices, callback):
        self.window = window
        self.value = tk.StringVar(window, choices[0])
        self.callback = callback
        self.choices = choices
        self.options = tk.OptionMenu(window, self.value, *choices, command=self.changed)
        self.options.grid(column=1, row=row, padx=10, pady=5)
        self.text = tk.Label(window, text=text, font=("Arial", 18))
        self.text.grid(column=0, row=row, padx=10, pady=5)

    def update(self, value):
        self.value.set(self.choices[value])

    def changed(self, value):
        self.callback(self)

class Slider:
    """ This is used to implement a simple slider."""
    def __init__(self, window, text, row, min, max, callback):
        self.window = window
        self.value = tk.DoubleVar(window, 0)
        self.callback = callback
        self.slider = tk.Scale(window, from_=min, to=max, orient=tk.HORIZONTAL, variable=self.value, command=self.changed, resolution=(max-min)/100)
        self.slider.grid(column=1, row=row, padx=10, pady=5)
        self.text = tk.Label(window, text=text, font=("Arial", 18))
        self.text.grid(column=0, row=row, padx=10, pady=5)

    def update(self, value):
        self.value.set(value)

    def changed(self, value):
        self.callback(self)

class Gauge:
    """ This is used to implement a simple linear dial indicator."""
    def __init__(self, window, text, row, column, min, max, nominal):
        self.window = window
        self.frame = tk.Frame(window)
        self.frame.grid(column=column, row=row, padx=10, pady=5)
        self.text = tk.Label(self.frame, text=text, font=("Arial", 18), anchor="center")
        self.text.grid(column=0, row=0)
        self.value = tk.DoubleVar(self.frame, 0)
        self.meter = tkdial.Meter(self.frame, start=min, end=max, radius = 150, width = 152, height = 152)
        if nominal:
            self.meter.set_mark(nominal - 10, nominal + 10, "green")
        self.meter.grid(column=0, row=1)

    def update(self, value):
        self.meter.set(value)

class GrblInterface:
    """ This class is used to interface with the GRBL controller."""
    def __init__(self, port):
        log.info(f"Connecting to GRBL controller on port {port}")
        self.serial = serial.Serial(port, 115200, timeout=60)
        self.wakeUp()

    def wakeUp(self):
        """ This function is used to wake up the GRBL controller."""
        self.serial.write(b"\r\n\r\n")
        time.sleep(2)
        self.serial.read_all()
        self.serial.flushInput()

    def close(self):
        """ This function is used to close the connection to the GRBL controller."""
        self.serial.close()

    def readSettings(self):
        """ This function is used to read the settings of the GRBL controller.
        Returns:
            A dictionary of values indexed by integer key."""
        self.serial.write(b"$$\n")
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        settings = {}
        for line in response.split("\n"):
            result = re.match(r"\$(\d+)=(.*)", line)
            if result:
                key = int(result.group(1))
                value = result.group(2)
                settings[key] = value
        return settings

    def sendCommand(self, command):
        """ This function is used to send a command to the GRBL controller.
        Args:
            command: The string command to send."""
        self.serial.write(f"{command}\n".encode('utf-8'))
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        if response != 'ok\n':
            raise Exception(f"Error sending command {command}")

    def writeSettings(self, key, value):
        """ This function is used to write a setting to the GRBL controller.
        Args:
            key: The integer key of the setting.
            value: The value of the setting."""
        self.sendCommand(f"${key}={value}")

    def home(self):
        """ This function is used to home the GRBL controller."""
        log.info("Homing the machine...")
        # Send the homing command
        self.serial.write("$H\n".encode('utf-8'))
        # Wait for the OK to come back
        now = time.time()
        while self.serial.in_waiting == 0:
            if time.time() - now > 40:
                raise Exception("Timeout waiting for Homing")
            time.sleep(0.1)
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        if response != 'ok\n':
            raise Exception(f"Error waiting for home")
        log.info("  Machine homed")

class ArduinoInterface:
    """ This class is used to interface with the Arduino board."""
    def __init__(self, port):
        log.info(f"Connecting to Arduino board on port {port}")
        self.serial = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)
        log.info(f"  Connected")

    def readAsciiStatus(self):
        """ This function is used to read the status of the Arduino board,
            using a human readable ASCII scheme.
        Returns:
            A dictionary of values indexed by string key."""
        self.serial.write(b"status\n")
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        status = {}
        for line in response.split("\n"):
            result = re.match(r"(.*)=(.*)", line)
            if result:
                key = result.group(1)
                value = result.group(2)
                status[key] = value
        # Type conversions where needed
        if "mode" in status:
            status["mode"] = int(status["mode"])
        if "submode" in status:
            status["submode"] = int(status["submode"])
        if "door" in status:
            status["door"] = bool(int(status["door"]))
        if "laser_head" in status:
            status["laser_head"] = bool(int(status["laser_head"]))
        if "force_vacuum" in status:
            status["force_vacuum"] = bool(int(status["force_vacuum"]))
        if "vacuum" in status:
            status["vacuum"] = bool(int(status["vacuum"]))
        if "hood" in status:
            status["hood"] = bool(int(status["hood"]))
        if "spindle" in status:
            status["spindle"] = bool(int(status["spindle"]))
        if "laser" in status:
            status["laser"] = bool(int(status["laser"]))
        if "air" in status:
            status["air"] = bool(int(status["air"]))
        if "pump_enable" in status:
            status["pump_enable"] = bool(int(status["pump_enable"]))
        if "pressure" in status:
            status["pressure"] = int(status["pressure"])
        if "pwm" in status:
            status["pwm"] = int(status["pwm"])
        if "pump_interval_ms" in status:
            status["pump_interval_ms"] = int(status["pump_interval_ms"])
        if "error" in status:
            status["error"] = int(status["error"])
        if "debug_length" in status:
            status["debug_length"] = int(status["debug_length"])

        return status

    def readBinaryStatus(self):
        """ This function is used to read the status of the Arduino board,
            using a binary scheme.
        Returns:
            A dictionary of values indexed by string key."""
        format = "<BBHHHHHB"        
        expected_size = struct.calcsize(format)
        self.serial.write(b"bstatus\n")
        size = int(self.serial.read(1)[0])
        if (size != expected_size):
            log.warning(f"Error reading binary status, expected {expected_size} bytes, got {size} bytes")
            time.sleep(0.1)
            self.serial.read_all()
            return None
        payload = self.serial.read(size)
        crc = self.serial.read(1)
        hash = crc8.crc8()
        hash.update(payload)
        if crc[0] != hash.digest()[0]:
            log.warning("CRC error")
            time.sleep(0.1)
            self.serial.read_all()
            return None

        response = struct.unpack(format, payload)
        status = {
            "mode" : int(response[0]),
            "submode" : int(response[1]),
            "door" : bool(response[2] & 1),
            "laser_head" : bool(response[2] & 2),
            "force_vacuum" : bool(response[2] & 4),
            "vacuum" : bool(response[2] & 8),
            "hood" : bool(response[2] & 16),
            "spindle" : bool(response[2] & 32),
            "laser" : bool(response[2] & 64),
            "air" : bool(response[2] & 128),
            "pump_enable" : bool(response[2] & 256),
            "pressure" : int(response[3]),
            "pwm" : int(response[4]),
            "pump_interval_ms" : int(response[5]),
            "error" : int(response[6]),
            "debug_length" : int(response[7])
        }
        return status

    def readValue(self, key):
        """ This function is used to read a setting to the Arduino board.
        Args:
            key: The string key of the setting.
        Returns:
            The value of the setting."""
        self.serial.write(f"{key}\n".encode('utf-8'))
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '').replace('\n', '')
        return response

    def writeValue(self, key, value):
        """ This function is used to write a setting to the Arduino board.
        Args:
            key: The string key of the setting.
            value: The value of the setting."""
        self.serial.write(f"{key}={value}\n".encode('utf-8'))
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        if response != 'done\n':
            raise Exception(f"Error writing value {key}={value}")

class CNC:
    class Mode(enum.Enum):
        IDLE = 0
        ROUTER = 1
        LASER = 2
        MANUAL = 3

    """ This is the main class used to keep track of the CNC state and interface to it"""
    def __init__(self, grbl_port, arduino_port):
        if grbl_port:
            self.grbl = GrblInterface(grbl_port)
        if arduino_port:
            self.arduino = ArduinoInterface(arduino_port)
        self.gui = None
        self.process = None

    def update(self):
        # Read the status of the Arduino board
        status = self.arduino.readBinaryStatus()
        #status = self.arduino.readAsciiStatus()
        if not status:
            return

        # Update the UI if needed
        if not self.gui:
            return
        if hasattr(self.gui, "mode") and "mode" in status:
            self.gui.mode.update(status["mode"])
        if hasattr(self.gui, "air_on") and "air" in status:
            self.gui.air_on.update(status["air"])
        if hasattr(self.gui, "vacuum_on") and "vacuum" in status:
            self.gui.vacuum_on.update(status["vacuum"])
        if hasattr(self.gui, "hood_on") and "hood" in status:
            self.gui.hood_on.update(status["hood"])
        if hasattr(self.gui, "spindle_on") and "spindle" in status:
            self.gui.spindle_on.update(status["spindle"])
        if hasattr(self.gui, "laser_on") and "laser" in status:
            self.gui.laser_on.update(status["laser"])
        if hasattr(self.gui, "pump_speed") and "pump_interval_ms" in status:
            pump_interval_ms = status["pump_interval_ms"]
            if pump_interval_ms == 0:
                self.gui.pump_speed.value.set(0)
            else:
                self.gui.pump_speed.value.set(200 / pump_interval_ms)
        if hasattr(self.gui, "door_closed") and "door" in status:
            self.gui.door_closed.update(status["door"])
        if hasattr(self.gui, "laser_present") and "laser_head" in status:
            self.gui.laser_present.update(status["laser_head"])
        if hasattr(self.gui, "force_vacuum") and "force_vacuum" in status:
            self.gui.force_vacuum.update(status["force_vacuum"])
        if hasattr(self.gui, "air_pressure") and "pressure" in status:
            pressure_psi = (status["pressure"] - 104.0) / 1024.0 * 100.0
            if pressure_psi < 0:
                pressure_psi = 0
            self.gui.air_pressure.update(pressure_psi)
        if hasattr(self.gui, "pwm") and "pwm" in status:
            self.gui.pwm.update(float(status["pwm"]) / 1024.0 * 100.0)
        if hasattr(self.gui, "error") and "error" in status:
            self.gui.error.update(status["error"])

        # If there is a debug message waiting, pull it        
        if "debug_length" in status and status["debug_length"]:
            debug = self.arduino.readValue("debug")
            log.info("Arduino says: " + debug)

    def modeChange(self, choice):
        """ This function is used to change the mode of the CNC."""
        if choice.value.get() == "Manual":
            mode = CNC.Mode.MANUAL
        elif choice.value.get() == "Router":
            mode = CNC.Mode.ROUTER
        elif choice.value.get() == "Laser":
            mode = CNC.Mode.LASER
        elif choice.value.get() == "Idle":
            mode = CNC.Mode.IDLE
        else:
            raise Exception(f"Invalid mode {choice.value.get()}")
        self.modeSet(mode)
        
    def modeSet(self, mode):
        """ This function is used to set the mode of the CNC."""
        self.arduino.writeValue("mode", str(mode.value))

    def pumpChange(self, slider):
        """ This function is used to change the stepper speed."""
        if slider.value.get() == 0:
            interval = 0
        else:
            interval = int(200 / slider.value.get())
        self.arduino.writeValue("pump_interval_ms", str(interval))

    def airToggle(self, toggle):
        """ This function is used to toggle the air valve."""
        if toggle.state:
            self.arduino.writeValue("air", "1")
        else:
            self.arduino.writeValue("air", "0")

    def vacuumToggle(self, toggle):
        """ This function is used to toggle the vacuum pump."""
        if toggle.state:
            self.arduino.writeValue("vacuum", "1")
        else:
            self.arduino.writeValue("vacuum", "0")

    def hoodToggle(self, toggle):
        """ This function is used to toggle the hood."""
        if toggle.state:
            self.arduino.writeValue("hood", "1")
        else:
            self.arduino.writeValue("hood", "0")

    def spindleToggle(self, toggle):
        """ This function is used to toggle the spindle."""
        if toggle.state:
            self.arduino.writeValue("spindle", "1")
        else:
            self.arduino.writeValue("spindle", "0")

    def laserToggle(self, toggle):
        """ This function is used to toggle the laser."""
        if toggle.state:
            self.arduino.writeValue("laser", "1")
        else:
            self.arduino.writeValue("laser", "0")

class Gui:
    """ This class is used to create a GUI for the CNC controller. """
    def __init__(self):
        self.window = tk.Tk()
        self.window.protocol("WM_DELETE_WINDOW", self.onClosing)
    def update(self):
        self.window.update_idletasks()
        self.window.update()
    def onClosing(self):
        if tkmessagebox.askokcancel("Quit", "Exit the controller and close application ?"):
            self.window.destroy()
            self.window = None

class ManualGui(Gui):
    """ This class is used to create the GUI for the CNC controller in manual mode."""
    def __init__(self, cnc):
        super().__init__()
        self.window.title("CNC - Manual mode")
        self.mode_frame = tk.LabelFrame(self.window, text="Mode")
        self.mode_frame.grid(column=0, row=0, sticky=tk.W+tk.E, padx=10, pady=10)
        self.mode = MultiChoice(self.mode_frame, "Mode", 0, ["Idle", "Router", "Laser", "Manual"], cnc.modeChange)
        self.control = tk.LabelFrame(self.window, text="Control")
        self.control.grid(column=0, row=1, sticky=tk.W+tk.E, padx=10, pady=10)
        self.spindle_on = OnOffToggle(self.control, "Spindle", 0, cnc.spindleToggle)
        self.laser_on = OnOffToggle(self.control, "Laser", 1, cnc.laserToggle)
        self.air_on = OnOffToggle(self.control, "Air", 2, cnc.airToggle)
        self.vacuum_on = OnOffToggle(self.control, "Vacuum", 3, cnc.vacuumToggle)
        self.hood_on = OnOffToggle(self.control, "Hood", 4, cnc.hoodToggle)
        self.pump_speed = Slider(self.control, "Pump Speed", 5, 0, 100, cnc.pumpChange)
        self.status = tk.LabelFrame(self.window, text="Status")
        self.status.grid(column=0, row=2, sticky=tk.W+tk.E, padx=10, pady=10)
        self.onoff_status = tk.Frame(self.status)
        self.onoff_status.grid(column=0, row=0, sticky=tk.W+tk.E, padx=10, pady=10)
        self.door_closed = OnOffToggle(self.onoff_status, "Door Closed", 0, None, read_only=True)
        self.laser_present = OnOffToggle(self.onoff_status, "Laser Present", 1, None, read_only=True)
        self.force_vacuum = OnOffToggle(self.onoff_status, "Force Vacuum", 2, None, read_only=True)
        self.gauge_status = tk.Frame(self.status)
        self.gauge_status.grid(column=0, row=1, sticky=tk.W+tk.E, padx=10, pady=10)
        self.air_pressure = Gauge(self.gauge_status, "Air Pressure", 0, 0, 0, 100, 30)
        self.pwm = Gauge(self.gauge_status, "PWM", 0, 1, 0, 100, None)

class LaserGui(Gui):
    """ This class is used to create the GUI for the CNC controller in laser mode."""
    def __init__(self, cnc):
        super().__init__()
        self.window.title("CNC - Laser mode")
        self.control = tk.LabelFrame(self.window, text="Control")
        self.control.grid(column=0, row=0, sticky=tk.W+tk.E, padx=10, pady=10)
        self.laser_on = OnOffToggle(self.control, "Laser", 0, cnc.laserToggle)
        self.air_on = OnOffToggle(self.control, "Air", 1, cnc.airToggle)
        self.vacuum_on = OnOffToggle(self.control, "Vacuum", 2, cnc.vacuumToggle)
        self.hood_on = OnOffToggle(self.control, "Hood", 3, cnc.hoodToggle)
        self.status = tk.LabelFrame(self.window, text="Status")
        self.status.grid(column=0, row=1, sticky=tk.W+tk.E, padx=10, pady=10)
        self.error = ErrorText(self.status, "Error", 0)
        self.onoff_status = tk.Frame(self.status)
        self.onoff_status.grid(column=0, row=1, sticky=tk.W+tk.E, padx=10, pady=10)
        self.door_closed = OnOffToggle(self.onoff_status, "Door Closed", 0, None, read_only=True)
        self.laser_present = OnOffToggle(self.onoff_status, "Laser Present", 1, None, read_only=True)
        self.force_vacuum = OnOffToggle(self.onoff_status, "Force Vacuum Switch", 2, None, read_only=True)
        self.gauge_status = tk.Frame(self.status)
        self.gauge_status.grid(column=0, row=2, sticky=tk.W+tk.E, padx=10, pady=10)
        self.air_pressure = Gauge(self.gauge_status, "Air Pressure", 0, 0, 0, 100, 30)
        self.pwm = Gauge(self.gauge_status, "PWM", 0, 1, 0, 100, None)

class RouterGui(Gui):
    """ This class is used to create the GUI for the CNC controller in router mode."""
    def __init__(self, cnc):
        super().__init__()
        self.window.title("CNC - Router mode")
        self.control = tk.LabelFrame(self.window, text="Control")
        self.control.grid(column=0, row=0, sticky=tk.W+tk.E, padx=10, pady=10)
        self.spindle_on = OnOffToggle(self.control, "Spindle", 0, cnc.spindleToggle)
        self.air_on = OnOffToggle(self.control, "Air", 1, cnc.airToggle)
        self.vacuum_on = OnOffToggle(self.control, "Vacuum", 2, cnc.vacuumToggle)
        self.hood_on = OnOffToggle(self.control, "Hood", 3, cnc.hoodToggle)
        self.pump_speed = Slider(self.control, "Pump Speed", 4, 0, 100, cnc.pumpChange)
        self.status = tk.LabelFrame(self.window, text="Status")
        self.status.grid(column=0, row=1, sticky=tk.W+tk.E, padx=10, pady=10)
        self.onoff_status = tk.Frame(self.status)
        self.onoff_status.grid(column=0, row=0, sticky=tk.W+tk.E, padx=10, pady=10)
        self.door_closed = OnOffToggle(self.onoff_status, "Door Closed", 0, None, read_only=True)
        self.laser_present = OnOffToggle(self.onoff_status, "Laser Present", 1, None, read_only=True)
        self.force_vacuum = OnOffToggle(self.onoff_status, "Force Vacuum Switch", 2, None, read_only=True)
        self.gauge_status = tk.Frame(self.status)
        self.gauge_status.grid(column=0, row=1, sticky=tk.W+tk.E, padx=10, pady=10)
        self.air_pressure = Gauge(self.gauge_status, "Air Pressure", 0, 0, 0, 100, 30)
        self.pwm = Gauge(self.gauge_status, "PWM", 0, 1, 0, 100, None)

def killProgramByName(name):
    """ This function is used to kill a specific controller program.
    Args:
        name: The name (used by the OS) of the executable to kill.
    """
    # List all running processes
    for proc in psutil.process_iter():
        if proc.name() == name:
            log.info(f"Killing {proc.name()}")
            proc.kill()

def runCNC():
    """ This function is used to run the CNC controller program."""

    # Setup the logging format to display time
    log.basicConfig(format='%(asctime)s - %(message)s', level=log.INFO)

    # Parse the command line arguments
    arg_parser = argparse.ArgumentParser(description="CNC controller")
    arg_parser.add_argument("--grbl_port", help="COM port connected to the GRBL controller", default="COM6", type=str)
    arg_parser.add_argument("--arduino_port", help="COM port connected to the Arduino board", default="COM7", type=str)
    mode_group = arg_parser.add_mutually_exclusive_group(required=True)
    mode_group.add_argument("--laser", help="Enable laser mode", action="store_true")
    mode_group.add_argument("--router", help="Enable router mode", action="store_true")
    mode_group.add_argument("--manual", help="Enable manual mode", action="store_true")
    arg_parser.add_argument("--lighburn_exec", help="Path to Lightburn executable", default="C:\\Program Files\\LightBurn\\LightBurn.exe", type=str)
    arg_parser.add_argument("--shapeoko_exec", help="Path to Shapeoko executable", default="C:\\Program Files (x86)\\Carbide\\carbidemotion.exe", type=str)
    args = arg_parser.parse_args()

    # In laser or CNC mode, we will access the GRBL controller, before we do that
    # we need to kill any running Lightburn or Carbide Motion program
    if args.laser or args.router:
        killProgramByName(os.path.basename(args.lighburn_exec))
        killProgramByName(os.path.basename(args.shapeoko_exec))
        # Now we can open the ports
        cnc = CNC(args.grbl_port, args.arduino_port)
    else:
        cnc = CNC(None, args.arduino_port)

    # Now what we do depends on the mode
    if args.manual:
        # Change mode
        log.info("Configuring the GRBL controller to run in manual mode...")
        cnc.modeSet(CNC.Mode.MANUAL)
        # Create the GUI
        cnc.gui = ManualGui(cnc)

    if args.laser:
        log.info("Configuring the GRBL controller to run in laser mode...")
        # We first connect to the GRBL controller and make sure that we send the correct settings
        # Do not report anything back except status
        cnc.grbl.writeSettings(10, 0)
        # Set the laser mode
        cnc.grbl.writeSettings(32, 1)
        # Set the minimum spindle speed/power to 0
        cnc.grbl.writeSettings(31, 0)
        # Set the maximum spindle speed/power to 1000
        cnc.grbl.writeSettings(30, 1000)
        # Home the machine
        cnc.grbl.home()
        # Set the origin to the corner opposite to home
        cnc.grbl.sendCommand("G10 L2 P1 X-845 Y-845")
        # Close the connection to the GRBL controller
        cnc.grbl.close()
        # Create the GUI
        cnc.gui = LaserGui(cnc)
        # Then we can open the Lightburn program
        log.info("Starting Lightburn...")
        cnc.process = subprocess.Popen(args.lighburn_exec)
        # Changing the Arduino to Laser mode
        log.info("Configuring the Arduino board to run in laser mode...")
        cnc.modeSet(CNC.Mode.LASER)

    if args.router:
        log.info("Configuring the GRBL controller to run in Router mode...")
        # We first connect to the GRBL controller and make sure that we send the correct settings
        # Report everything back
        cnc.grbl.writeSettings(10, 255)
        # Turn off the laser mode
        cnc.grbl.writeSettings(32, 0)
        # Set the minimum spindle speed/power to 0
        cnc.grbl.writeSettings(31, 0)
        # Set the maximum spindle speed/power to the maximum the spindle can handle
        cnc.grbl.writeSettings(30, 22800)
        # Home the machine
        cnc.grbl.home()
        # Set the origin to the home corner
        cnc.grbl.sendCommand("G10 L2 P1 X0 Y0")
        # Close the connection to the GRBL controller
        cnc.grbl.close()
        # Create the GUI
        cnc.gui = RouterGui(cnc)
        # Then we can open the Shapeoko program
        log.info("Starting Carbide Motion...")
        cnc.process = subprocess.Popen(args.shapeoko_exec)
        # Changing the Arduino to Router mode
        log.info("Configuring the Arduino board to run in router mode...")
        cnc.modeSet(CNC.Mode.ROUTER)

    # Main loop
    while True:
        # Update the CNC controller
        cnc.update()
        # Update the GUI
        cnc.gui.update()
        if not cnc.gui.window:
            break
        # Check that the associated process is still running
        if cnc.process and cnc.process.poll() is not None:
            break

    # Clsoe the associated process if it is still running
    if cnc.process:
        cnc.process.kill()

if __name__ == '__main__':
    sys.exit(runCNC())

