import time
from utils.data_gatherer import DataGatherer
from utils.predictors.next_second_predictor import NextSecondPredictor
from utils.actions.tc_queue_action import TCQueueAction

if __name__ == "__main__":
    # Replace 'enp7s0' with your network interface
    interface = "s1-eth1"
    switch_port = "s1-eth3"
    
    # Initialize data gatherer and predictor
    gatherer = DataGatherer(interface, max_seconds=25)
    predictor = NextSecondPredictor()
    action = TCQueueAction()
    action.setup_tc(switch_port)
    
    # Start data collection
    gatherer.start()
    # time.sleep(25)
    
    try:        
        while True:
            data = gatherer.get_data_averaged(5)        
            if len(data) >= 5:
                print (f"Data: {data}")
                prediction = predictor.predict(data)
                print(f"Predicted RX bytes for the next second: {prediction}")
                if prediction < 200:
                    action.update_tc_class_20(switch_port, 400)
                else:
                    action.update_tc_class_20(switch_port, 200)
            else:
                print("Not enough data to predict.")
            time.sleep(1)
        
    finally:
        # Stop data gathering when done
        gatherer.stop()
