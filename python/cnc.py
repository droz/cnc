""" This code is used to run a homebrewed CNC and laser cutter machine.
It connects to a GRBL controller and an Arduino board through USB.
Depending if the laser head is connected, it will either run the CNC
program (Shapeoko) or the laser program (Lightburn)."""

import tkinter as tk
import tkdial as tkdial
import sys
import os


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
    def __init__(self, window, text, row, initial_state = True):
        self.window = window
        self.state = initial_state

        self.button = tk.Button(window, image=self.on, command=self.switch, bd=0)
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



# vacuum
vacuum_on = OnOffToggle(window, "Vacuum", 0, True)

air_on = OnOffToggle(window, "Air", 1, True)

air_pressure = Gauge(window, "Air Pressure", 2, 0, 100, 50, 50)

mist_on = OnOffToggle(window, "Mist", 3, True)

laser_power = Gauge(window, "Laser Power", 4, 0, 100, 50, 50)

window.mainloop()