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
                    rate = float(rate_str.replace(" Mbps", ""))
                    current_time = datetime.strptime(time_str, "%H:%M:%S")

                    if base_time is None:
                        base_time = current_time

                    if current_time < base_time:
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

def replace_zeros_with_preceding_average(rates):
    """
    Replace zeros with the average of the preceding non-zero sequence.
    Ensures that newly replaced values don't influence subsequent replacements.
    
    Parameters:
    - rates: Array of rate values
    
    Returns:
    - Processed array with zeros replaced
    """
    # Make two copies - one to hold the original values, one for results
    original = rates.copy()
    result = rates.copy()
    n = len(rates)
    
    # First pass: identify all zero sequences and their replacements
    zero_sequences = []
    i = 0
    while i < n:
        if original[i] == 0:
            # Found a zero, now find the sequence
            zero_start = i
            
            # Find end of zero sequence
            while i < n and original[i] == 0:
                i += 1
            zero_end = i - 1
            
            # Store the zero sequence range
            zero_sequences.append((zero_start, zero_end))
        else:
            i += 1
    
    # Second pass: calculate replacements and apply them
    for zero_start, zero_end in zero_sequences:
        # Find the preceding sequence of non-zeros in the original array
        preceding_values = []
        j = zero_start - 1
        
        # Go backward until we hit another zero or the beginning of the array
        while j >= 0 and original[j] > 0:
            preceding_values.append(original[j])
            j -= 1
        
        # If we found preceding values, replace zeros with their average
        if preceding_values:
            avg_value = sum(preceding_values) / len(preceding_values)
            result[zero_start:zero_end+1] = avg_value
        # If no preceding values (zeros at beginning), leave as is
    
    return result

def apply_fft_filtering_dynamic(rates, std_factor=1.0):
    n = len(rates)
    if n == 0:
        return rates

    freq_domain = fft.fft(rates)
    magnitudes = np.abs(freq_domain)

    mean_magnitude = np.mean(magnitudes)
    std_magnitude = np.std(magnitudes)
    threshold_25 = np.max(magnitudes) * 0.1
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

def analyze_and_plot(input_filename, output_filename):
    try:
        # Read data
        times, rates = read_txt_file(input_filename)
        
        # Replace zeros with the average of preceding non-zero sequences
        rates_replaced = replace_zeros_with_preceding_average(rates)
        
        print(f"Original data points: {len(rates)}")
        print(f"Zero values found: {np.sum(rates == 0)}")
        
        # Apply FFT filtering
        fft_25, mean_mag, std_mag = apply_fft_filtering_dynamic(rates_replaced)
        
        # Add offset to times if needed
        times_with_offset = 1800 + times
        
        # Create visualization
        plt.figure(figsize=(16, 8))
        
        # Plot original data
        plt.plot(times_with_offset, rates, 'o-', alpha=0.4, label="Original Rate (with zeros)", markersize=4)
        
        # Plot data with zeros replaced
        plt.plot(times_with_offset, rates_replaced, 'o-', alpha=0.6, label="Rate with zeros replaced", markersize=4)
        
        # Plot filtered data
        plt.plot(times_with_offset, fft_25, label="FFT Filtered (Threshold = Mean + 25% * std)", linewidth=2)
        
        plt.xlabel("Time (Seconds)")
        plt.ylabel("Rate (Mbps)")
        plt.xticks(np.arange(min(times_with_offset), max(times_with_offset) + 100, step=200))
        
        # Adjust y-axis if needed
        max_rate = max(np.max(rates_replaced), np.max(fft_25))
        plt.yticks(np.arange(0, max_rate + 50, step=50))
        
        plt.title("Network Rate Analysis with Zero Replacement")
        plt.legend(loc="lower center", bbox_to_anchor=(0.5, 1.05), ncol=2)
        plt.grid(True, linestyle='--', alpha=0.7)
        plt.tight_layout()
        
        # Save figure
        plt.savefig(f"{output_filename}.png")
        
        # Save predictions to file
        write_predictions_to_file(900 + times, fft_25, "predictions.txt")
        
        print(f"Analysis complete. Results saved to {output_filename}.png and {output_filename}.txt")
        
    except Exception as e:
        print(f"Error during analysis: {e}")

if __name__ == "__main__":
    analyze_and_plot("log.txt", "predictions")