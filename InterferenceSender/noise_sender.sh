#!/bin/bash

FILES=($(ls ./data/))

# Define the destination IP and port
DEST_IP="dest_ip"
PORT="5555"

# Function to send file via UDP
send_file() {
    local file=$1
    local dest_ip=$2
    local port=$3
    local interval=$4
    local index=$5
    port=$((port + $index))
    local file_path="./data/$file"
    local start_time=$(date +%s)
    local iterations=0

    while true; do
        # Send the file using netcat (UDP mode)
        echo "Sending $file to $dest_ip:$port"
        cat "$file_path" | nc -u -w1 "$dest_ip" "$port"
        echo "Sent $file"

        current_time=$(date +%s)
        while ((current_time - start_time < interval)); do
            current_time=$(date +%s)
        done

        # Update the start time for the next iteration
        start_time=$current_time
        iterations=$((iterations + 1))
        if ((iterations >= 10)); then
            break
        fi
    done
}
start_time=$(date +%s)

for i in "${!FILES[@]}"; do
    interval=$((10 * (i + 1)))
    (send_file "${FILES[$i]}" "$DEST_IP" "$PORT" "$interval" $i) &
done

wait

current_time=$(date +%s)
final_time=$((current_time - start_time))
echo "Time taken to send all files: $final_time seconds"
echo "All files have been sent."
