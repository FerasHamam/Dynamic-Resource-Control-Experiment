#!/bin/bash

# Get the total file size
FILE_SIZE=$(stat --format="%s" full_data_xgc.bin)

# Calculate the size for the first part (6.24% of the total size) using bc
PART1_SIZE=$(echo "$FILE_SIZE * 0.0624" | bc)

# Round PART1_SIZE to the nearest integer
PART1_SIZE=$(echo "$PART1_SIZE" | awk '{printf "%.0f\n", $1}')

# Ensure PART1_SIZE is at least 1 byte
PART1_SIZE=$((PART1_SIZE > 0 ? PART1_SIZE : 1)) 

# Use split for faster splitting
split -b "$PART1_SIZE" full_data_xgc.bin part1_ 

# Calculate the size for the second part using bc
PART2_SIZE=$(echo "$FILE_SIZE - $PART1_SIZE" | bc)

# Ensure PART2_SIZE is non-negative
PART2_SIZE=$((PART2_SIZE > 0 ? PART2_SIZE : 0)) 

# Concatenate all files except part1_aa
for file in part1_*
do
  if [[ "$file" != "part1_aa" ]]; then
    cat "$file" >> large_portion.bin
  fi
done

cp part1_aa small_portion.bin

rm part*
