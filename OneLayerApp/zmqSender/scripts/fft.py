import numpy as np
from scipy.fftpack import fft
import argparse
import matplotlib.pyplot as plt
import os


parser = argparse.ArgumentParser(description="Monitor and adjust tc settings based on JSON file.")
parser.add_argument("--prediction", type=int, nargs='+', required=True, help="Prediction Number #")
args = parser.parse_args()
prediction_number = args.prediction

script_dir = os.path.dirname(os.path.abspath(__file__))  # Get the directory of the script
log_file = os.path.join(script_dir, "..", "build", "log.txt")  
prediction_file = os.path.join(script_dir, "..", "build", "prediction.txt") 

def read_transfer_rates(filename):
    """Read time_taken:bytes_sent values from a file and compute transfer rates."""
    rates = []
    with open(filename, 'r') as file:
        for line in file:
            try:
                time_taken, bytes_sent = line.strip().split(':')
                time_taken = float(time_taken)
                bytes_sent = float(bytes_sent)
                
                if time_taken > 0 and bytes_sent > 0:
                    # Calculate rate in Mbps: (8 * bytes_sent) / (time_taken * 10^6)
                    rate = (8 * bytes_sent) / (time_taken * 1_000_000)
                    rates.append(rate)
            except ValueError:
                continue  # Skip lines that do not match the expected format
    
    # Remove the log file after reading
    os.remove(filename)
    
    if not rates:
        raise ValueError("No valid data in log file.")
    
    return np.array(rates)

def apply_fft(rate):
    """Apply FFT to detect periodic patterns."""
    rate = rate - np.mean(rate)
    n = len(rate)
    freq_components = fft(rate)
    magnitudes = np.abs(freq_components)[:n // 2]  # Get magnitude of first half frequencies
    return magnitudes

def calculate_threshold(frequencies):
    """Calculate dynamic threshold based on mean and standard deviation."""
    mean = np.mean(frequencies)
    stddev = np.std(frequencies)
    return mean + 1.5 * stddev

def detect_dominant_frequencies(frequencies, threshold):
    """Identify dominant frequency indices above threshold."""
    dominant_indices = np.where(frequencies > threshold)[0]
    return dominant_indices

def calculate_average_rate_fft(rate, dominant_indices):
    """
    Calculates the average rate based on dominant frequencies.

    Args:
        rate: A numpy array of transfer rates.
        dominant_indices: A list of indices of dominant frequencies.

    Returns:
        The average rate in Mbps.
    """
    if len(dominant_indices) == 0: 
        return np.mean(rate)  # If no dominant frequencies, use overall average
    
    dominant_rates = []
    for freq_index in dominant_indices:
        # Estimate period (inverse of frequency)
        period = len(rate) / freq_index
        # Calculate average rate within the estimated period
        start_idx = int(max(0, freq_index - period // 2))
        end_idx = int(min(freq_index + period // 2, len(rate) - 1))
        dominant_rates.append(np.mean(rate[start_idx:end_idx]))
    return np.mean(dominant_rates)

def plot_results(rate, frequencies, dominant_indices, threshold):
    """Plot network rate, FFT magnitude, and dominant frequencies."""
    fig, axs = plt.subplots(2, 1, figsize=(10, 6))
    
    # Plot original transfer rate data
    axs[0].plot(rate, label="Transfer Rate")
    axs[0].set_title("Network Transfer Rate Over Time")
    axs[0].set_xlabel("Time Steps (Chunk index)")
    axs[0].set_ylabel("Rate (Mbps)")
    axs[0].legend()
    
    # Plot FFT results
    axs[1].plot(frequencies, label="FFT Magnitude")
    axs[1].axhline(y=threshold, color='r', linestyle='--', label="Threshold")
    axs[1].scatter(dominant_indices, frequencies[dominant_indices], color='red', label="Dominant Frequencies")
    axs[1].set_title("FFT Analysis of Transfer Rate")
    axs[1].set_xlabel("Frequency Index")
    axs[1].set_ylabel("Magnitude")
    axs[1].legend()
    
    plt.tight_layout()
    plt.show()
    plt.savefig(f"fft{prediction_number}.png")

# Read transfer rate data from log.txt
rate = read_transfer_rates(log_file)

# Apply FFT
frequencies = apply_fft(rate)

# Calculate threshold and detect dominant frequencies
threshold = calculate_threshold(frequencies)
dominant_indices = detect_dominant_frequencies(frequencies, threshold)

# Calculate average rate using dominant frequencies
predicted_rate_fft = calculate_average_rate_fft(rate, dominant_indices)

# Calculate congestion (assuming LINK_BANDWIDTH is defined elsewhere)
congestion = (1.0 - (predicted_rate_fft / 200)) * 100

# Print the average rate and congestion
print("Average Rate (Mbps) using FFT:", predicted_rate_fft)
print(f"Congestion: {congestion:.0f}%")

with open(prediction_file, "w") as f:
    f.write(f"{congestion:.0f}\n")
    f.write(f"{predicted_rate_fft:.0f}\n")
# Plot results (optional)
plot_results(rate, frequencies, dominant_indices, threshold)