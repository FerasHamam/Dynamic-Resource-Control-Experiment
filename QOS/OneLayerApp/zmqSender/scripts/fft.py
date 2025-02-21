import numpy as np
import scipy.fftpack as fft
import matplotlib.pyplot as plt
from datetime import datetime

# Read and parse the log file
def read_txt_file(filename):
    times, rates = [], []
    base_time = None

    with open(filename, 'r') as file:
        for line in file:
            parts = line.strip().split(',')
            if len(parts) == 2:
                time_str, rate_str = parts
                try:
                    rate = float(rate_str.replace(" Mbps", ""))  # Remove " Mbps" before conversion
                    current_time = datetime.strptime(time_str, "%H:%M:%S")

                    if base_time is None:
                        base_time = current_time  # Set first timestamp as reference

                    if current_time < base_time:
                      # Midnight rollover: add 86400 seconds (one day)
                        time_seconds = int((current_time - base_time).total_seconds() + 86400)
                    else:
                        time_seconds = int((current_time - base_time).total_seconds())                    
                    times.append(time_seconds)
                    rates.append(rate)

                except ValueError:
                    print(f"Skipping invalid line: {line.strip()}")  # Debugging print

    if not rates:
        raise ValueError("Error: No valid numerical data found in file!")

    return np.array(times), np.array(rates)

# Adjust first and last values of each measurement section
def adjust_measurements(rates):
    nonzero_indices = np.where(rates > 0)[0]
    if len(nonzero_indices) == 0:
        return rates  # No nonzero values to adjust

    i = 0
    while i < len(nonzero_indices):
        start = nonzero_indices[i]
        while i + 1 < len(nonzero_indices) and nonzero_indices[i + 1] == nonzero_indices[i] + 1:
            i += 1
        end = nonzero_indices[i]

        if start + 1 < len(rates):
            rates[start] = rates[start + 1]

        # Adjust last value (use previous-to-last value)
        if end - 1 >= 0:
            rates[end] = rates[end - 1]

        i += 1  # Move to next measurement section

    return rates

def apply_fft_filtering_dynamic(rates, std_factor=1.0):
    n = len(rates)
    if n == 0:
        return rates  # Avoid FFT on empty array

    # Perform FFT
    freq_domain = fft.fft(rates)
    magnitudes = np.abs(freq_domain)  # Compute magnitude spectrum

    # Compute thresholds based on standard deviation
    mean_magnitude = np.mean(magnitudes)
    std_magnitude = np.std(magnitudes)

    threshold_25 = mean_magnitude + 0.25 * std_magnitude
    threshold_50 = mean_magnitude + 0.50 * std_magnitude
    threshold_75 = mean_magnitude + 0.75 * std_magnitude

    # Apply filtering based on different thresholds
    def filter_freq(threshold):
        filtered_freq = freq_domain.copy()
        filtered_freq[magnitudes < threshold] = 0  # Remove frequencies below threshold
        return np.real(fft.ifft(filtered_freq))

    # Generate filtered versions based on thresholds
    fft_25 = np.clip(filter_freq(threshold_25), 0, None)
    # fft_50 = np.clip(filter_freq(threshold_50), 0, None)
    # fft_75 = np.clip(filter_freq(threshold_75), 0, None)

    return fft_25, mean_magnitude, std_magnitude

def write_predictions_to_file(times, rates, filename):
    """Write predicted times and rates into a file."""
    with open(filename, 'w') as f:
        f.write("Predicted Time (seconds), Predicted Rate (Mbps)\n")
        for time, rate in zip(times, rates):
            f.write(f"{time:.2f}, {rate:.2f}\n")

filename = "log.txt"
try:
    times, rates = read_txt_file(filename)
    adjust_measurements(rates)
    rates = np.where(rates == 0, 200, rates)
except ValueError as e:
    print(e)

fft_25, mean_mag, std_mag = apply_fft_filtering_dynamic(rates)
times = 1800 + times

plt.figure(figsize=(16, 6))
plt.plot(times, rates, label="Reconstructed Rate | Original", linestyle="solid")
plt.plot(times, fft_25, label="Reconstructed Rate | Threshold = Mean + 25% * std", linestyle="dashed")

plt.xlabel("Time (Seconds)")
plt.ylabel("Rate")
plt.xticks(np.arange(1800, max(times) + 100, step=200))  # Show ticks every 200 seconds
plt.yticks(np.arange(0, 450, step=50))

plt.title("Rate")

plt.legend(loc="lower center", bbox_to_anchor=(0.5, 1.05), ncol=2)
plt.tight_layout()
plt.show()
plt.savefig("fft_25.png")
write_predictions_to_file(times, fft_25, "predictions.txt")