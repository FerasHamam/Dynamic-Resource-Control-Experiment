#!/bin/bash

#Run the setup
./setup.sh

# Disable firewall in port 4444 in order for the Socket to communicate
sudo firewall-cmd --zone=public --add-port=4444/tcp --permanent
sudo firewall-cmd --reload

# Run the compiled server C code
echo "Running the Server C code"
./server

#Check if the c program was executed successfully
if [ $? -eq 0 ]; then
    echo "Server C code executed successfully."
else
    echo "Error: Server C code failed to run."
fi
