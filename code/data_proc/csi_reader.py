import serial
import csv
import time
from datetime import datetime
from collections import deque

#PACKET FORMAT: [start bytes (2 bytes)] [num samples (2 bytes)] [tstamp (8 bytes)][samples(num_samples*2 bytes)][end bytes (2 bytes)]
#esp32 is little endian


serial_port = "COM5"
baud_rate = 921600

start_byte = 0xAB
start_byte_count = 2
end_byte = 0xCD
end_byte_count = 2

now = datetime.now()
output_file = "csi_data/" + "csi_data_" + now.strftime("%Y-%m-%d_%H_%M_%S.csv")

byte_count_written = 0
byte_count_recv = 0
adc_buffer = deque()

def bytes_to_int(queue, num_bytes, signed):
    #lsb will come first
    result = 0
    for i in range(num_bytes):
        result += queue.popleft() * 2**(i*8)
    if signed and result >= 2**(num_bytes*8 - 1):
        result -= 2**(num_bytes*8)
    return result
    

try:
    ser = serial.Serial(serial_port, baud_rate, timeout=1)
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    print("Connected to serial port (ctrl+c to exit)")
    
    with open(output_file, "w", newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["timestamp (us)", "samples (I, Q)"])
        first_packet_received = True
        while True:
            #
            #WAITING FOR PACKET
            #
            start_reached = False
            hit_count = 0

            while (not start_reached):
                #grab data
                if ser.in_waiting > 0:
                    byte_count_recv += ser.in_waiting
                    adc_buffer.extend(ser.read(ser.in_waiting))
                    

                #check if data contains start
                while (len(adc_buffer) > 0 and not start_reached):
                    if (adc_buffer.popleft() == start_byte):
                        hit_count += 1
                        if hit_count == start_byte_count:
                            start_reached = True
                    else:
                        hit_count = 0
            
            #
            #IN PACKET
            #
            if first_packet_received:
                print("Data is successfully being transferred")
                first_packet_received = False

            #get length
            while (len(adc_buffer) < 2):
                #grab data
                if ser.in_waiting > 0:
                    byte_count_recv += ser.in_waiting
                    adc_buffer.extend(ser.read(ser.in_waiting))
            #esp32 is little endian
            num_samples = bytes_to_int(adc_buffer, 2, False)

            #get tstamp
            while (len(adc_buffer) < 8):
                #grab data
                if ser.in_waiting > 0:
                    byte_count_recv += ser.in_waiting
                    adc_buffer.extend(ser.read(ser.in_waiting))
            #esp32 is little endian
            tstamp = bytes_to_int(adc_buffer, 8, False)
            new_row = [tstamp]

            #grab length*2 number of bytes for samples, 2 more to check end bytes
            while (len(adc_buffer) < num_samples*2 + 2):
                #grab data
                if ser.in_waiting > 0:
                    byte_count_recv += ser.in_waiting
                    adc_buffer.extend(ser.read(ser.in_waiting))
            
            #parse sample bytes 
            new_row += [bytes_to_int(adc_buffer, 1, True) for i in range(num_samples*2)]
            
            #check that the end bytes appear
            end_correct = True
            for i in range(end_byte_count):
                if (adc_buffer.popleft() != end_byte):
                    end_correct = False
            #write to file
            if (end_correct):
                writer.writerow(new_row)
                byte_count_written += num_samples*2
                if (byte_count_written % 256 == 0): #not the best - only intersects by chance
                    print(f"{byte_count_written/1024.0} kB written")
            
except KeyboardInterrupt:
    print(f"{byte_count_written/1024.0} KB written (valid)")
    print(f"{byte_count_recv/1024.0} KB received over UART")
    print("Exiting")
finally:
    ser.close()
    print("Connection Closed")



