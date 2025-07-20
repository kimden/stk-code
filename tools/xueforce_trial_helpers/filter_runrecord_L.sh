#!/bin/sh

grep -o "\[RunRecord\]: L.*" | sed "s/\[RunRecord\]: L //g" | sed "s/rt4-crowded-stadium //g"
