#!/bin/bash

echo "Setup for Client started"

# Update package list
sudo apt-get update

# Install necessary packages
sudo apt-get install -y build-essential libzmq3-dev python3 python3-pip python3-venv cmake pkg-config

# Create Python virtual environment
python3 -m venv ../venv

# Activate virtual environment
source ./venv/bin/activate

# Install Python dependencies
pip install numpy matplotlib scipy opencv-python-headless

# Disable firewall in ports 4444 to 4448 in order for the Socket to communicate
for port in {4444..4448}; do
    sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done
sudo firewall-cmd --reload

echo "Setup complete! Virtual environment created and required packages installed."
