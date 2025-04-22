import time
from typing import Dict, List
import numpy as np
from utils.data_gatherer import DataGatherer
from utils.predictors.fft_predictor import FftPredictor
from utils.actions.tc_queue_action import TCQueueAction
from utils.predictors.next_second_predictor import NextSecondPredictor

def run_experiment() -> None:
    """
    Gathers data from specified network ports for a set duration and uses FFT predictors
    to make predictions based on the summed data from all ports except one.
    Then applies TC actions based on the average predicted bandwidth for the next second.
    """
    # Network configuration
    ports: List[str] = ["enp8s0", "enp7s0", "enp10s0", "enp11s0"]  # Example port names; adjust as needed
    switch_port = "enp9s0"  # Port to apply TC actions to
    EXCLUDED_PORT = "enp8s0"
    GATHERING_WINDOW = 1200
    STEP_SIZE = 60
    coefficient = 0.5   # Coefficient for bandwidth calculation [0,1]

    # Initialize TC action handler
    action = TCQueueAction()
    action.setup_tc(switch_port)
    
    # Initialize data gatherers
    gatherers: Dict[str, DataGatherer] = {port: DataGatherer(port, max_seconds=GATHERING_WINDOW) for port in ports}
    
    # Initialize FFT predictor
    fft_predictor = FftPredictor(window_seconds=GATHERING_WINDOW, sleep_sec=1)
    nsp_predictor = NextSecondPredictor()

    # Start data gathering for all ports
    for gatherer in gatherers.values():
        gatherer.start()
    
    # Allow data gathering to run for a sufficient duration initially
    time.sleep(GATHERING_WINDOW+2)  # Wait for some initial data
    
    try:
        
        # Main experiment loop
        while True:
            # Sum data for all ports except one
            summed_data = None
            for port in ports:
                if port != EXCLUDED_PORT:
                    port_data = gatherers[port].get_data()
                    if summed_data is None:
                        summed_data = np.array(port_data)
                    else:
                        summed_data += np.array(port_data)

            if summed_data is None or len(summed_data) == 0:
                print("Error: No data available for prediction.")
                continue


            # Calculate prediction for the summed data
            try:
                # Get prediction for future values
                prediction = fft_predictor.predict(summed_data)
                for i in range(0, len(prediction), STEP_SIZE):
                    next_second_prediction = prediction[i:i+STEP_SIZE] if len(prediction) >= i+STEP_SIZE else prediction[i:]
                    
                    if len(next_second_prediction) == 0:
                        print("Error: Prediction length is zero, resetting pointer and creating new prediction.")
                        break
                    
                    # Calculate average predicted bandwidth for the next second
                    avg_predicted_bandwidth = np.mean(next_second_prediction)
                    print(f"Average predicted bandwidth for next second: {avg_predicted_bandwidth} bytes/sec")
                    
                    # Apply TC action based on prediction
                    # Convert bytes/sec to Mbit/sec (8 bits per byte, 1,000,000 bits per Mbit)
                    bandwidth_mbits = (avg_predicted_bandwidth * 8) / 1000000
                    
                    # Apply TC action with appropriate ceiling based on prediction
                    assigned_bandwidth = coefficient * bandwidth_mbits 

                    action.update_tc_class_20(switch_port, assigned_bandwidth)
                    
                    # Optionally generate a visualization periodically
                    if np.random.random() < 0.1:  # 10% chance to create a plot
                        fft_predictor.plot_prediction(summed_data, filename=f"fft_prediction_{int(time.time())}.png")
                      
                    # Get the last 5 seconds of data for the excluded port
                    start_time = time.time()
                    end_time = start_time
                    while STEP_SIZE - (end_time - start_time) > 0.1:
                        ex_port_data = gatherers[EXCLUDED_PORT].get_data()
                        if len(ex_port_data) < 5:
                            print(f"Error: No data available for excluded port {EXCLUDED_PORT}.")
                        
                        nsp_result = nsp_predictor.predict(ex_port_data[-5:])
                        if nsp_result <= 0:
                            print(f"Next second prediction for excluded port {EXCLUDED_PORT}: {nsp_result} bytes/sec")
                            action.update_tc_class_20(switch_port, 400)
                        else:
                            action.update_tc_class_20(switch_port, assigned_bandwidth)
                        time.sleep(5)
                        end_time = time.time()
                    
                    # Sleep before next prediction cycle                
            except ValueError as ve:
                print(f"Error predicting for summed data: {ve}")
                
    except KeyboardInterrupt:
        print("Experiment interrupted by user.")
    finally:
        # Stop data gathering when done
        for gatherer in gatherers.values():
            gatherer.stop()
        print("Experiment completed.")

if __name__ == "__main__":
    run_experiment()