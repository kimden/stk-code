import matplotlib
import matplotlib.pyplot as plt
import numpy as np
from scanf import scanf
import sys

inf = 1e100

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
        if line[0] != '#':
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

def iP(s, n):
    for i in range(n):
        print(s, end='')


def findBestTime(lap_count, compound, tyre_age, times_cumulative_list, memoization_of_best_times, penalty, stop_limit, depth):
    best_time = 0
    compound_list = []
    stint_lengths = []
    if lap_count > tyre_age:
        # Substract first lap, and use outlap for time calculations instead
        # Outlap assumed to be non-corrected for penalty, uncomment "+ penalty" in the for loop later to assume it is corrected instead
        best_time += (times_cumulative_list[compound][tyre_age-1] - times_cumulative_list[compound][0])\
                     + (times_cumulative_list[compound][-1] - times_cumulative_list[compound][-2])
        compound_list.append(compound)
        stint_lengths.append(tyre_age)

        """
        iP('\t', depth)
        print("PATH REC AT DEPTH", depth)

        iP('\t', depth+1)
        print("COMPOUND",compound)
        iP('\t', depth+1)
        print("LAPS",lap_count)
        iP('\t', depth+1)
        print("AGE",tyre_age)
        iP('\t', depth+1)
        print("TIME",best_time)
        iP('\t', depth+1)
        print("COMPOUNDS_HEAD",compound_list)
        iP('\t', depth+1)
        print("HEAD_LENGTH", stint_lengths)
        """

        best_found_t = inf # 24 hours so that it's always improvable
        best_found_c = []
        best_found_s = []
        for i in range(len(times_cumulative_list)):
            for j in reversed(range(1, len(times_cumulative_list[i])-2+1)):
                if lap_count-tyre_age >= j:
                    if memoization_of_best_times[i][lap_count-tyre_age-1][j-1][0] == 0:
                        (t, c, s) = findBestTime(lap_count-tyre_age, i, j, times_cumulative_list, memoization_of_best_times, penalty, stop_limit-1, depth+1) # + penalty # Uncomment this to assume penalty-corrected outlap
                        memoization_of_best_times[i][lap_count-tyre_age-1][j-1] = (t, c.copy(), s.copy())
                    else:
                        (t, c, s) = memoization_of_best_times[i][lap_count-tyre_age-1][j-1]
                    #iP('\t', depth+1)
                    #print("LOOP", "(", i, j, ")", "TIME:", t, c, s)
                    if stop_limit > 0 and t < best_found_t and len(best_found_c) <= stop_limit:
                        best_found_t = t
                        best_found_c = c.copy()
                        best_found_s = s.copy()
                    elif stop_limit == 0:
                        best_found_t = inf
        best_time += best_found_t
        compound_list.extend(best_found_c)
        stint_lengths.extend(best_found_s)
    elif lap_count == tyre_age:
        best_time += times_cumulative_list[compound][tyre_age-1]
        compound_list.append(compound)
        stint_lengths.append(tyre_age)

        """
        iP('\t', depth)
        print("PATH BASE AT DEPTH", depth)

        iP('\t', depth+1)
        print("COMPOUND",compound)
        iP('\t', depth+1)
        print("LAPS",lap_count)
        iP('\t', depth+1)
        print("AGE",tyre_age)
        iP('\t', depth+1)
        print("TIME",best_time)
        iP('\t', depth+1)
        print("COMPOUNDS_HEAD",compound_list)
        iP('\t', depth+1)
        print("HEAD_LENGTH", stint_lengths)
        """

    else:
        """
        iP('\t', depth)
        print("ERROR AT DEPTH", depth)

        iP('\t', depth)
        print("COMPOUND",compound)
        iP('\t', depth)
        print("LAPS",lap_count)
        iP('\t', depth)
        print("AGE",tyre_age)
        """
        best_time = inf
    return (best_time, compound_list, stint_lengths)

