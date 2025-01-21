import zmq
import time
import random
import json
import signal
import sys
from multiprocessing import Process

def log(message, enabled):
    """Log message if logging is enabled."""
    if enabled:
        print(message)

def signal_handler(signal, frame):
    """Handle termination signal to exit gracefully."""
    print("\nStopping the sender application...")
    sys.exit(0)

def send_traffic(config, log_enabled, continuous):
    """Send traffic to a specific address based on the configuration."""
    context = zmq.Context()
    socket = context.socket(zmq.PUSH)
    socket.connect(config["host"])

    log(f"Sender connected to {config['host']}", log_enabled)

    start_time = time.time()
    while continuous or time.time() - start_time < config["duration"]:
        if config["type"] == "periodic":
            message_size = int(config["bandwidth"] * 125000)  # Mbps to bytes/second
            active_time = config["period"] / 2
            inactive_time = config["period"] - active_time

            # Active phase
            active_start = time.time()
            while time.time() - active_start < active_time:
                socket.send(b"x" * message_size)
                log(f"Sent {message_size} bytes to {config['host']}", log_enabled)
                time.sleep(0.1)

            # Inactive phase
            time.sleep(inactive_time)
        elif config["type"] == "non-periodic":
            min_bandwidth = config["min_bandwidth"]
            max_bandwidth = config["max_bandwidth"]
            bandwidth = random.uniform(min_bandwidth, max_bandwidth)
            message_size = int(bandwidth * 125000)  # Mbps to bytes/second
            burst_duration = random.uniform(1, 5)

            # Burst traffic
            burst_start = time.time()
            while time.time() - burst_start < burst_duration:
                socket.send(b"x" * message_size)
                log(f"Sent {message_size} bytes to {config['host']}", log_enabled)
                time.sleep(0.1)

if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="Traffic Sender")
    parser.add_argument("--config", type=str, required=True, help="Path to JSON file with instance configurations")
    parser.add_argument("--log", action="store_true", help="Enable logging")
    parser.add_argument("--c", action="store_true", help="Run continuously ignoring the duration")
    args = parser.parse_args()

    with open(args.config, "r") as f:
        configs = json.load(f)

    # Handle graceful stopping
    signal.signal(signal.SIGINT, signal_handler)

    processes = []
    for config in configs:
        p = Process(target=send_traffic, args=(config, args.log, args.c))
        processes.append(p)
        p.start()

    for p in processes:
        p.join()
