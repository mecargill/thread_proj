import pandas as pd
from matplotlib import pyplot as plt
import numpy as np
from scipy import signal
import os

csv_paths = {
    "vib_data/vib_data_2026-03-21_12_14_58.csv" : "(Clamp, Cantilever w washer, no bias, adaptive threshold)",
    #"vib_data/vib_data_2026-03-21_12_39_38.csv" : "(Clamp, cant w washer, bias (500kx2))",
    "vib_data/vib_data_2026-03-21_12_42_47.csv" : "(Clamp, cant w washer, bias (1Mx2))",
    #"vib_data/vib_data_2026-03-21_12_51_00.csv" : "(Clamp, cant w washer, bias (500x2), ac couple 104)",
    #"vib_data/vib_data_2026-03-21_12_52_43.csv" : "(Clamp, cant w washer, bias (500x2), ac couple 106)",
    #"vib_data/vib_data_2026-03-21_12_57_41.csv" : "(Clamp, Cantilever NO washer, no bias)",
    #"vib_data/vib_data_2026-03-21_12_59_42.csv" : "(Clamp, Cantilever w washer, no bias) run 2",
    "vib_data/vib_data_2026-03-21_13_49_23.csv" : "Footsteps + simulated fall + adaptive threshold"
}
"""
sorted_files = sorted(os.listdir("vib_data"))
csv_paths = {
    "vib_data/" + sorted_files[-1] : "Test",
   
}"""

for csv_path in csv_paths.keys():
    df = pd.read_csv(csv_path, skiprows=[0], header=None)
    arr = df.to_numpy().flatten()
    mean = 0.0
    alpha_mean = 0.001
    alpha_dev = 0.001
    thresh_factor = 3.9
    dev = 0.0   # initialize non-zero
    
    threshs = []
    for i, x in enumerate(arr):
        # --- Mean (clipped update) ---    
        mean = alpha_mean * x + (1-alpha_mean) * mean

        # --- Deviation ---
        diff = x - mean
        abs_diff = abs(diff)

        dev = alpha_dev * abs_diff + (1 - alpha_dev) * dev
        if dev < 30:
            dev = 30
        threshs.append(mean + thresh_factor*dev)
        


    x = range(len(arr))
    plt.figure()
    plt.plot(x, arr, x, threshs)
    plt.ylabel("ADC Output (~0-3V, 0-4096)")
    plt.title(f"Vibration Data {csv_paths[csv_path]}")
plt.show()