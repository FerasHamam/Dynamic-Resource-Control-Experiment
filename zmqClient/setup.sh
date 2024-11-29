#!/bin/bash

echo "Setup for Client started"

# Update package list
sudo apt-get update

# Install necessary packages
sudo apt-get install -y build-essential libzmq3-dev python3 python3-pip python3-venv

# Create Python virtual environment
python3 -m venv ./venv

# Activate virtual environment
source ./venv/bin/activate

# Install Python dependencies
pip install numpy matplotlib scipy opencv-python-headless

echo "Setup complete! Virtual environment created and required packages installed."
