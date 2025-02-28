import time
import numpy as np
import matplotlib.pyplot as plt
from . import Predictor

# ------------------------------------------------------------------------------
# FFT-Based Predictor with Cache and Visualization
# ------------------------------------------------------------------------------
class FftPredictor(Predictor):
    def __init__(self, window_seconds=180, sleep_sec=6):
        """
        :param window_seconds: Total window of history to consider (in seconds).
        :param sleep_sec: Sampling interval (in seconds).
        """
        self.window_seconds = window_seconds
        self.sleep_sec = sleep_sec
        self.required_history = window_seconds // sleep_sec  # number of samples needed
        self.cache_duration = window_seconds  # seconds to keep the cache valid
        self.prediction_cache = {}  # cache: key -> { 'prediction': ..., 'time': ... }
    
    def _cache_key(self, time_series):
        """
        Build a cache key from the time series.
        Since numpy arrays aren’t hashable, we convert to a tuple.
        """
        return tuple(time_series)
    
    def predict_future_with_ifft(self, time_series, n_predict):
        """
        Compute the FFT of the time series, apply a threshold to filter out low
        amplitude components, zero-pad the FFT result, and then compute the IFFT 
        to generate a prediction for n_predict future samples.
        
        :param time_series: 1D numpy array of recent data.
        :param n_predict: Number of future samples to predict.
        :return: Predicted future values as a numpy array.
        """
        n = len(time_series)
        fft_coeffs = np.fft.fft(time_series)
        threshold = 0.0 * np.max(np.abs(fft_coeffs))
        fft_coeffs[np.abs(fft_coeffs) < threshold] = 0

        N = n + n_predict
        padded_fft = np.zeros(N, dtype=complex)
        half = (n // 2) + 1
        padded_fft[:half] = fft_coeffs[:half]
        padded_fft[-(n - half):] = fft_coeffs[half:]
        extended_signal = np.fft.ifft(padded_fft) * (N / n)
        return np.real(extended_signal[n:])

    def predict(self, port_data):
        """
        Generate a prediction based on the input port_data.
        It expects port_data to be an array-like sequence of values (e.g. rx_bytes),
        and uses only the most recent required_history samples.
        
        :param port_data: A list or array of numeric data points.
        :return: A numpy array with the predicted future values.
        :raises ValueError: if insufficient history is provided.
        """
        if len(port_data) < self.required_history:
            raise ValueError(f"Not enough data to perform prediction (requires {self.required_history} samples).")
        
        # Use the most recent required_history samples.
        time_series = np.array(port_data[-self.required_history:])
        key = self._cache_key(time_series)
        current_time = time.time()
        
        # Use cached prediction if still valid.
        if key in self.prediction_cache:
            cached = self.prediction_cache[key]
            if current_time - cached['time'] < self.cache_duration:
                return cached['prediction']
        
        # Compute a new prediction.
        prediction = self.predict_future_with_ifft(time_series, self.required_history)
        self.prediction_cache[key] = {'prediction': prediction, 'time': current_time}
        return prediction

    def plot_prediction(self, port_data, filename="prediction.png"):
        """
        Generate a visualization of the prediction along with the historical data,
        and store it as an image file.
        
        :param port_data: A list or array of numeric data points.
        :param filename: Output filename for the visualization.
        """
        # Ensure we have enough history and compute prediction.
        prediction = self.predict(port_data)
        historical_data = np.array(port_data[-self.required_history:])
        
        # Create time indices for the historical data and the prediction.
        t_hist = np.arange(len(historical_data))
        t_pred = np.arange(len(historical_data), len(historical_data) + len(prediction))
        
        # Create the plot.
        plt.figure(figsize=(10, 6))
        plt.plot(t_hist, historical_data, label="Historical Data", marker='o')
        plt.plot(t_pred, prediction, label="Predicted Future", marker='x')
        plt.title("FFT-Based Prediction Visualization")
        plt.xlabel("Sample Number")
        plt.ylabel("Value")
        plt.legend()
        plt.grid(True)
        plt.savefig(filename)
        plt.close()
        print(f"Saved prediction visualization to {filename}")

# ------------------------------------------------------------------------------
# Example Usage
# ------------------------------------------------------------------------------
if __name__ == "__main__":
    # Simulate some port data (for example, rx_bytes over time)
    # In a real scenario, this data could come from your data gatherer.
    simulated_data = [1000 + 50 * np.sin(0.4 * i) for i in range(300)]
    predictor = FftPredictor(window_seconds=300, sleep_sec=1)  # Adjust parameters as needed
    
    try:
        prediction = predictor.predict(simulated_data)
        print("Predicted future values:", prediction)
        # Generate and store the visualization.
        predictor.plot_prediction(simulated_data, filename="fft_prediction.png")
    except ValueError as ve:
        print("Error:", ve)
