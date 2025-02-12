#!/bin/bash

# Run the setup
./setup.sh

# Disable firewall in port 4444 in order for the Socket to communicate
sudo firewall-cmd --zone=public --add-port=4444/tcp --permanent
sudo firewall-cmd --reload

# Run the compiled client C code
echo "Running the client C code..."
./client
# Check if the C program executed successfully
if [ $? -eq 0 ]; then
    echo "Client C code executed successfully."
    # Run the Python script after the C code has finished
    #echo "Running the Python script for Full blob detection..."
    #./venv/bin/python ~/zmq/scripts/data_to_blob_detection.py --app_name xgc --data_type full --path ./ --output_name xgc_full.png
    echo "Running the Combining Python script for blob detection..."
    ./venv/bin/python ~/zmq/scripts/combine.py
    echo "Running the script for Reduced blob detection..."
    ./venv/bin/python ~/zmq/scripts/data_to_blob_detection.py --app_name xgc --data_type reduced --path ~/zmq/data/reduced/ --output_name xgc_reduced.png
else
    echo "Error: Client C code failed to run."
fi
