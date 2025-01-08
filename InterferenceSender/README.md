<!-- PROJECT LOGO -->
<br />
<p align="center">
  <h1 align="center">Interference Sender</h3>
</p>

# Follow the steps below to install dependencies, build the project, and run the ZMQ Interference Sender.

## 1. Give execute permissions to all scripts
```sh
cd /path/to/InterferenceSender
chmod +x scripts/*
```

## 2. Install Dependencies

First, update your package lists and install the necessary dependencies:

```sh
sudo ./scripts/setup.sh
```

## 3. Build the sender
```sh
cd /path/to/InterferenceSender
mkdir build
cd build
```
Run CMake to configure the project
```sh
cmake ..
```
Compile the interference sender c code
```sh
make
```

## 4. Create noise files

```sh
dd if=/dev/urandom of={file_name}.bin bs={file_size}M count=1
```

## 5. Run the Interference Sender

sender
```sh
./sender
```
