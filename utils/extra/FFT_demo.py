import numpy as np
import matplotlib.pyplot as plt

# -------------------------
# Generate Dummy Data
# -------------------------
Fs = 10            # Sampling frequency in Hz
T = 10              # Duration of original signal in seconds
n = int(Fs * T)     # Number of samples in the original window
t = np.arange(n) / Fs

# Create a signal composed of two sinusoids with different frequencies and phases.
# The frequencies (1 Hz and 3 Hz) are chosen arbitrarily.
signal = np.sin(2 * np.pi * 1 * t) + 0.5 * np.sin(2 * np.pi * 3 * t + 0.5)

# Optionally, add a little noise.
signal += 0.1 * np.random.randn(n)

# Plot the original signal.
plt.figure(figsize=(12, 5))
plt.plot(t, signal, label="Original Signal")
plt.xlabel("Time (s)")
plt.ylabel("Amplitude")
plt.title("Original Signal (Duration = {} s)".format(T))
plt.legend()
plt.grid(True)
plt.show()

# -------------------------
# FFT Prediction via Zero-Padding + IFFT
# -------------------------
# Compute the FFT of the original signal.
fft_coeffs = np.fft.fft(signal)

# Set the number of extra samples to predict.
n_predict = n      # e.g., predict an extra 10 seconds (same number of samples as original)
N = n + n_predict  # Total length after zero-padding

# Prepare a zero-padded FFT array.
padded_fft = np.zeros(N, dtype=complex)

# For real signals, the FFT is symmetric.
# Determine the index up to which the positive frequency components are stored.
half = (n // 2) + 1

# Copy the positive frequency components.
padded_fft[:half] = fft_coeffs[:half]
# Copy the negative frequency components to preserve conjugate symmetry.
padded_fft[-(n - half):] = fft_coeffs[half:]

# Compute the inverse FFT on the padded FFT.
# The scaling factor (N/n) preserves the amplitude.
extended_signal = np.fft.ifft(padded_fft) * (N / n)
extended_signal = np.real(extended_signal)  # Should be real if symmetry is preserved

# Separate the predicted part from the original data.
predicted_signal = extended_signal[n:]
t_pred = np.arange(n, N) / Fs  # Time axis for the predicted part

# -------------------------
# Plotting the Results
# -------------------------
plt.figure(figsize=(12, 5))
plt.plot(t, signal, label="Original Signal")
plt.plot(t_pred, predicted_signal, 'r--', label="Predicted Signal (via IFFT)")
plt.xlabel("Time (s)")
plt.ylabel("Amplitude")
plt.title("FFT Prediction using Zero-Padding and IFFT")
plt.legend()
plt.grid(True)
plt.show()

# Optionally, plot the entire extended signal (original + predicted)
t_full = np.arange(N) / Fs
plt.figure(figsize=(12, 5))
plt.plot(t_full, extended_signal, label="Extended Signal (Periodic Extension)")
plt.xlabel("Time (s)")
plt.ylabel("Amplitude")
plt.title("Extended Signal: Original + Predicted")
plt.legend()
plt.grid(True)
plt.show()
