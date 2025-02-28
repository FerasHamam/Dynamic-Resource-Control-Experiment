from . import Predictor


# Concrete predictor implementation using linear extrapolation.
class NextSecondPredictor(Predictor):
    def predict(self, port_data):
        """
        Predict the next second's value based on the last 5 seconds.
        Uses a simple linear extrapolation: 
            prediction = last_value + (average difference over 4 intervals)
        """
        if len(port_data) < 5:
            raise ValueError("Need at least 5 data points to predict.")
        
        # Consider the last 5 data points
        recent_data = port_data[-5:]
        # Calculate differences between consecutive data points
        differences = [recent_data[i+1] - recent_data[i] for i in range(len(recent_data)-1)]
        # Calculate the average difference
        avg_diff = sum(differences) / len(differences)
        # Prediction is the last known value plus the average difference
        prediction = recent_data[-1] + avg_diff
        return prediction