""" This code is used to run a homebrewed CNC and laser cutter machine.
It connects to a GRBL controller and an Arduino board through USB.
Depending if the laser head is connected, it will either run the CNC
program (Shapeoko) or the laser program (Lightburn)."""

import tkinter as tk
import tkdial as tkdial
import argparse
import time
import serial
import sys
import os
import re

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
    def __init__(self, window, text, row, min, max, nominal):
        self.window = window
        self.value = tk.DoubleVar(window, 0)
        self.meter = tkdial.Meter(window, start=min, end=max, radius = 100, width = 200, height = 200)
        self.meter.set_mark(nominal - 10, nominal + 10, "green")
        self.meter.grid(column=1, row=row, padx=10, pady=5)
        self.text = tk.Label(window, text=text, font=("Arial", 18))
        self.text.grid(column=0, row=row, padx=10, pady=5)

    def update(self, value):
        self.meter.set(value)

class GrblInterface:
    """ This class is used to interface with the GRBL controller."""
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=1)
        self.wakeUp()

    def wakeUp(self):
        """ This function is used to wake up the GRBL controller."""
        self.serial.write(b"\r\n\r\n")
        time.sleep(1)
        self.serial.flushInput()

    def close(self):
        """ This function is used to close the serial connection to the GRBL controller."""
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

    def writeSettings(self, key, value):
        """ This function is used to write a setting to the GRBL controller.
        Args:
            key: The integer key of the setting.
            value: The value of the setting."""
        self.serial.write(f"${key}={value}\n".encode('utf-8'))
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        if response != 'ok\n':
            raise Exception(f"Error writing setting {key}={value}")

class ArduinoInterface:
    """ This class is used to interface with the Arduino board."""
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=1)
        time.sleep(2)

    def readStatus(self):
        """ This function is used to read the status of the Arduino board.
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
        return status

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
    """ This is the main class used to keep track of the CNC state and interface to it"""
    def __init__(self, grbl_port, arduino_port):
        self.grbl = GrblInterface(grbl_port)
        self.arduino = ArduinoInterface(arduino_port)
        self.gui = None

    def update(self):
        # Read the status of the Arduino board
        status = self.arduino.readStatus()
        # Update the UI if needed
        if self.gui:
            self.gui.air_on.update(status["air"] == "1")
            self.gui.vacuum_on.update(status["vacuum"] == "1")
            self.gui.hood_on.update(status["hood"] == "1")
            self.gui.spindle_on.update(status["spindle"] == "1")
            self.gui.laser_on.update(status["laser"] == "1")
            pump_interval_ms = int(status["pump_interval_ms"])
            if pump_interval_ms == 0:
                self.gui.pump_speed.value.set(0)
            else:
                self.gui.pump_speed.value.set(200 / pump_interval_ms)
            self.gui.door_closed.update(status["door"] == "1")
            self.gui.laser_present.update(status["laser_head"] == "1")
            self.gui.force_vacuum.update(status["force_vacuum"] == "1")
            self.gui.air_pressure.update(int(status["pressure"]))
            self.gui.pwm.update(int(status["pwm"]))

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
    """ This class is used to create the GUI for the CNC controller."""
    def __init__(self, cnc):
        self.window = tk.Tk()
        self.window.title("CNC Control Panel")

        self.air_on = OnOffToggle(self.window, "Air", 0, cnc.airToggle)
        self.vacuum_on = OnOffToggle(self.window, "Vacuum", 1, cnc.vacuumToggle)
        self.hood_on = OnOffToggle(self.window, "Hood", 2, cnc.hoodToggle)
        self.spindle_on = OnOffToggle(self.window, "Spindle", 3, cnc.spindleToggle)
        self.laser_on = OnOffToggle(self.window, "Laser", 4, cnc.laserToggle)
        self.pump_speed = Slider(self.window, "Pump Speed", 5, 0, 100, cnc.pumpChange)
        self.door_closed = OnOffToggle(self.window, "Door Closed", 6, None, read_only=True)
        self.laser_present = OnOffToggle(self.window, "Laser Present", 7, None, read_only=True)
        self.force_vacuum = OnOffToggle(self.window, "Force Vacuum", 8, None, read_only=True)
        self.air_pressure = Gauge(self.window, "Air Pressure", 9, 0, 1024, 50)
        self.pwm = Gauge(self.window, "PWM", 10, 0, 1024, 50)

    def update(self):
        self.window.update_idletasks()
        self.window.update()

def runCNC():
    """ This function is used to run the CNC controller program."""
    arg_parser = argparse.ArgumentParser(description="CNC controller")
    arg_parser.add_argument("--grbl_port", help="COM port connected to the GRBL controller", default="COM6", type=str)
    arg_parser.add_argument("--arduino_port", help="COM port connected to the Arduino board", default="COM7", type=str)
    args = arg_parser.parse_args()

    cnc = CNC(args.grbl_port, args.arduino_port)
    settings = cnc.grbl.readSettings()
    print("GRBL settings:")
    print(settings)
    cnc.grbl.close()
    status = cnc.arduino.readStatus()
    print("Arduino status:")
    print(status)

    # Create the GUI
    gui = Gui(cnc)
    cnc.gui = gui

    # Main loop
    while True:
        cnc.update()
        gui.update()

if __name__ == '__main__':
    sys.exit(runCNC())

