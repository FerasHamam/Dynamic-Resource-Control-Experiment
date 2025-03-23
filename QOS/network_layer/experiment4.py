import time
from typing import Dict, List
import numpy as np
from utils.data_gatherer import DataGatherer
from utils.predictors.fft_predictor import FftPredictor
from utils.actions.tc_queue_action import TCQueueAction

def run_experiment() -> None:
    """
    Gathers data from specified network ports for a set duration and uses FFT predictors
    to make predictions based on the summed data from all ports except one.
    Then applies TC actions based on the average predicted bandwidth for the next second.
    """
    # Network configuration
    ports: List[str] = ["enp8s0", "enp7s0", "enp10s0", "enp11s0"]  # Example port names; adjust as needed
    switch_port = "enp9s0"  # Port to apply TC actions to
    
    # Initialize TC action handler
    action = TCQueueAction()
    action.setup_tc(switch_port)
    
    # Initialize data gatherers
    gatherers: Dict[str, DataGatherer] = {port: DataGatherer(port, max_seconds=180) for port in ports}

    # Start data gathering for all ports
    for gatherer in gatherers.values():
        gatherer.start()
    
    # Allow data gathering to run for a sufficient duration initially
    time.sleep(1200)  # Wait for some initial data
    
    try:
        # Main experiment loop
        while True:
            
            # Sum data for all ports except one
            excluded_port = "enp8s0"
            summed_data = None
            for port in ports:
                if port != excluded_port:
                    port_data = gatherers[port].get_data()
                    if summed_data is None:
                        summed_data = np.array(port_data)
                    else:
                        summed_data += np.array(port_data)

            if summed_data is None or len(summed_data) == 0:
                print("Error: No data available for prediction.")
                continue

            # Initialize FFT predictor
            predictor = FftPredictor(window_seconds=60, sleep_sec=1)

            # Calculate prediction for the summed data
            try:
                # Get prediction for future values
                prediction = predictor.predict(summed_data)
                step_size = 1 
                for i in range(0, len(prediction), step_size):
                    next_second_prediction = prediction[i:i+step_size] if len(prediction) >= i+step_size else prediction[i:]
                    
                    if len(next_second_prediction) == 0:
                        print("Error: Prediction length is zero, resetting pointer and creating new prediction.")
                        break
                    
                    # Calculate average predicted bandwidth for the next second
                    avg_predicted_bandwidth = np.mean(next_second_prediction)
                    print(f"Average predicted bandwidth for next second: {avg_predicted_bandwidth} bytes/sec")
                    
                    # Apply TC action based on prediction
                    # Convert bytes/sec to Mbit/sec (8 bits per byte, 1,000,000 bits per Mbit)
                    bandwidth_mbits = (avg_predicted_bandwidth * 8) / 1000000
                    
                    # Define max_bandwidth and coefficients for the linear equation
                    max_bandwidth = 370  # Example max bandwidth in Mbit/sec, adjust as needed
                    k1 = 1  # Example coefficient, adjust as needed
                    b = 0  # Example intercept, adjust as needed

                    # Apply TC action with appropriate ceiling based on prediction
                    if bandwidth_mbits <= max_bandwidth / 2:
                        assigned_bandwidth = bandwidth_mbits
                    else:
                        assigned_bandwidth = k1 * bandwidth_mbits / max_bandwidth + b

                    action.update_tc_class_20(switch_port, assigned_bandwidth)
                    
                    # Optionally generate a visualization periodically
                    if np.random.random() < 0.1:  # 10% chance to create a plot
                        predictor.plot_prediction(summed_data, filename=f"fft_prediction_{int(time.time())}.png")
                    
                    # Sleep before next prediction cycle
                    time.sleep(1200)  # Adjust as needed for your use case
                
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