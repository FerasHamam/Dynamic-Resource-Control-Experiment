import numpy as np
from scipy.fftpack import fft, ifft
import matplotlib.pyplot as plt
import os
from datetime import datetime, timedelta
import argparse

# Prediction number for file naming
# Parse command line arguments
parser = argparse.ArgumentParser(description='FFT and IFFT for network transfer rates prediction.')
parser.add_argument('--prediction_number', type=int, required=True, help='Prediction number for file naming')
args = parser.parse_args()

prediction_number = args.prediction_number
log_file = "log.txt"
prediction_file = "prediction.txt"

def read_transfer_rates(filename):
    """Read time and transfer rate values from a file."""
    times = []
    rates = []
    with open(filename, 'r') as file:
        for line in file:
            try:
                # Extract time and rate (adjust split character if needed)
                time, rate = line.strip().split(',')  # Assuming comma-separated values
                time = time.strip()  # Time in HH:MM:SS format
                rate = float(rate.split()[0])  # Extract rate in Mbps
                times.append(time)
                rates.append(rate)
            except ValueError:
                continue  # Skip lines that don't match the expected format
    
    if not rates:
        raise ValueError("No valid data in log file.")
    
    return np.array(times), np.array(rates)

def apply_fft(rate):
    """Apply FFT to detect periodic patterns."""
    n = len(rate)
    freq_components = fft(rate)
    magnitudes = np.abs(freq_components)[:n // 2]  # Get magnitude of first half frequencies
    return freq_components, magnitudes

def apply_ifft(freq_components, dominant_indices):
    """Reconstruct the signal using inverse FFT."""
    filtered_freq = np.zeros_like(freq_components, dtype=complex)
    filtered_freq[dominant_indices] = freq_components[dominant_indices]
    filtered_freq[-dominant_indices] = np.conjugate(freq_components[dominant_indices])  # Conjugate for symmetry
    reconstructed = np.real(ifft(filtered_freq))  # Take real part since the original was real-valued
    return reconstructed

def predict_future_rates(reconstructed_rate, extension_factor=1):
    """Extend the reconstructed signal to predict future rates."""
    future_length = int(len(reconstructed_rate) * extension_factor)  # Extend the signal length by a factor of 5
    future_rates = np.zeros(future_length)

    # Repeat the reconstructed rate pattern to fill the future rate array
    pattern_length = len(reconstructed_rate)
    for i in range(future_length):
        future_rates[i] = reconstructed_rate[i % pattern_length]  # Repeat the pattern

    return future_rates

def predict_future_times(times, extension_factor=1):
    """Predict future times by repeating the original pattern."""
    n = len(times)
    future_length = int(n * extension_factor)

    # Convert times to datetime objects
    time_objects = [datetime.strptime(t, '%H:%M:%S') for t in times]

    # Calculate time interval pattern
    time_diffs = [(time_objects[i+1] - time_objects[i]).total_seconds() for i in range(n - 1)]
    
    # Repeat the pattern to match future_length exactly
    future_times_seconds = []
    last_time = time_objects[-1]
    
    i = 0
    while len(future_times_seconds) < future_length:
        diff = time_diffs[i % len(time_diffs)]  # Loop through intervals
        last_time += timedelta(seconds=diff)
        future_times_seconds.append((last_time - time_objects[0]).total_seconds())
        i += 1

    return np.array(future_times_seconds[:future_length])  # Ensure correct size

def plot_results(times, rate, magnitudes, frequencies, reconstructed_rate, dominant_indices):
    """Plot results, showing time in seconds for predictions."""

    fig, axs = plt.subplots(3, 1, figsize=(10, 12))

    # Plot original transfer rate
    axs[0].plot(times, rate, label="Original Transfer Rate", color="blue")
    axs[0].set_title("Original Network Transfer Rate Over Time")
    axs[0].set_xlabel("Time")
    axs[0].set_ylabel("Rate (Mbps)")
    axs[0].legend()

    # Plot FFT magnitude
    axs[1].plot(frequencies[:len(frequencies)//2], magnitudes, label="FFT Magnitude", color="green")
    axs[1].scatter(frequencies[dominant_indices], magnitudes[dominant_indices], color='red', label="Dominant Frequencies")
    axs[1].set_title("FFT Magnitude of Transfer Rate")
    axs[1].set_xlabel("Frequency (Hz)")
    axs[1].set_ylabel("Magnitude")
    axs[1].legend()

    # Plot reconstructed rate
    time_reconstructed = np.linspace(0, (datetime.strptime(times[-1], '%H:%M:%S') - datetime.strptime(times[0], '%H:%M:%S')).total_seconds(), len(reconstructed_rate))
    axs[2].plot(time_reconstructed, reconstructed_rate, label="Reconstructed Rate", color="orange", linestyle="dashed")
    axs[2].set_title("Reconstructed Transfer Rate (Using IFFT)")
    axs[2].set_xlabel("Time (seconds)")
    axs[2].set_ylabel("Rate (Mbps)")
    axs[2].legend()

    # Plot predicted future rates
    # axs[3].plot(future_times_seconds, future_rates, label="Predicted Future Rates", color="red", linestyle="dotted")
    # axs[3].set_title("Predicted Future Rates")
    # axs[3].set_xlabel("Time (seconds)")
    # axs[3].set_ylabel("Rate (Mbps)")
    # axs[3].legend()

    plt.tight_layout()
    plt.savefig(f"fft_ifft_prediction_{prediction_number}.png")
    plt.show()

def write_predictions_to_file(times, rates, filename):
    time_reconstructed = np.linspace(0, (datetime.strptime(times[-1], '%H:%M:%S') - datetime.strptime(times[0], '%H:%M:%S')).total_seconds(), len(reconstructed_rate))
    """Write predicted times and rates into a file."""
    with open(filename, 'w') as f:
        f.write("Predicted Time (seconds), Predicted Rate (Mbps)\n")
        for time, rate in zip(time_reconstructed, rates):
            f.write(f"{time:.2f}, {rate:.2f}\n")


# Read data from log file
times, rate = read_transfer_rates(log_file)

# Apply FFT to detect dominant frequencies
freq_components, magnitudes = apply_fft(rate)
frequencies = np.fft.fftfreq(len(rate), d=1)

# Calculate threshold and detect dominant frequencies
threshold = np.mean(magnitudes) + 0.5 * np.std(magnitudes)

dominant_indices = np.where(magnitudes > threshold)[0]

# Apply IFFT to reconstruct the signal
reconstructed_rate = apply_ifft(freq_components, dominant_indices)

# Predict future rates based on the reconstructed signal (repeating the pattern)
# future_rates = predict_future_rates(reconstructed_rate)

# Predict future times (in seconds)
# future_times_seconds = predict_future_times(times)

# Write predictions to a file
write_predictions_to_file(times, reconstructed_rate, prediction_file)

# Plot results
plot_results(times, rate, magnitudes, frequencies, reconstructed_rate, dominant_indices)