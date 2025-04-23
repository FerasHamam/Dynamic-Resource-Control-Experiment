import time
from typing import Dict, List
import numpy as np
import csv
import os
from datetime import datetime
from utils.data_gatherer import DataGatherer
from utils.predictors.fft_predictor import FftPredictor
from utils.actions.tc_queue_action import TCQueueAction

def run_experiment() -> None:
    """
    Gathers data from specified network ports and uses FFT predictor
    to determine if an application is on/off based on prediction patterns.
    All predictions are logged with timestamps, and TC actions are disabled.
    """
    # Network configuration
    ports: List[str] = ["enp8s0", "enp7s0", "enp10s0", "enp11s0"]  # Example port names; adjust as needed
    SWITCH_PORT = "enp9s0"  # Port to apply TC actions to (not used in this run)
    EXCLUDED_PORT = "enp8s0"  # Port to monitor for application activity
    GATHERING_WINDOW = 1200
    STEP_SIZE = 60
    MAX_BANDWIDTH = 370  # Example max bandwidth in Mbit/sec
    
    # Threshold for determining if application is on (in bytes/sec)
    APP_ON_THRESHOLD = 0.10 * MAX_BANDWIDTH * 1000000 / 8  # 10% of max bandwidth
    
    # Initialize TC action handler (setup only, won't use for actions)
    action = TCQueueAction()
    action.setup_tc(SWITCH_PORT)
    
    # Initialize data gatherers
    gatherers: Dict[str, DataGatherer] = {port: DataGatherer(port, max_seconds=GATHERING_WINDOW) for port in ports}
    
    # Initialize FFT predictor (for both summed data and app detection)
    fft_predictor = FftPredictor(window_seconds=GATHERING_WINDOW, sleep_sec=1)
    app_predictor = FftPredictor(window_seconds=GATHERING_WINDOW, sleep_sec=1)

    # Create log directory and file for predictions
    log_dir = "prediction_logs"
    os.makedirs(log_dir, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(log_dir, f"app_state_predictions_{timestamp}.csv")
    
    with open(log_file, 'w', newline='') as csvfile:
        csv_writer = csv.writer(csvfile)
        csv_writer.writerow(['timestamp', 'prediction_start_time', 'prediction_end_time', 
                            'avg_prediction_value', 'app_state', 
                            'min_prediction_value', 'max_prediction_value',
                            'raw_predictions'])
        
        # Start data gathering for all ports
        for gatherer in gatherers.values():
            gatherer.start()
        
        # Allow data gathering to run for a sufficient duration initially
        print(f"Starting initial data gathering for {GATHERING_WINDOW} seconds...")
        time.sleep(GATHERING_WINDOW+2)  # Wait for some initial data
        print("Initial data gathering complete. Starting predictions...")
        
        try:
            # Main experiment loop
            while True:
                current_timestamp = time.time()
                formatted_time = datetime.fromtimestamp(current_timestamp).strftime('%Y-%m-%d %H:%M:%S')
                
                # Get data for the excluded port (the one we're monitoring for app activity)
                ex_port_data = gatherers[EXCLUDED_PORT].get_data()
                if ex_port_data is None or len(ex_port_data) == 0:
                    print(f"Error: No data available for prediction on {EXCLUDED_PORT}")
                    time.sleep(5)
                    continue
                
                # Sum data for all other ports (for context)
                summed_data = None
                for port in ports:
                    if port != EXCLUDED_PORT:
                        port_data = gatherers[port].get_data()
                        if summed_data is None:
                            summed_data = np.array(port_data)
                        else:
                            summed_data += np.array(port_data)

                if summed_data is None or len(summed_data) == 0:
                    print("Error: No data available for other ports.")
                    time.sleep(5)
                    continue

                # Calculate prediction for the application port
                try:
                    # Get prediction for future values
                    app_prediction_result = app_predictor.predict(ex_port_data)
                    fft_result = fft_predictor.predict(summed_data)
                    
                    # Process predictions in step size chunks
                    for i in range(0, len(app_prediction_result), STEP_SIZE):
                        # Get the chunk of prediction
                        chunk = app_prediction_result[i:i+STEP_SIZE] if len(app_prediction_result) >= i+STEP_SIZE else app_prediction_result[i:]
                        
                        if len(chunk) == 0:
                            print("Error: Prediction chunk length is zero, resetting.")
                            break
                        
                        # Calculate statistics about the prediction
                        avg_predicted_bandwidth = np.mean(chunk)
                        min_predicted_bandwidth = np.min(chunk)
                        max_predicted_bandwidth = np.max(chunk)
                        
                        # Determine if application is ON or OFF
                        app_state = "ON" if avg_predicted_bandwidth >= APP_ON_THRESHOLD else "OFF"
                        
                        # Calculate prediction time range
                        prediction_start_time = current_timestamp + i
                        prediction_end_time = current_timestamp + i + len(chunk)
                        
                        # Format prediction times for display
                        start_time_formatted = datetime.fromtimestamp(prediction_start_time).strftime('%Y-%m-%d %H:%M:%S')
                        end_time_formatted = datetime.fromtimestamp(prediction_end_time).strftime('%Y-%m-%d %H:%M:%S')
                        
                        # Print prediction information
                        print(f"[{formatted_time}] Prediction for {start_time_formatted} to {end_time_formatted}:")
                        print(f"  App State: {app_state}")
                        print(f"  Average: {avg_predicted_bandwidth:.2f} bytes/sec")
                        print(f"  Threshold: {APP_ON_THRESHOLD:.2f} bytes/sec")
                        
                        # Log prediction to CSV
                        csv_writer.writerow([
                            formatted_time,
                            start_time_formatted,
                            end_time_formatted,
                            avg_predicted_bandwidth,
                            app_state,
                            min_predicted_bandwidth,
                            max_predicted_bandwidth,
                            ','.join(map(str, chunk))
                        ])
                        csvfile.flush()  # Ensure data is written even if program is interrupted
                        
                        # Sleep for 5 seconds between logging intervals
                        time.sleep(5)
                        
                    # Optionally generate a visualization periodically
                    if np.random.random() < 0.05:  # 5% chance to create a plot
                        app_predictor.plot_prediction(ex_port_data, filename=f"app_prediction_{int(current_timestamp)}.png")
                                            
                except ValueError as ve:
                    print(f"Error predicting: {ve}")
                    time.sleep(5)
                    
        except KeyboardInterrupt:
            print("Experiment interrupted by user.")
        finally:
            # Stop data gathering when done
            for gatherer in gatherers.values():
                gatherer.stop()
            print(f"Experiment completed. Prediction logs saved to {log_file}")

if __name__ == "__main__":
    run_experiment()