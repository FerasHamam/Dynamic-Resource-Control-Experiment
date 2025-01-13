#!/bin/bash

echo "Setup for Server started..."

# Update package list
sudo apt-get update

# Install required packages
sudo apt-get install -y build-essential libzmq3-dev cmake pkg-config firewalld

# Disable firewall in ports 5555 to 5560 in order for the Socket to communicate
# Adjust based on the number of noise files that you are sending
for port in {5555..5560}; do
    sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done
sudo firewall-cmd --reload

echo "Setup for Server is complete..."
