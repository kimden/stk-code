import matplotlib
import matplotlib.pyplot as plt
from matplotlib.colors import hsv_to_rgb
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

def processFileOld(fp):
    laps = []
    times = []
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
    return (laps, times, compound_changes, count + 1, finished, kart)

def myget_cmap(n, name='hsv'):
    '''Returns a function that maps each index in 0, 1, ..., n-1 to a distinct 
    RGB color; the keyword argument name must be a standard mpl colormap name.'''
    return plt.get_cmap(name, n)

def processFile(fp):
    retval = {}
    # Dictionary structure:
    # Key: kart name
    # Content: List
    #   Auxiliary current lap variable
    #   Laps (Integers)
    #   Laptimes (Integers)
    #   Compound changes ([new_compound, lap, time_penalty])

    for line in fp:
        if line[0] != '#':
            parselist = scanf("S %s %s %d", line)
            if parselist != None:
                
                retval[parselist[0]] = {}
                retval[parselist[0]]["currlap"] = 1
                retval[parselist[0]]["laps"] = []
                retval[parselist[0]]["times"] = []
                retval[parselist[0]]["changes"] = [[parselist[2], 1, 0]]
            else:
                parselist = scanf("L %s %s %f", line)
                if parselist != None:
                    retval[parselist[0]]["times"].append(parselist[2])
                    retval[parselist[0]]["laps"].append(retval[parselist[0]]["currlap"])
                    retval[parselist[0]]["currlap"] += 1
                else:
                    parselist = scanf("C %s %s %d %d", line)
                    if parselist != None:
                        retval[parselist[0]]["changes"].append([parselist[2], retval[parselist[0]]["currlap"], parselist[3]]);
                    else:
                        pass
    return retval


def iP(s, n):
    for i in range(n):
        print(s, end='')


def findBestTime(lap_count, compound, tyre_age, times_cumulative_list, memoization_of_best_times, penalty, stop_limit, depth):
    best_time = 0
    compound_list = []
    stint_lengths = []
    if lap_count > tyre_age:
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
                        (t, c, s) = findBestTime(lap_count-tyre_age, i, j, times_cumulative_list, memoization_of_best_times, penalty, stop_limit-1, depth+1)
                        t += penalty # Uncomment this line to use the penalty. 
                        # It's recommendable that the normalize option is used along with
                        # non-zero penalties to replace the outlaps with other hypothetical
                        # pit length values, for seeing the impact of different time penalties on strategy.
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
    for i in range(len(times_cumulative_list)):
        best_found_t = inf # 24 hours so that it's always improvable
        best_found_c = []
        best_found_s = []
        for j in reversed(range(1, len(times_cumulative_list[i])-2+1)):
            if lap_count >= j:
                if memoization_of_best_times[i][lap_count-1][j-1][0] == 0:
                    (t, c, s) = findBestTime(lap_count, i, j, times_cumulative_list, memoization_of_best_times, penalty, stop_limit,1)
                    memoization_of_best_times[i][lap_count-1][j-1] = (t, c.copy(), s.copy())
                else:
                    (t, c, s) = memoization_of_best_times[i][lap_count-1][j-1]
                #print("GLOBAL LOOP", i, j, "TIME:", t, c, s)
                #print("IN", i, ":::", "\n\t", best_found_t, "\n\t", compound_lists[i], "\n\t", stint_lengths[i])
                if stop_limit > 0 and t <= best_found_t and len(best_found_c) <= stop_limit+1:
                    if t < best_found_t:
                        if (len(best_times[i]) >= best_limit):
                            best_times[i] = best_times[i][1:]
                            best_found_c = best_found_c[1:]
                            best_found_s = best_found_s[1:]
                            compound_lists[i] = compound_lists[i][1:]
                            stint_lengths[i] = stint_lengths[i][1:]
                    if t < best_found_t or\
                      (t == best_found_t and\
                          (all(c != k for k in best_found_c) or all(s != k for k in best_found_s)) and\
                          (len(best_times[i]) < best_limit)):
                        best_found_t = t
                        best_times[i].append(t)

                        best_found_c.append(c.copy())
                        best_found_s.append(s.copy())

                        compound_lists[i].append(c.copy())
                        stint_lengths[i].append(s.copy())
                elif stop_limit == 0:
                    best_found_t = inf
    return (best_times, compound_lists, stint_lengths)


