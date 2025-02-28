import time

from utils.data_gatherer import DataGatherer
from utils.predictors.next_second_predictor import NextSecondPredictor

if __name__ == "__main__":
    # Replace 'enp7s0' with your network interface
    interface = "s1-eth1"
    
    # Initialize data gatherer and predictor
    gatherer = DataGatherer(interface, max_seconds=5)
    predictor = NextSecondPredictor()
    
    # Start data collection
    gatherer.start()
    time.sleep(4)
    
    try:
        for i in range(10):
            # Get the collected data
            data = gatherer.get_data()            
            if len(data) >= 5:
                prediction = predictor.predict(data)
                print("Predicted rx_bytes for the next second:", prediction)
            else:
                print("Not enough data to predict.")
            time.sleep(1)
    finally:
        # Stop data gathering when done
        gatherer.stop()
