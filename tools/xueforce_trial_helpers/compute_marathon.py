import math
import statistics

laptimes = {}

with open('/dev/stdin') as fp:
    for line in fp:
        x = line.split()
        if len(x) == 2:
            if x[0] in laptimes:
                laptimes[x[0]].append(float(x[1]))
            else:
                laptimes[x[0]] = []
                laptimes[x[0]].append(float(x[1]))


medians = {}
averages = {}
worsts = {}
for player in laptimes:
    del laptimes[player][0]
    laps = sorted(laptimes[player]);

    #print(player+" laps: "+str(sorted(laptimes[player])))
    worsts[player] = laps[-1]
    medians[player] = statistics.median(laps)
    total = 0
    for val in laps:
        total += val
    avg = total/len(laps)
    averages[player] = avg

print("Worst laps:")
for player in worsts:
    print(player+" "+str(worsts[player]))
print("---------")
print("Median laps:")
for player in medians:
    print(player+" "+str(medians[player]))
print("---------")
print("Average laps:")
for player in averages:
    print(player+" "+str(averages[player]))
