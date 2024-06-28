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
import psutil
import subprocess

def resource_path(relative_path):
    """ Get absolute path to resource """
    try:
        # PyInstaller creates a temp folder and stores path in _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")
    return os.path.join(base_path, 'images', relative_path)


window = tk.Tk()
window.title("CNC Control Panel")


class OnOffToggle:
    """ This is used to implement a simple ON/OFF toggle button."""
    on = tk.PhotoImage(file = resource_path("on.png"))
    off = tk.PhotoImage(file = resource_path("off.png"))
    def __init__(self, window, text, row, callback):
        self.window = window
        self.state = False
        self.callback = callback

        self.button = tk.Button(window, image=self.off, command=self.switch, bd=0)
        self.button.grid(column=1, row=row, padx=10, pady=5)
        self.text = tk.Label(window, text=text, font=("Arial", 18))
        self.text.grid(column=0, row=row, padx=10, pady=5)

    def switch(self):
        if self.state:
            self.button.config(image = self.off)
            self.state = False
        else:
            self.button.config(image = self.on)
            self.state = True
        self.callback(self)

class Gauge:
    """ This is used to implement a simple linear dial indicator."""
    def __init__(self, window, text, row, min, max, nominal, initial_value):
        self.window = window
        self.value = tk.DoubleVar(window, initial_value)
        self.meter = tkdial.Meter(window, start=min, end=max)
        self.meter.set_mark(nominal - 10, nominal + 10, "green")
        self.meter.grid(column=1, row=row, padx=10, pady=5)
        self.text = tk.Label(window, text=text, font=("Arial", 18))
        self.text.grid(column=0, row=row, padx=10, pady=5)

    def switch(self):
        if self.state:
            self.button.config(image = self.off)
            self.state = False
        else:
            self.button.config(image = self.on)
            self.state = True

class GrblInterface:
    """ This class is used to interface with the GRBL controller."""
    def __init__(self, port):
        self.serial = serial.Serial(port, 115200, timeout=60)
        self.wakeUp()

    def wakeUp(self):
        """ This function is used to wake up the GRBL controller."""
        self.serial.write(b"\r\n\r\n")
        time.sleep(1)
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
        print("Homing the machine...")
        # Send the homing command
        self.sendCommand("$H")
        # Wait for the OK to come back
        now = time.time()
        while self.serial.in_waiting() == 0:
            if time.time() - now > 40:
                raise Exception("Timeout waiting for Homing")
            time.sleep(0.1)
        time.sleep(0.1)
        response = self.serial.read_all().decode('utf-8').replace('\r', '')
        if response != 'ok\n':
            raise Exception(f"Error waiting for home")
        print("Machine homed")

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
        print(response)
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

def killProgramByName(name):
    """ This function is used to kill a specific controller program.
    Args:
        name: The name (used by the OS) of the executable to kill.
    """
    # List all running processes
    for proc in psutil.process_iter():
        if proc.name() == name:
            print(f"Killing {proc.name()}")
            proc.kill()

def runCNC():
    """ This function is used to run the CNC controller program."""
    arg_parser = argparse.ArgumentParser(description="CNC controller")
    arg_parser.add_argument("--grbl_port", help="COM port connected to the GRBL controller", default="COM6", type=str)
    arg_parser.add_argument("--arduino_port", help="COM port connected to the Arduino board", default="COM7", type=str)
    mode_group = arg_parser.add_mutually_exclusive_group(required=True)
    mode_group.add_argument("--laser", help="Enable laser mode", action="store_true")
    mode_group.add_argument("--cnc", help="Enable CNC mode", action="store_true")
    mode_group.add_argument("--manual", help="Enable manual mode", action="store_true")
    arg_parser.add_argument("--lighburn_exec", help="Path to Lightburn executable", default="C:\\Program Files\\LightBurn\\LightBurn.exe", type=str)
    arg_parser.add_argument("--shapeoko_exec", help="Path to Shapeoko executable", default="C:\\Program Files (x86)\\Carbide\\carbidemotion.exe", type=str)
    args = arg_parser.parse_args()

    # Depending on the mode, we will run the CNC or laser program
    # We first kill any of these two that is running
    if args.laser or args.cnc:
        killProgramByName(os.path.basename(args.lighburn_exec))
        killProgramByName(os.path.basename(args.shapeoko_exec))

    # Then we connect to the arduino and grbl controllers
    cnc = CNC(args.grbl_port, args.arduino_port)

    # Now what we do depends on the mode
    if args.laser:
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

        # Then we can open the Lightburn program
        lighburn_process = subprocess.Popen(args.lighburn_exec)
    if args.cnc:
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

        # Then we can open the Shapeoko program
        shapeoko_process = subprocess.Popen(args.shapeoko_exec)
    return

    settings = cnc.grbl.readSettings()
    print("GRBL settings:")
    print(settings)
    status = cnc.arduino.readStatus()
    print("Arduino status:")
    print(status)

    print("Running CNC program")

#    vacuum_on = OnOffToggle(window, "Vacuum", 0, True)

    air_on = OnOffToggle(window, "Air", 0, cnc.airToggle)
    vacuum_on = OnOffToggle(window, "Vacuum", 1, cnc.vacuumToggle)
    hood_on = OnOffToggle(window, "Hood", 2, cnc.hoodToggle)
    spindle_on = OnOffToggle(window, "Spindle", 3, cnc.spindleToggle)
    laser_on = OnOffToggle(window, "Laser", 4, cnc.laserToggle)

#    air_pressure = Gauge(window, "Air Pressure", 2, 0, 100, 50, 50)

#    mist_on = OnOffToggle(window, "Mist", 3, True)

#    laser_power = Gauge(window, "Laser Power", 4, 0, 100, 50, 50)

    window.mainloop()


if __name__ == '__main__':
    sys.exit(runCNC())

