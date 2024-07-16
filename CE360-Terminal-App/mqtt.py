import eel
import paho.mqtt.client as mqtt
import sqlite3
import queue
import threading
import sys
import time

# MQTT Broker Settings
broker_address = "test.mosquitto.org"  # Change to your broker's IP if not running locally
broker_port = 1883
topic = "ee/ce360/weatherapp"

# Initialize GUI
eel.init('web')

# Connect to SQLite database
conn = sqlite3.connect('weather_data.db')
c = conn.cursor()

# Create table if not exists
c.execute('''CREATE TABLE IF NOT EXISTS weather
             (date TEXT, city TEXT, temperature TEXT)''')

# Create queue for inter-thread communication
data_queue = queue.Queue()

# Callback when connection is established
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))
    client.subscribe(topic)

# Callback when a message is received
def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload.decode()))
    data = eval(msg.payload.decode())
    data_queue.put(data)

# Function to process data from the queue and insert into database
def process_data():
    while True:
        if not data_queue.empty():
            data = data_queue.get()
            city = data.get('l')
            temperature = round(float(data.get('t')))  # Round temperature and convert to integer
            temperature = str(temperature) + " °C"  # Add Celsius symbol
            date = time.strftime("%Y-%m-%d %H:%M:%S")  # Get current date and time
            with sqlite3.connect('weather_data.db') as conn:
                c = conn.cursor()
                c.execute("INSERT INTO weather (date, city, temperature) VALUES (?, ?, ?)", (date, city, temperature))
                conn.commit()
                update_gui()

# Update GUI with database values
def update_gui():
    with sqlite3.connect('weather_data.db') as conn:
        c = conn.cursor()
        weather_data = c.execute("SELECT * FROM weather").fetchall()
        eel.updateTable(weather_data)

# Populate GUI with initial data from database
update_gui()

# Create MQTT client instance
client = mqtt.Client()

# Assign callback functions
client.on_connect = on_connect
client.on_message = on_message

# Connect to MQTT broker
client.connect(broker_address, broker_port, 60)

# Start MQTT loop in a separate thread
mqtt_thread = threading.Thread(target=client.loop_forever)
mqtt_thread.start()

# Start data processing thread
data_process_thread = threading.Thread(target=process_data)
data_process_thread.start()

# Expose Python functions to JavaScript
@eel.expose
def clear_database():
    with sqlite3.connect('weather_data.db') as conn_clear:
        c_clear = conn_clear.cursor()
        c_clear.execute("DELETE FROM weather")
        conn_clear.commit()
    update_gui()

# Close the program when GUI is closed
def close_program(_, __):
    sys.exit()

# Start GUI
eel.start('main.html', close_callback=close_program)
