#!/bin/bash

run_prog() { # [iterations] [threads] [dir]
  for ((i = 1; i <= $1; i++)); do
    ./mdu_competition $3 -j $2;
  done;
}

if [[ $# -ne 3 ]]; then
  echo "usage: $0 [ITERATIONS] [MAX THREAD COUNT] [DIR]"
  exit
fi

log_file="mdu_test.log"
test_dir=$3
iterations=$1
max_threads=$2

echo "----- New test -----" >> $log_file
echo "Iterations: $iterations    Threads: $max_threads" >> $log_file

fastest_time=10000
for ((i = 1; i <= $max_threads; i++)); do
  timeout=$( { time run_prog "$iterations" "$i" "$test_dir"; } 2>&1 )
  fmt=$(echo "$timeout" | grep 'real' | awk '{print $2}' | sed s/s// | sed s/.m//)

  if (( $(echo "$fmt < $fastest_time" | bc -l) )); then
    fastest_time="$fmt"
    fastest_threads="$i"
  fi

  echo "$i $fmt" >> $log_file
done;

echo "Saved results to $log_file"
echo "Fastest time: "$fastest_time"s" 
echo "Used threads: $fastest_threads"
