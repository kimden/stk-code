import matplotlib
import matplotlib.pyplot as plt
import numpy as np
from scanf import scanf
import sys


def getCompound(n):
    match n:
        case 1:
            return "S"
        case 2:
            return "S"
        case 3:
            return "M"
        case 4:
            return "H"
        case 5:
            return "H"

def getColor(n):
    match n:
        case 0:
            return 'r'
        case 1:
            return 'g'
        case 2:
            return 'b'
        case 3:
            return 'y'
        case 4:
            return 'c'
        case 5:
            return 'm'

def processFile(fp):
    laps = []
    times = []
    times_cumulative = []
    compound_changes = []
    count = 0
    kart = "unknown"
    finished = False
    for line in fp:
        parselist = scanf("START %d %s", line)
        if parselist != None:
            compound_changes.append([parselist[0], count + 1, 0])
            kart = parselist[1]
        else:
            parselist = scanf("LAP %f", line)
            if parselist != None:
                times.append(parselist[0])
                laps.append(count+1)
                count += 1
            else:
                parselist = scanf("CHANGE %d %d", line)
                if parselist != None:
                    compound_changes.append([parselist[0], count + 1, parselist[1]]);
                else:
                    if line == "END":
                        finished = True
                    else:
                        None
    times_cumulative.append(times[0])
    for i in range(1, len(times)):
        times_cumulative.append(times[i] + times_cumulative[i-1])
    return (laps, times, times_cumulative, compound_changes, count + 1, finished, kart)

def mainProgram():
    first_times = []
    first_times_cumulative = []
    x = []
    y = []
    for i in range(2, len(sys.argv)):
        (laps, times, times_cumulative, compound_changes, lap_amount, finished, kart) = processFile(open(sys.argv[i]))
        if i == 2:
            first_times = times.copy()
            first_times_cumulative = times_cumulative.copy()
        mycolor = getColor(i-2)
        plt.xlabel("Laps from start")
        plt.ylabel("Time from start")
        print(first_times)
        match sys.argv[1]:
            case "absolute":
                for j in compound_changes:
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], 30+(i-2), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], 30+(i-2), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times)
            case "absolute_nopits":
                for j in compound_changes:
                    if j[1] != 1:
                        times[j[1]-1] = None
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], 30+(i-2), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], 30+(i-2), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times)
            case "relative_laps":
                for j in range(0, len(times)):
                    times[j] = times[j] - first_times[j]
                for j in compound_changes:
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], -10+(i-2), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], -10+(i-2), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times)
            case "relative_gaps":
                for j in range(0, len(times_cumulative)):
                    times_cumulative[j] = times_cumulative[j] - first_times_cumulative[j]
                for j in compound_changes:
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], -10+(i-2), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], -10+(i-2), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times_cumulative)

        plt.xticks(x)
        plt.plot(x, y, color=mycolor, alpha=0.5)
    plt.show()

mainProgram()
