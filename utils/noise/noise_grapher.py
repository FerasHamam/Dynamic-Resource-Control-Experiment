import multiprocessing
import random
import time
import matplotlib.pyplot as plt
import numpy as np

# Constants for unit conversion
MB_TO_BITS = 10**6 / 8

def periodic_bandwidth_noise(bandwidth_mbps, period, interval, record_queue, instance_id):
    """Generate periodic bandwidth noise."""
    active_time = period / 2
    inactive_time = period - active_time
    start_time = time.time()
    total_data = 0

    while time.time() - start_time < interval:
        # Active phase
        record_queue.put((instance_id, time.time(), bandwidth_mbps))
        total_data += bandwidth_mbps * MB_TO_BITS * active_time
        time.sleep(active_time)
        
        # Inactive phase
        record_queue.put((instance_id, time.time(), 0))
        time.sleep(inactive_time)
    print(f"Periodic noise completed: {total_data / MB_TO_BITS:.2f} MB transferred.")

def non_periodic_bandwidth_noise(min_bandwidth_mbps, max_bandwidth_mbps, interval, record_queue, instance_id):
    """Generate non-periodic bandwidth noise."""
    start_time = time.time()
    total_data = 0

    while time.time() - start_time < interval:
        bandwidth_mbps = random.uniform(min_bandwidth_mbps, max_bandwidth_mbps)
        duration = random.uniform(1, 5)  # Random duration for traffic bursts
        record_queue.put((instance_id, time.time(), bandwidth_mbps))
        total_data += bandwidth_mbps * MB_TO_BITS * duration
        time.sleep(duration)

    print(f"Non-periodic noise completed: {total_data / MB_TO_BITS:.2f} MB transferred.")

def monitor_bandwidth(record_queue, total_duration, num_instances):
    """Monitor and graph bandwidth usage over time."""
    start_time = time.time()
    bandwidth_records = [[] for _ in range(num_instances)]

    while time.time() - start_time < total_duration:
        while not record_queue.empty():
            record = record_queue.get()
            bandwidth_records[record[0]].append(record[1:])
        time.sleep(0.1)

    # Combine all timestamps into a single sorted timeline
    unified_timeline = set()
    for instance_records in bandwidth_records:
        if instance_records:
            times, _ = zip(*instance_records)
            unified_timeline.update(times)

    unified_timeline = sorted(unified_timeline)
    total_bandwidth = []
    instance_bandwidths = [[] for _ in range(num_instances)]

    # Calculate bandwidth for each instance and the total
    for t in unified_timeline:
        total_at_t = 0
        for idx, instance_records in enumerate(bandwidth_records):
            # Find the most recent bandwidth record for this instance
            current_bw = 0
            for record_time, bw in instance_records:
                if record_time <= t:
                    current_bw = bw
                else:
                    break
            instance_bandwidths[idx].append(current_bw)
            total_at_t += current_bw
        total_bandwidth.append(total_at_t)

    # Plot each instance and the total
    plt.figure(figsize=(12, 8))

    for idx, bw_values in enumerate(instance_bandwidths):
        plt.step(unified_timeline, bw_values, where='post', label=f"Instance {idx + 1}", lw=2)

    plt.step(unified_timeline, total_bandwidth, where='post', label="Total Bandwidth", lw=3, linestyle='--')

    plt.xlabel("Time (s)")
    plt.ylabel("Bandwidth (Mbps)")
    plt.title("Bandwidth Usage by Instance and Total")
    plt.legend()
    plt.grid(True)
    plt.show()

if __name__ == "__main__":
    processes = []
    record_queue = multiprocessing.Queue()

    # Define configurations for each instance
    configs = [
        {"type": "periodic", "bandwidth": 30, "period": 10, "interval": 60},
        {"type": "periodic", "bandwidth": 20, "period": 20, "interval": 60},
    ]

    total_duration = max(config["interval"] for config in configs)

    # Start processes based on configurations
    for idx, config in enumerate(configs):
        if config["type"] == "periodic":
            p = multiprocessing.Process(
                target=periodic_bandwidth_noise,
                args=(config["bandwidth"], config["period"], config["interval"], record_queue, idx)
            )
        elif config["type"] == "non-periodic":
            p = multiprocessing.Process(
                target=non_periodic_bandwidth_noise,
                args=(config["min_bandwidth"], config["max_bandwidth"], config["interval"], record_queue, idx)
            )
        else:
            raise ValueError("Invalid type specified in configuration.")

        processes.append(p)
        p.start()

    # Monitor bandwidth usage
    monitor_bandwidth(record_queue, total_duration, len(configs))

    # Wait for all processes to finish
    for p in processes:
        p.join()