# strategize laps stop_limit [FILES]
def mainProgram():
    graphing_constant = 28
    graphing_positioner = lambda i, s, c: graphing_constant+8*(i-s)+2*c
    penaltyMode = 0
    first_times = []
    first_times_cumulative = []
    times_cumulative_list = []

    if sys.argv[1] == "strategize":
        if len(sys.argv) < 8:
            raise ValueError("Usage: process.py strategize [\"normalize\"/\"nothing\"] [penaltymode 0/1] [penalty] [amount of laps] [stop limit] [...RUN FILES...]")
        start_of_runs = 7
    else:
        start_of_runs = 5
        if len(sys.argv) < 6:
            raise ValueError("Usage: process.py [mode] [\"normalize\"/\"nothing\"] [penaltymode 0/1] [penalty] [...RUN FILES..]")

    penaltyMode = (sys.argv[3] == "inlap")
    penalty = int(sys.argv[4])

    xticks_array_best_length = []

    for i in range(start_of_runs, len(sys.argv)):
        dictOfRuns = processFile(open(sys.argv[i]))
        times_cumulative = {}
        for key in dictOfRuns:
            #print(dictOfRuns[key])
            if sys.argv[2] == "normalize":
                    for k in dictOfRuns[key]["changes"]:
                        if penaltyMode == True:
                            normi = k[1]-1
                        else:
                            normi = k[1]-2
                        dictOfRuns[key]["times"][normi] -= k[2]
                        if k[1] != 1:
                            dictOfRuns[key]["times"][normi] += penalty
            times_cumulative[key] = []
            times_cumulative[key].append(dictOfRuns[key]["times"][0])
            for k in range(1, len(dictOfRuns[key]["times"])):
                times_cumulative[key].append(dictOfRuns[key]["times"][k] + times_cumulative[key][k-1])

        if i == start_of_runs:
            first_times = list(dictOfRuns.values())[0]["times"].copy()
            first_times_cumulative = list(times_cumulative.values())[0].copy()


        plt.xlabel("Laps from start")
        plt.ylabel("Time from start")

        Xs = []
        Ys = []

        globalcolors = [hsv_to_rgb([(i * 0.618033988749895) % 1.0, 1, 1]) for i in range(1000)]

        colors = []

        match sys.argv[1]:
            case "strategize":
                times_cumulative_list.append(times_cumulative.copy())
            case "absolute":
                counter = 0
                for key in dictOfRuns:
                    currcolor = globalcolors[4*(i-start_of_runs)+counter];
                    colors.append(currcolor)
                    for j in dictOfRuns[key]["changes"]:
                        plt.axvline(x=j[1], c=currcolor, linestyle = 'dotted')
                        if j[1] == 1:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]) + " " + key, fontsize=12, c=currcolor)
                        else:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]), fontsize=12, c=currcolor)
                    if len(dictOfRuns[key]["laps"]) >= len(xticks_array_best_length):
                        xticks_array_best_length = dictOfRuns[key]["laps"].copy()
                    Xs.append(dictOfRuns[key]["laps"])
                    Ys.append(dictOfRuns[key]["times"])
                    counter += 1
            case "absolute_nopits":
                counter = 0
                for key in dictOfRuns:
                    currcolor = globalcolors[4*(i-start_of_runs)+counter];
                    colors.append(currcolor)
                    for j in dictOfRuns[key]["changes"]:
                        if j[1] != 1:
                            dictOfRuns[key]["times"][j[1]-1] = None
                        plt.axvline(x=j[1], c=currcolor, linestyle = 'dotted')
                        if j[1] == 1:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]) + " " + key, fontsize=12, c=currcolor)
                        else:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]), fontsize=12, c=currcolor)
                    if len(dictOfRuns[key]["laps"]) >= len(xticks_array_best_length):
                        xticks_array_best_length = dictOfRuns[key]["laps"].copy()
                    Xs.append(dictOfRuns[key]["laps"])
                    Ys.append(dictOfRuns[key]["times"])
                    counter += 1
            case "relative_laps":
                counter = 0
                for key in dictOfRuns:
                    currcolor = globalcolors[4*(i-start_of_runs)+counter];
                    colors.append(currcolor)

                    jcounter = 0
                    for j in range(min(len(first_times), len(dictOfRuns[key]["times"]))):
                        dictOfRuns[key]["times"][j] = dictOfRuns[key]["times"][j] - first_times[j]
                        jcounter += 1
                    x = dictOfRuns[key]["laps"][:jcounter].copy()
                    y = dictOfRuns[key]["times"][:jcounter].copy()
                    
                    for j in dictOfRuns[key]["changes"]:
                        plt.axvline(x=j[1], c=currcolor, linestyle = 'dotted')
                        if j[1] == 1:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]) + " " + key, fontsize=12, c=currcolor)
                        else:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]), fontsize=12, c=currcolor)
                    if len(dictOfRuns[key]["laps"]) >= len(xticks_array_best_length):
                        xticks_array_best_length = dictOfRuns[key]["laps"].copy()
                    Xs.append(x)
                    Ys.append(y)
                    counter += 1
            case "relative_gaps":
                counter = 0
                for key in dictOfRuns:
                    currcolor = globalcolors[4*(i-start_of_runs)+counter];
                    colors.append(currcolor)

                    jcounter = 0
                    for j in range(min(len(times_cumulative[key]), len(first_times_cumulative))):
                        times_cumulative[key][j] = times_cumulative[key][j] - first_times_cumulative[j]
                        jcounter += 1
                    x = dictOfRuns[key]["laps"][:jcounter].copy()
                    y = times_cumulative[key][:jcounter].copy()


                    for j in dictOfRuns[key]["changes"]:
                        plt.axvline(x=j[1], c=currcolor, linestyle = 'dotted')
                        if j[1] == 1:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]) + " " + key, fontsize=12, c=currcolor)
                        else:
                            plt.text(j[1], graphing_positioner(i, start_of_runs, counter), getCompound(j[0]), fontsize=12, c=currcolor)
                    if len(dictOfRuns[key]["laps"]) >= len(xticks_array_best_length):
                        xticks_array_best_length = dictOfRuns[key]["laps"].copy()
                    Xs.append(x)
                    Ys.append(y)
                    counter += 1
        plt.xticks(xticks_array_best_length)
        for l in range(len(Xs)):
            plt.plot(Xs[l], Ys[l], c=colors[l], alpha=0.5)


    if sys.argv[1] != "strategize":
        plt.show()
    else:
        # The strategize mode is currently not ported to the new multiplayer structure.
        lap_num = int(sys.argv[5])
        stop_limit = int(sys.argv[6])

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
        (t, c, s) = findBestTimeAbsolute(lap_num, times_cumulative_list, memoization_of_best_times, penalty, stop_limit, best_limit)
        for i in range(len(times_cumulative_list)):
            co = 0
            for j in t[i]:
                c[i][co].reverse()
                s[i][co].reverse()
                print("Solution C", i, "/" "R", co+1, ":::", j, "( C -", c[i][co], "<<>>", "S -", s[i][co], ")")
                co += 1
            print("")

mainProgram()
