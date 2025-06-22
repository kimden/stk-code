cat $1 | grep "RunRecord" | sed 's/\[.*]: //g'
