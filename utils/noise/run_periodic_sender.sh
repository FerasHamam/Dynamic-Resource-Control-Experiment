#!/bin/bash

# Function to run iperf3 periodically and log output
run_iperf3_periodically() {
    local server_ip="$1"    # First argument: server IP
    local port="$2"         # Second argument: server port
    local duration="$3"     # Third argument: duration of each test
    local bandwidth="$4"    # Fourth argument: bandwidth for the test
    local interval="$5"     # Fifth argument: interval between tests

    # Validate inputs
    if [[ -z "$server_ip" || -z "$port" || -z "$duration" || -z "$bandwidth" || -z "$interval" ]]; then
        echo "Usage: $0 <server_ip> <port> <duration> <bandwidth> <interval>"
        exit 1
    fi

    # Define the log file
    local logfile="sender_port_${port}.txt"

    echo "Starting periodic iperf3 tests..."
    echo "Server: $server_ip, Port: $port, Duration: ${duration}s, Bandwidth: $bandwidth, Interval: ${interval}s"
    echo "Logs will be saved to $logfile"

    # Infinite loop to run iperf3 periodically
    while true; do
        echo "$(date): Running iperf3 test..." | tee -a "$logfile"
        iperf3 -c "$server_ip" -p "$port" -t "$duration" -b "$bandwidth" --timestamps >> "$logfile" 2>&1
        
        if [[ $? -ne 0 ]]; then
            echo "$(date): Error: iperf3 command failed. Retrying in $interval seconds..." | tee -a "$logfile"
        else
            echo "$(date): iperf3 test completed successfully. Waiting for $interval seconds..." | tee -a "$logfile"
        fi

        # Sleep for the specified interval
        sleep "$interval"
    done
}

# Main script execution
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    # Pass command-line arguments to the function
    run_iperf3_periodically "$@"
fi
