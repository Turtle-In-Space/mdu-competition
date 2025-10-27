#!/bin/bash

if [[ $# -ne 3 ]]; then
  echo "usage: $0 [START THREADS][MAX THREAD COUNT] [DIR]"
  exit
fi

log_file="mdu_test.log"
test_dir=$3
max_threads=$2
start_threads=$1

echo "----- New test -----" >> $log_file
echo "Threads: $start_threads - $max_threads    Dir: $test_dir" >> $log_file

fastest_time=10000
for ((i = $start_threads; i <= $max_threads; i++)); do
  start=$(date +%s.%N)
  ./mdu_competition -j "$i" "$test_dir" >/dev/null
  end=$(date +%s.%N)
  fmt=$(echo "$end - $start" | bc -l)
  printf "%d %.6f\n" "$i" "$fmt"

  if (( $(echo "$fmt < $fastest_time" | bc -l) )); then
    fastest_time=$fmt
    fastest_threads=$i
  fi
done;

echo "Saved results to $log_file"
echo "Fastest time: "$fastest_time"s" 
echo "Used threads: $fastest_threads"
