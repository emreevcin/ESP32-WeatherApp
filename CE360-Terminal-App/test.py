import serial
import time
import eel

eel.init('web')

bluetooth_port = '/dev/tty.ESP32-BT-EE'

# Establish Bluetooth serial connection
try:
    bluetooth_serial = serial.Serial(bluetooth_port, 115200, timeout=1)
except serial.SerialException as e:
    print("Failed to establish Bluetooth connection:", e)
    exit()

# Wait for Arduino to initialize
time.sleep(2)

# Function to send data to Arduino
def send_data(data):
    bluetooth_serial.write(data.encode())

@eel.expose
def submit_form(ssid, password):
    send_data(ssid + ":" + password + "\n")

eel.start('index.html', size=(600, 600))
