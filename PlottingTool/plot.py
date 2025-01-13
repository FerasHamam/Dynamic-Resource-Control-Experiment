import argparse
import matplotlib.pyplot as plt
import numpy as np

def parse_bandwidth_file(file_path):
    """
    Parse a file containing lines of `seconds_passed, bytes_transferred`.
    Accumulate time to simulate progression and return steps, times, and cumulative times.
    """
    cumulative_time = 0.0
    times = []
    cumulative_times = []
    bytes_transferred = []

    with open(file_path, 'r') as file:
        for line in file:
            try:
                seconds_passed, bytes_data = map(float, line.split(","))
                cumulative_time += seconds_passed
                cumulative_times.append(cumulative_time)
                times.append(seconds_passed)
                bytes_transferred.append(bytes_data)
            except ValueError:
                print(f"Skipping invalid line: {line.strip()}")
    return cumulative_times, times, bytes_transferred

def calculate_bandwidth(bytes_transferred, times):
    """
    Calculate bandwidth in Mibps for each entry.
    Convert bytes to bits and divide by elapsed seconds.
    """
    bandwidths = []
    for i in range(len(bytes_transferred)):
        b = bytes_transferred[i]
        t = times[i]
        if t > 0:
            bandwidth = (b * 8) / (t * (10**6))  # Mibps calculation
        else:
            bandwidth = 0
        bandwidths.append(bandwidth)
    return bandwidths

def calculate_statistics(data, label):
    """
    Calculate and print statistics (max, min, avg, median) for the data.
    """
    max_value = max(data)
    min_value = min(data)
    avg_value = np.mean(data)
    median_value = np.median(data)

    print(f"Statistics for {label}:")
    print(f"  Max Bandwidth: {max_value:.2f} Mbps")
    print(f"  Min Bandwidth: {min_value:.2f} Mbps")
    print(f"  Average Bandwidth: {avg_value:.2f} Mbps")
    print(f"  Median Bandwidth: {median_value:.2f} Mbps")
    print("-" * 40)

    return max_value, min_value, avg_value, median_value

def plot_bandwidth(experiments, legends, file_size=None):
    """
    Generate a line graph for bandwidth across experiments.
    """
    plt.figure(figsize=(12, 6))

    for exp, legend in zip(experiments, legends):
        file_path = f"./{exp}/timing.txt"
        cumulative_times, times, bytes_transferred = parse_bandwidth_file(file_path)
        bandwidths = calculate_bandwidth(bytes_transferred, times)
        calculate_statistics(bandwidths, legend)
        plt.plot(cumulative_times, bandwidths, label=legend)

    plt.title("Bandwidth Across Time")
    plt.xlabel("Cumulative Time (s)")
    plt.ylabel("Bandwidth (Mbps)")
    plt.legend(loc="lower right")
    plt.grid(True)
    plt.savefig("bandwidth_plot.png", dpi=300)
    print("Plot saved as 'bandwidth_plot.png'")
    plt.show()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Plot bandwidth data from multiple experiments.")
    parser.add_argument(
        "--experiments",
        type=str,
        required=True,
        nargs="+",
        help="List of experiment names (e.g., C2 D2).",
    )
    parser.add_argument(
        "--legends",
        type=str,
        required=True,
        nargs="+",
        help="List of legend titles for each experiment (e.g., 'Dataset C2' 'Dataset D2').",
    )

    args = parser.parse_args()

    # Validate inputs
    if len(args.experiments) != len(args.legends):
        print("Error: Number of experiments must match the number of legend titles.")
        exit(1)  # Exit with an error status

    # Call plot_bandwidth for the provided experiments
    plot_bandwidth(args.experiments, args.legends)