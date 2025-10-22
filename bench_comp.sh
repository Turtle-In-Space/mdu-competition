#!/bin/bash

run_prog() { # [iterations] [threads] [dir]
  for ((i = 1; i <= $1; i++)); do
    ./mdu_competition $3 -j $2;
  done;
}

if [[ $# -ne 3 ]]; then
  echo "usage: $0 [ITERATIONS] [THREAD COUNT] [DIR]"
  exit
fi

log_file="bench.log"
test_dir=$3
iterations=$1
max_treads=$2

echo "----- New test -----" >> $log_file
echo "Iterations: $iterations    Threads: $max_treads    Dir: $test_dir" >> $log_file

timeout=$( { time run_prog "$iterations" "$max_treads" "$test_dir"; } 2>&1 )
fmt=$(echo "$timeout" | grep 'real' | awk '{print $2}' | sed s/s// | sed s/.m//)
average=$(echo $fmt $iterations | awk '{print $1 / $2}' | bc)

echo "Time: "$average"s" >> $log_file

echo "Saved results to $log_file"
echo "Average: "$average"s"
