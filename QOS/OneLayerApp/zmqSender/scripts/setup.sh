#!/bin/bash

echo "Setup for Server started..."

# Update package list
sudo apt-get update

# Install required packages
sudo apt-get install -y build-essential libzmq3-dev cmake pkg-config firewalld libfftw3-dev python3 python3-venv python3-pip libpcap-dev

# Disable firewall in ports 4444 to 4447 in order for the Socket to communicate
for port in {5555..5556}; do
    sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done
sudo firewall-cmd --reload

echo "Setup for Server is complete..."
