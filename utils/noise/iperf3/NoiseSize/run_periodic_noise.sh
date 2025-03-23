#!/bin/bash
# filepath: /Users/ferashamam/Exploring/QOSNetworkStorage/utils/noise/iperf3/NoiseSize/run_periodic_noise_size.sh

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

    local first_iteration=true

    while true; do
        echo "$(date): Running iperf3 test..." | tee -a "$logfile" 2>&1

        local start_time=$(date +%s.%N)

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

        # Record the end time after iperf3 finishes
        local end_time=$(date +%s.%N)
        local elapsed=$(echo "$end_time - $start_time" | bc -l)

        # Calculate remaining time from the intended interval
        local remain=$(echo "$interval - $elapsed" | bc -l)
        if (( $(echo "$remain > 0" | bc -l) )); then
            echo "$(date): iperf3 took ${elapsed}s, sleeping remaining ${remain}s" | tee -a "$logfile" 2>&1
            # Sleep the remaining seconds, logging each second
            local int_remain=$(printf "%.0f" "$remain")
            local fractional=$(echo "$remain - $int_remain" | bc -l)
            for ((j = 1; j <= int_remain; j++)); do
                echo "1.00:0" | tee -a "$logfile" 2>&1
                sleep 1
            done
            # Sleep the fractional second if necessary
            if (( $(echo "$fractional > 0" | bc -l) )); then
                echo "1.00:0" | tee -a "$logfile" 2>&1
                sleep "$fractional"
            fi
        else
            echo "$(date): iperf3 took ${elapsed}s which exceeds the interval (${interval}s)" | tee -a "$logfile" 2>&1
        fi
    done
}

# Main script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Pass command-line arguments to the function
    run_iperf3_periodically "$@"
fi