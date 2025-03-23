#############################################
# Experiment 1: Predicting the next second
# Description: This experiment predicts the next second's bandwidth based on the last 5 seconds of data.
#              It uses a simple linear extrapolation to predict the next second's value.
#              The prediction is used to adjust the TC queue ceiling for a switch port.
#              The experiment runs indefinitely until manually stopped.
#              The experiment requires the DataGatherer, NextSecondPredictor, and TCQueueAction classes.
#              The DataGatherer class is used to collect network data, the NextSecondPredictor class is used to predict
#              the next second's bandwidth, and the TCQueueAction class is used to adjust the TC queue ceiling.
# Experiment setup: Run the experiment1.py script.
# Expected behavior: The experiment starts by collecting network data for 25 seconds.
#                    The experiment then predicts the next second's bandwidth based on the last 5 seconds of data.
#                    The prediction is used to adjust the TC queue ceiling for a switch port.
#                    The experiment continues to predict the next second's bandwidth and adjust the TC queue ceiling indefinitely.
# Additional notes: The experiment can be stopped by pressing Ctrl+C.
#############################################

import time
from utils.data_gatherer import DataGatherer
from utils.predictors.next_second_predictor import NextSecondPredictor
from utils.actions.tc_queue_action import TCQueueAction

if __name__ == "__main__":
    interface = "enp8s0"
    switch_port = "enp9s0"
    
    # Initialize data gatherer and predictor
    gatherer = DataGatherer(interface, max_seconds=5)
    predictor = NextSecondPredictor()
    action = TCQueueAction()
    action.setup_tc(switch_port)
    
    # Start data collection
    gatherer.start()
    # time.sleep(25)
    
    try:        
        while True:
            data = gatherer.get_data()        
            if len(data) >= 5:
                print (f"Data: {data}")
                prediction = predictor.predict(data)
                print(f"Predicted RX bytes for the next second: {prediction}")
                if prediction == 0:
                    action.update_tc_class_20(switch_port, 400)
                else:
                    action.update_tc_class_20(switch_port, 200)
            else:
                print("Not enough data to predict.")
            time.sleep(1)
        
    finally:
        # Stop data gathering when done
        gatherer.stop()
