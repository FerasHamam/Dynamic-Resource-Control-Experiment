from abc import ABC,abstractmethod


class Predictor(ABC):
    """Abstract predictor that determines what action to take based on port data."""
    @abstractmethod
    def predict(self, port_data):
        """Return a prediction or decision based on the provided data."""
        pass