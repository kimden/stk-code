#!/bin/sh
# Arguments:
# First - Track ID
# Second - message type: S, C, L, E
# Third - If S: starting compound
#         If C: new compound
#         If L: laptime
#         If E: ignored
# Fourth - If S: Kart ID
#          If C: stop length in seconds
#          If L: Ignored
#          IF E: ignored


echo -- ARGS $1 $2 $3 $4

mkdir -p ./runs/"$1"/
LAST=$(find ./runs/"$1"/ -type f -exec basename {} \; | sort -V | tail -n 1)
case $2 in
	S)
		echo "START $3 $4" >> ./runs/"$1"/$(($LAST + 1))
		;;
	C)
		echo "CHANGE $3 $4" >> ./runs/"$1"/$LAST
		;;
	L)
		echo "LAP $3" >> ./runs/"$1"/$LAST
		;;
	E)
		echo "END" >> ./runs/"$1"/$LAST
		;;
	P) # Prune single-line files
		find ./runs -type f | for i in $(cat); do
			if [ $(cat "$i" | wc -l) = 1 ]; then
				rm "$i"
			fi
		done
esac
