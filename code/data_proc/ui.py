import serial
import csv
import time
from datetime import datetime
from collections import deque
import tkinter as tk
from tkinter import scrolledtext
import time

#PACKET FORMAT: [start bytes (2 bytes)] [num samples (2 bytes)] [tstamp (8 bytes)][samples(num_samples*2 bytes)][end bytes (2 bytes)]
#esp32 is little endian


serial_port = "COM5"
baud_rate = 115200

start_byte = 0xAB
start_byte_count = 2
end_byte = 0xCD
end_byte_count = 2
alert_byte = bytes([0xAB])

now = datetime.now()
output_file = "event_log_" + now.strftime("%Y-%m-%d_%H_%M_%S.csv")

adc_buffer = deque()
event_queue = deque(maxlen=70)
last_fall_time = time.perf_counter()-6

subsys_nums = {
    "CSI_LOW":0,
    "CSI_HIGH":1,
    "VIB":2,
    "MIC":3
}

subsys_names = {
    0:"CSI_LOW",
    1:"CSI_HIGH",
    2:"VIB",
    3:"MIC"
}

#destructively reads num_bytes
def bytes_to_int(queue, num_bytes, signed):
    #lsb will come first
    result = 0
    for i in range(num_bytes):
        result += queue.popleft() * 2**(i*8)
    if signed and result >= 2**(num_bytes*8 - 1):
        result -= 2**(num_bytes*8)
    return result

def check_for_fall(queue, dashboard, ser):
    global last_fall_time
    now = time.perf_counter()
    last10s = [e[1] for e in event_queue if now - e[0] < 10]
    last2s = [e[1] for e in event_queue if now - e[0] < 2]

    recent_motion = last10s.count(subsys_nums["CSI_LOW"]) >= 2
    recent_vib = last2s.count(subsys_nums["VIB"]) >= 1
    recent_mic = last10s.count(subsys_nums["MIC"]) >= 2
    recent_large_motion = last2s.count(subsys_nums["CSI_HIGH"]) >= 1
    recent_fall_time = time.perf_counter() - last_fall_time
    recent_fall = recent_fall_time < 5

    presence = recent_motion or recent_mic
    if (presence and recent_vib and not recent_fall):
        last_fall_time = time.perf_counter()
        dashboard.log_event("\nFALL DETECTED\n")
        ser.write(alert_byte)
        return (presence, True)
        
    elif (recent_vib and recent_large_motion and not recent_fall):
        last_fall_time = time.perf_counter()
        dashboard.log_event("\nFALL DETECTED\n")
        ser.write(alert_byte)
        return (presence, True)
    
    return (presence, recent_fall)

hit_count = 0
def poll_events(ser, adc_buffer, dashboard):
    global event_queue, hit_count
    start_reached = False
    start_time = time.perf_counter()
    timeout = False
    #
    #WAIT FOR START OF PACKET
    #
    while (not start_reached):
        if (time.perf_counter() - start_time > 0.1):
            timeout = True
            break

        #grab data
        if ser.in_waiting > 0:
            adc_buffer.extend(ser.read(ser.in_waiting))
            

        #check if data contains start
        while (len(adc_buffer) > 0 and not start_reached):
            if (adc_buffer.popleft() == start_byte):
                hit_count += 1
                if hit_count == start_byte_count:
                    hit_count = 0
                    start_reached = True
            else:
                hit_count = 0

    if not timeout:
        #
        #GET THE EVENT CODE
        #
        while (len(adc_buffer) < 2):
            #grab data
            if ser.in_waiting > 0:
                adc_buffer.extend(ser.read(ser.in_waiting))
        #esp32 is little endian
        subsys_code = bytes_to_int(adc_buffer, 2, False)

        #
        #GET THE TIMESTAMP
        #
        while (len(adc_buffer) < 8):
            #grab data
            if ser.in_waiting > 0:
                adc_buffer.extend(ser.read(ser.in_waiting))
        tstamp_us = bytes_to_int(adc_buffer, 8, False)

        #
        #CHECK THE END OF PACKET AND WRITE TO QUEUE
        #
        while (len(adc_buffer) < 2):
            #grab data
            if ser.in_waiting > 0:
                adc_buffer.extend(ser.read(ser.in_waiting))

        #esp32 is little endian
        end_correct = adc_buffer.popleft() == end_byte
        end_correct = end_correct and (adc_buffer.popleft() == end_byte)
        if (end_correct):
            ev_time = time.perf_counter()
            event_queue.append((ev_time, subsys_code))
            dashboard.log_event(f"[{ev_time}] {subsys_names[subsys_code]}")
            print(f"Event: {subsys_names[subsys_code]} at {ev_time} seconds")
    presence, fall = check_for_fall(event_queue, dashboard, ser)
    dashboard.set_fall(fall)
    dashboard.set_presence(presence)


class Dashboard:
    def __init__(self, root):
        self.root = root
        self.root.title("Detection Dashboard")
        self.root.geometry("600x400")

        # --- STATUS BOXES ---
        self.fall_label = self.create_status_box("Fall Detected")
        self.presence_label = self.create_status_box("Presence Detected")

        self.set_fall(False)
        self.set_presence(False)

        # --- LOG ---
        self.log = scrolledtext.ScrolledText(root, height=10, width=50)
        self.log.pack(pady=10)

    def create_status_box(self, text):
        label = tk.Label(
            self.root,
            text=text,
            width=25,
            height=3,
            bg="gray",
            fg="black",
            font=("Arial", 12, "bold")
        )
        label.pack(pady=5)
        return label

    # --- UPDATE FUNCTIONS ---
    def set_fall(self, detected: bool):
        color = "red" if detected else "gray"
        self.fall_label.config(bg=color)

    def set_presence(self, detected: bool):
        color = "yellow" if detected else "gray"
        self.presence_label.config(bg=color)

    def log_event(self, text):
        self.log.insert(tk.END, f"{text}\n")
        self.log.yview(tk.END)           
            

try:
    ser = serial.Serial(serial_port, baud_rate, timeout=0.1)
    
    if ser.in_waiting:
        ser.read(ser.in_waiting)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print("Connected to serial port (ctrl+c to exit)")
    root = tk.Tk()
    dash = Dashboard(root)
    def gui_poll():
        poll_events(ser, adc_buffer, dash)
        root.after(10, gui_poll)  # run every 10ms
    gui_poll()
    root.mainloop()
    
                
            
except KeyboardInterrupt:
    print("Exiting")
finally:
    ser.close()
    print("Connection Closed")



