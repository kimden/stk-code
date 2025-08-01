#!/bin/sh
grep "^[0-9]" | sed 's/[0-9]\. *//g' | sed 's/(.*)//g'

# example:
# [info   ] ClientLobby: Grand Prix standings, 1 games:
# 1.   nomagno  1  (00:00:08.606)
# 2.   Cirno_del_Nueve  0  (00:00:34.312)
