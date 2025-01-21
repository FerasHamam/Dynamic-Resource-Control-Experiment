import zmq
import time
import matplotlib.pyplot as plt
import numpy as np
import random
import json
import signal
import sys
from multiprocessing import Process, Queue

def log(message, enabled):
    """Log message if logging is enabled."""
    if enabled:
        print(message)

def signal_handler(signal, frame):
    """Handle termination signal to exit gracefully."""
    print("\nStopping the receiver application...")
    sys.exit(0)

def receive_traffic(socket_address, queue, socket_id, duration, log_enabled, continuous):
    """Receive and log traffic from a single socket."""
    context = zmq.Context()
    socket = context.socket(zmq.PULL)
    socket.bind(socket_address)
    start_time = time.time()

    log(f"Receiving traffic on {socket_address}...", log_enabled)
    while continuous or time.time() - start_time < duration:
        try:
            message = socket.recv(flags=zmq.NOBLOCK)
            queue.put((socket_id, time.time(), len(message)))
            log(f"Received {len(message)} bytes on {socket_address}", log_enabled)
        except zmq.Again:
            pass

    log(f"Traffic reception complete on {socket_address}.", log_enabled)

def collect_traffic(queue, num_sockets, duration):
    """Collect traffic data from all processes."""
    traffic_records = {i: [] for i in range(num_sockets)}

    start_time = time.time()
    while duration == 0 or time.time() - start_time < duration + 2:  # Extra buffer time
        while not queue.empty():
            socket_id, timestamp, size = queue.get()
            traffic_records[socket_id].append((timestamp, size))

    return traffic_records

def fill_gaps_and_sum(traffic_records, duration, interval=0.1):
    """Fill timestamp gaps and sum bandwidth from multiple sockets."""
    unified_times = np.arange(0, duration, interval) if duration > 0 else []
    socket_bandwidths = {}

    for socket_id, records in traffic_records.items():
        if not records:
            print(f"No traffic recorded for socket {socket_id}.")
            socket_bandwidths[socket_id] = np.zeros_like(unified_times) if duration > 0 else []
            continue

        # Sort and extract times and sizes
        records.sort(key=lambda x: x[0])
        times, sizes = zip(*records)
        times = np.array(times) - min(times)

        # Aggregate bandwidth for this socket
        bandwidth = np.zeros_like(unified_times) if duration > 0 else []
        for t, size in zip(times, sizes):
            if duration > 0:
                idx = np.searchsorted(unified_times, t)
                if idx < len(bandwidth):
                    bandwidth[idx] += size * 8 / 1_000_000  # Convert bytes to Mbps
            else:
                bandwidth.append((t, size * 8 / 1_000_000))

        socket_bandwidths[socket_id] = bandwidth

    total_bandwidth = (np.sum(np.array(list(socket_bandwidths.values())), axis=0) if duration > 0
                       else [sum(band[1] for band in socket_bandwidths.values())])
    return unified_times, socket_bandwidths, total_bandwidth

def monitor_bandwidth(unified_times, socket_bandwidths, total_bandwidth, save_path=None):
    """Plot bandwidth usage for each socket and the total."""
    plt.figure(figsize=(12, 8))

    for socket_id, bandwidth in socket_bandwidths.items():
        if len(bandwidth) > 0:
            plt.plot(unified_times, bandwidth, label=f"Socket {socket_id + 1} Bandwidth", lw=2)

    if len(total_bandwidth) > 0:
        plt.plot(unified_times, total_bandwidth, label="Total Bandwidth", lw=3, linestyle='--')

    plt.xlabel("Time (s)")
    plt.ylabel("Bandwidth (Mbps)")
    plt.title("Bandwidth Usage by Socket and Total")
    plt.legend()
    plt.grid(True)

    if save_path:
        plt.savefig(save_path)
        print(f"Plot saved to {save_path}")
    else:
        plt.show()

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Traffic Receiver")
    parser.add_argument("--config", type=str, required=True, help="Path to JSON file with instance configurations")
    parser.add_argument("--log", action="store_true", help="Enable logging")
    parser.add_argument("--plot", type=str, help="Path to save the plot (e.g., plot.png). If omitted, the plot will not be saved or shown.")
    parser.add_argument("--c", action="store_true", help="Run continuously ignoring the duration")
    args = parser.parse_args()

    with open(args.config, "r") as f:
        configs = json.load(f)

    queue = Queue()
    processes = []

    # Handle graceful stopping
    signal.signal(signal.SIGINT, signal_handler)

    for i, config in enumerate(configs):
        duration = config["duration"] if not args.c else float("inf")
        p = Process(target=receive_traffic, args=(config["host"], queue, i, duration, args.log, args.c))
        processes.append(p)
        p.start()

    # Wait for all processes to finish
    for p in processes:
        p.join()

    # Collect and process traffic
    traffic_records = collect_traffic(queue, len(configs), configs[0]["duration"] if not args.c else 0)
    if args.log:
        print("Traffic Records:", traffic_records)

    if not args.c and configs[0]["duration"] > 0:
        unified_times, socket_bandwidths, total_bandwidth = fill_gaps_and_sum(traffic_records, configs[0]["duration"])
        if args.plot:
            monitor_bandwidth(unified_times, socket_bandwidths, total_bandwidth, save_path=args.plot)
