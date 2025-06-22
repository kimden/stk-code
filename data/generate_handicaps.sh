#!/bin/bash
format_string='<characteristic name="handicap-%s"> <engine power="*%s" max-speed="*%s" brake-factor="*%s" /> </characteristic>\n'

for i in {1..48}; do
    in_val=$(echo "scale=1; ${i}/2" | bc -l | awk '{printf "%.1f\n", $0}')
    per_val=$(echo "scale=3; 1 - ${i}/2*0.01" | bc -l)
    printf "${format_string}" $in_val $per_val $per_val $per_val
done

