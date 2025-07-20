# Copyright Nomagno 2025
# CC0 license

import random as r
import math

def ojoin(s, x):
    if isinstance(x, tuple) or isinstance(x, list):
        return s.join(list(str(i) for i in x))
    elif isinstance(x, str):
        return s.join([x])
    else:
        return s.join([str(x)])

def choose(l, n):
    re = r.sample(list(enumerate(l)), n)
    return ojoin(", ", list(i[1] for i in re))

def full(l):
    return ojoin(", ", l)

# Regular  scoring will be F1*3: 75 54 45 36 30 24 18 12 6 3
# Marathon scoring will be F1  : 25 18 15 12 10  8  6  4 2 1
# This is so that it can be divided cleanly for the marathon round's three categories


# This data is specific to RT4: Crowded stadium. Modify per-track
lsprint = range(4,10)
lmarathon = range(5,13)

tlh = 11
tth = 51.1

tlm = 7
ttm = 50.0

tls = 4
tts = 48.5

pitloss = 13.0
# End of track-dependent data

timebound_upper = 35*60
timebound_lower = 28*60

stint_l = [tls, tlm, tlh]
stint_t = [tts, ttm, tth]
karts = ["light","medium","heavy"]
comps = ["Soft (C2)", "Medium (C3)", "Hard (C4)"]
items = ["yes", "no"]

variety = [
    "Free-for-all battle: Las Dunas Arena",
    "Free-for-all battle: Alien Signal",
    "Capture The Flag: Temple",
    "Soccer: Oasis",
    "Soccer: Tournament Field (Snowy)",
    "Soccer: Tournament Hoops"
]

def stintLength(laps, time):
    return laps*time
def computeRaceLength(index_list):
    num_c = [0, 0, 0];
    num_l = [0, 0, 0];
    max_t = r.choice(range(timebound_lower,timebound_upper+1))
    curr_t = 0
    curr_l = 0
    while curr_t < max_t:
        idx = r.choice(index_list)
        curr_t += (stint_t[idx]*stint_l[idx]) + pitloss
        curr_l += stint_l[idx]
        num_c[idx] += 1;
        if curr_t > timebound_lower and r.choice([0,1,2]) == 0:
            break
    return [num_c, curr_t, curr_l]


end = ";\n"
sel = input("[sprint/race/marathon/tt/variety]? ")
match sel:
    case "sprint":
        out  = "Laps:  " + choose(lsprint, 1) + end
        out += "Karts: " + choose(karts, 1) + end
        out += "Tyres: " + full(comps) + end
        out += "Items: " + choose(items, 1) + end
        print(out)
    case "race":
        banned = r.choice([0,1,2])
        do_ban = r.choice([0, 1])
        tyres = list(comps)
        if do_ban == 1:
            del tyres[banned]
        indexes = [0, 1, 2]
        if do_ban == 1:
            del indexes[banned]
        raceInfo = computeRaceLength(indexes)

        out  = "(Target time was:) " + str(raceInfo[1]/60) + end
        offset = r.choice(range(-math.floor(raceInfo[2]/15.0), math.ceil(raceInfo[2]/15.0)))
        out += "Laps:  " + str(raceInfo[2]) + " + (" + str(offset) + ")" + end
        out += "Alloc: " + str(raceInfo[0]) + end

        n = r.choice([2, 3])
        if n == 2:
            out += "Karts: " + choose(karts, n) + end
        else:
            out += "Karts: " + full(n) + end
        if do_ban:
            out += "Tyres: " + choose(tyres, 2) + end
        else:
            out += "Tyres: " + full(tyres) + end    
        out += "Items: " + choose(items, 1) + end
        print(out)
    case "marathon":
        out  = "Laps:  " + choose(lmarathon, 1) + end
        out += "Karts: " + choose(karts, 1) + end
        out += "Tyres: " + "C10 only" + end
        out += "Items: " + "1 zipper/lap, use it or you will not get more for next lap" + end
        out += "Items: " + "Item boxes, bananas, and nitro will not spawn" + end
        print(out)
    case "tt":
        out  = "Laps:  " + "1 lap, people take turns" + end
        out += "Karts: " + choose(karts, 2) + end
        out += "Tyres: " + choose(comps, 1) + end
        out += "Items: " + "no" + end
        print(out)
    case "variety":
        out  = "Event: " + choose(variety, 1) + end
        out += "Length:" + "GAME MASTER ENTER LENGTH MANUALLY IN GOALS/MINUTES/POINTS" + end
        out += "Karts: " + "any" + end
        out += "Tyres: " + "C10 only" + end
        out += "Items: " + "yes" + end
        print(out)
    case _:
        print("Unknown round type")
