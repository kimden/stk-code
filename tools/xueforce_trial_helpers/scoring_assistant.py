import math as m

scores = {}

mode = "rank"

while True:
    data = input("[SCORE/RANK [25 18 ...]/TT [25 18 ...]/EXIT]? ").split()
    if len(data) == 0 or data[0] == '':
        continue
    elif data[0] == 'EXIT':
        break
    elif data[0] == 'SCORE':
        while True:
            data = input("SCORE>")
            if data == '':
                break
            ps = data.split()
            if len(ps) < 2:
                print("ignored: no score")
                continue
            p = ps[0]
            try:
                s = int(ps[1])
            except:
                s = 0
            if p in scores:
                scores[p] += s
            else:
                scores[p] = s
    elif data[0] == 'RANK':
        if len(data) >= 2:
            scoring = [float(f) for f in data[1:]]
        else:
            scoring = [0]
        pos = 0
        while True:
            data = input("RANK>")
            if data == '':
                break;
            p = data
            if pos >= len(scoring):
                points = 0
            else:
                points = scoring[pos]

            if p in scores:
                scores[p] += points
            else:
                scores[p] = points
            pos += 1
    elif data[0] == 'TT':
        if len(data) >= 2:
            scoring = [float(f) for f in data[1:]]
        else:
            scoring = [0]

        times = {}
        while True:
            data = input("TT>")
            if data == '':
                break;
            pt = data.split()
            if len(pt) < 2:
                print("ignored: no time")
                continue
            p = pt[0]
            t = float(pt[1])
            if p in times:
                print("ignored: duplicate player")
                continue
            else:
                times[p] = t
        times_sorted = sorted(((v,k) for k,v in times.items()))
        print(times_sorted)
        times_players = [p for t,p in times_sorted]


        pos = 0
        for p in times_players:
            if pos >= len(scoring):
                    points = 0
            else:
                points = scoring[pos]

            if p in scores:
                scores[p] += points
            else:
                scores[p] = points
            pos += 1

    scores_view = sorted(((v,k) for k,v in scores.items()), reverse=True)
    print(scores_view)