def findBestTimeAbsolute(lap_count, times_cumulative_list, memoization_of_best_times, penalty, stop_limit, best_limit):
    best_times = [ [] for i in range(len(times_cumulative_list)) ]
    compound_lists = [ [] for i in range(len(times_cumulative_list)) ]
    stint_lengths = [ [] for i in range(len(times_cumulative_list)) ]
    # Substract first lap, and use outlap for time calculations instead
    # Outlap assumed to be non-corrected for penalty, uncomment "+ penalty" in the for loop later to assume it is corrected instead
    for i in range(len(times_cumulative_list)):
        best_found_t = inf # 24 hours so that it's always improvable
        best_found_c = []
        best_found_s = []
        for j in reversed(range(1, len(times_cumulative_list[i])-2+1)):
            if lap_count >= j:
                if memoization_of_best_times[i][lap_count-1][j-1][0] == 0:
                    (t, c, s) = findBestTime(lap_count, i, j, times_cumulative_list, memoization_of_best_times, penalty, stop_limit,1) # + penalty # Uncomment this to assume penalty-corrected outlap
                    memoization_of_best_times[i][lap_count-1][j-1] = (t, c.copy(), s.copy())
                else:
                    (t, c, s) = memoization_of_best_times[i][lap_count-1][j-1]
                #print("GLOBAL LOOP", i, j, "TIME:", t, c, s)
                #print("IN", i, ":::", "\n\t", best_found_t, "\n\t", compound_lists[i], "\n\t", stint_lengths[i])
                if stop_limit > 0 and t <= best_found_t and len(best_found_c) <= stop_limit+1:
                    if (t == best_found_t) and (all(c != k for k in best_found_c) or all(s != k for k in best_found_s)) and (len(best_times[i]) < best_limit):
                        best_found_t = t
                        best_times[i].append(t)

                        best_found_c.append(c.copy())
                        best_found_s.append(s.copy())

                        compound_lists[i].append(c.copy())
                        stint_lengths[i].append(s.copy())
                    elif t < best_found_t:
                        best_found_t = t
                        best_times[i] = [t]

                        best_found_c = [c.copy()]
                        best_found_s = [s.copy()]

                        compound_lists[i] = [c.copy()]
                        stint_lengths[i] = [s.copy()]
                elif stop_limit == 0:
                    best_found_t = inf
    #best_time = best_found_t
    #compound_lists = best_found_c
    #stint_lengths = best_found_s
    return (best_times, compound_lists, stint_lengths)


# strategize laps stop_limit [FILES]
def mainProgram():
    first_times = []
    first_times_cumulative = []
    times_cumulative_list = []
    x = []
    y = []
    if sys.argv[1] == "strategize":
        start_of_runs = 4
    else:
        start_of_runs = 2
    for i in range(start_of_runs, len(sys.argv)):
        (laps, times, times_cumulative, compound_changes, lap_amount, finished, kart) = processFile(open(sys.argv[i]))
        if i == start_of_runs:
            first_times = times.copy()
            first_times_cumulative = times_cumulative.copy()
        mycolor = getColor(i-start_of_runs)
        plt.xlabel("Laps from start")
        plt.ylabel("Time from start")
        match sys.argv[1]:
            case "strategize":
                times_cumulative_list.append(times_cumulative.copy())
            case "absolute":
                for j in compound_changes:
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], 30+(i-start_of_runs), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], 30+(i-start_of_runs), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times)
            case "absolute_nopits":
                for j in compound_changes:
                    if j[1] != 1:
                        times[j[1]-1] = None
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], 30+(i-start_of_runs), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], 30+(i-start_of_runs), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times)
            case "relative_laps":
                for j in range(0, len(times)):
                    times[j] = times[j] - first_times[j]
                for j in compound_changes:
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], -10+(i-start_of_runs), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], -10+(i-start_of_runs), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times)
            case "relative_gaps":
                for j in range(0, len(times_cumulative)):
                    times_cumulative[j] = times_cumulative[j] - first_times_cumulative[j]
                for j in compound_changes:
                    plt.axvline(x=j[1], color=mycolor, linestyle = 'dotted')
                    if j[1] == 1:
                        plt.text(j[1], -10+(i-start_of_runs), getCompound(j[0]) + " " + kart, fontsize=12, color=mycolor)
                    else:
                        plt.text(j[1], -10+(i-start_of_runs), getCompound(j[0]), fontsize=12, color=mycolor)
                x = np.array(laps)
                y = np.array(times_cumulative)
        plt.xticks(x)
        plt.plot(x, y, color=mycolor, alpha=0.5)


    if sys.argv[1] != "strategize":
        plt.show()
    else:
        lap_num = int(sys.argv[2])
        stop_limit = int(sys.argv[3])

        can_use_laps_as_limit = True
        for i in times_cumulative_list:
            if len(i)-2 < lap_num:
                can_use_laps_as_limit = False
        if can_use_laps_as_limit:
            size_limit_of_tyre_ages = lap_num
        else:
            size_limit_of_tyre_ages = max([(len(i)-2) for i in times_cumulative_list])

        size_x = len(times_cumulative_list)
        size_y = lap_num
        size_z = size_limit_of_tyre_ages
        base_list = (0, [], [])
        memoization_of_best_times = []
        memoization_of_best_times = [[[base_list for k in range(size_z)] for j in range(size_y)] for i in range(size_x)]

        best_limit = 5
        (t, c, s) = findBestTimeAbsolute(lap_num, times_cumulative_list, memoization_of_best_times, compound_changes[1][2], stop_limit, best_limit)
        for i in range(len(times_cumulative_list)):
            co = 0
            for j in t[i]:
                print("Solution C", i, "/" "R", co+1, ":::", j, c[i][co], s[i][co])
                co += 1

mainProgram()
