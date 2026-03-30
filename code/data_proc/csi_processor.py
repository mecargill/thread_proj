import pandas as pd
from matplotlib import pyplot as plt
import numpy as np
from scipy import signal
# input format: [cssi_data_prefix],[rssi],[length],[first_wd_invalid],[Q1],[I1],[Q2],[I2],
#NOTE: I skip the first word, 32 bits, 4 bytes, 2 subcarriers
import pandas as pd
from matplotlib import pyplot as plt
import numpy as np
from scipy import signal
"""
csv_paths = {
    "csi_data/csi_data_2026-03-25_18_42_13.csv" : "Baseline",
    "csi_data/csi_data_2026-03-25_18_43_28.csv" : "Simulated Fall",   
}"""

csv_paths = {
    "csi_data/csi_data_2026-03-26_09_42_33.csv" : "Baseline (Walking)",
    "csi_data/csi_data_2026-03-26_09_42_57.csv" : "Walk, Fall, Still",   
}


#DC was indices 25 to 35 inclusinve
#As a minimalistic first pass, I picked the sets of subcarriers that looked most sensitive to motion
#mov_avg_lens = [1, 30, 60]
#var_win_lens = [50, 100, 200]
mov_avg_lens = [30]
var_win_lens = [50]

g_axs = [] #this will be 3d 0th dim is mov_avg_len, 1st is var win, 3rd is subplot dims
g_figs = [] #g for global
for mov_avg_len in mov_avg_lens:
    temp_fig = []
    temp_axs = []
    for var_win_len in var_win_lens:
        fig, axs = plt.subplots(2, 1, constrained_layout=True, sharey=True)
        temp_fig.append(fig)
        temp_axs.append(axs)
    g_figs.append(temp_fig)
    g_axs.append(temp_axs)

i = 0
for csv_path in csv_paths.keys():
    df = pd.read_csv(csv_path, skiprows=[0], header=None)

    time = df.iloc[:, 0]#in microseconds - since esp was turned on
    df = df.drop([0], axis=1)
    
    
    
    Q = df.iloc[:,1::2]
    Q.columns = range(len(Q.columns))
    #take out nulled subcarriers - discontinuity between 24 and 25 now
    Q = pd.concat([Q.iloc[:, :25], Q.iloc[:,36:]], axis=1)
    

    I = df.iloc[:,::2]
    I.columns = range(len(I.columns))
    I = pd.concat([I.iloc[:, :25], I.iloc[:,36:]], axis=1)
    
    mags = np.square(I) + np.square(Q)
    mag_buckets = [
        mags.iloc[:, 15:25],
        mags.iloc[:, 25:35]
    ]
    
    fig, axs = plt.subplots(2, 1)
    for j in range(len(g_axs)):
        for k in range(len(g_axs[0])):
            var_avg = [df.rolling(window=mov_avg_lens[j]).mean() for df in mag_buckets]#smooth
            var_avg = [df.mean(axis=1) for df in var_avg]#average buckets
            var_avg = [df.rolling(window=var_win_lens[k]).var()*50 for df in var_avg]#get variance
            var_avg = sum(var_avg)#combine buckets
            
            g_axs[j][k][i].plot(var_avg)
            #g_axs[j][k][i].set_ylim([0, 14])
            g_axs[j][k][i].set_xlabel("Packet Number")
            g_axs[j][k][i].set_ylabel(f"Rolling Variance (win = {var_win_lens[k]})")
            g_axs[j][k][i].set_title(f"{csv_paths[csv_path]} (Mov Avg Len {mov_avg_lens[j]})")
    i += 1

plt.show()
   





































""""
    for mags in mag_buckets:
        plt.figure()
        plt.imshow(mags, origin='lower', aspect='auto', vmin=3, vmax=32) #the rows are packets, columns are subcarriers
        plt.xlabel("Subcarrier Index")
        plt.ylabel("Packet Index")
        plt.title(f"Magnitude {csv_paths[csv_path]}")
        plt.colorbar()"""
