#!/bin/bash

# Function to run iperf3 periodically and log output
run_iperf3_periodically() {
    local server_ip="$1"    # First argument: server IP
    local port="$2"         # Second argument: server port
    local size="$3"         # Third argument: size of the noise  
    local interval="$4"     # Fourth argument: interval between tests

    # Validate inputs
    if [[ -z "$server_ip" || -z "$port" || -z "$size" || -z "$interval" ]]; then
        echo "Usage: $0 <server_ip> <port> <size> <interval>"
        exit 1
    fi

    # Define the log file
    local logfile="sender_port_${port}.txt"

    echo "Starting periodic iperf3 tests..."
    echo "Logs will be saved to $logfile"

    # Record start time for synchronization
    local start_time=$(date +%s)

    while true; do
        echo "$(date): Running iperf3 test..."
        for ((i = 1; i <= interval; i++)); do
            echo "1.00:0" | tee -a "$logfile" 2>&1
            sleep 1
        done

        iperf3 -c "$server_ip" -p "$port" -n "$size" | grep 'sec' | sed '$d' | sed '$d' | awk '
        {
            split($3, times, "-");  # Split the interval (e.g., 0.00-1.00)
            start_time = times[1] + 0;  # Convert start time to float
            end_time = times[2] + 0;    # Convert end time to float
            duration = end_time - start_time;  # Compute duration
            bitrate = $7;  # Bitrate value
            unit = $8;     # Unit (Mbits/sec or Kbits/sec)
            if (unit == "Kbits/sec") bitrate /= 1000;  # Convert Kbits to Mbits if necessary
            printf "%.2f:%.2f\n", duration, bitrate;
        }' | tee -a "$logfile" 2>&1
    done
}

# Main script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Pass command-line arguments to the function
    run_iperf3_periodically "$@"
fi