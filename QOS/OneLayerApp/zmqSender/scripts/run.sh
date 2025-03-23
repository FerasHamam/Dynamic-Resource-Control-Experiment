#!/bin/bash

#Run the setup
./setup.sh

# Disable firewall in ports 4444 to 4447 in order for the Socket to communicate
for port in {5555..5556}; do
    sudo firewall-cmd --zone=public --add-port=${port}/tcp --permanent
done
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
