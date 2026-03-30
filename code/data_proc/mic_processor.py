import pandas as pd
from matplotlib import pyplot as plt
import numpy as np
from scipy import signal

csv_paths = {
    "mic_data/mic_data_2026-03-21_17_10_16.csv" : "Baseline",
    "mic_data/mic_data_2026-03-21_17_04_20.csv" : "Speech",
    "mic_data/mic_data_2026-03-21_17_11_58.csv" : "Simulated fall (grunt/thump, then just thump)",
    
}

for csv_path in csv_paths.keys():
    df = pd.read_csv(csv_path, skiprows=[0], header=None)
    arr = df.to_numpy().flatten()**2

    plt.figure()
    plt.plot(arr)
    plt.ylabel("Mic Output Power")
    plt.title(f"Mic Data {csv_paths[csv_path]}")
plt.show()