#!/bin/bash

START_PORT=5555

NUM_LISTENERS=$1

if [ -z "$NUM_LISTENERS" ]; then
    echo "Usage: $0 <num_listeners>"
    exit 1
fi

for (( i=0; i<NUM_LISTENERS; i++ )); do
    PORT=$((START_PORT + i))
    echo "Starting listener on port $PORT..."
    nc -lu $PORT &
done

wait

echo "All listeners are running."