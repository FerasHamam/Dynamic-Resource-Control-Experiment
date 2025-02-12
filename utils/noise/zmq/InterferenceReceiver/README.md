<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Interference Receiver</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ Interference Receiver.

## 1. Give execute permissions to all scripts
```sh
cd /path/to/InterferenceReceiver
chmod +x scripts/*
```

## 2. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo ./scripts/setup.sh
```

## 3. Build the receiver
```sh
cd /path/to/InterferenceReceiver
mkdir build
cd build
```
Run CMake to configure the project
```sh
cmake ..
```
Compile the interference receiver c code
```sh
make
```

## 4. Run the Interference Receiver

receiver
```sh
./receiver
```
