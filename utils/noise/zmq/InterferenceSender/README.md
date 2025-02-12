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

## 3. Specify the client ip

Update client_ip on sender.c file line 10:

1. 
```sh
vim sender.c
```
2.
```sh
Search for CLIENT_IP using ? - Or go to line 10
```
3.
```sh
#define CLIENT_IP "CLIENT_IP" // Update with the client's IP
```

## 4. Build the sender
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

## 5. Create noise files (No naming conventions use whatever suits you)

```sh
dd if=/dev/urandom of={file_name}.bin bs={file_size}M count=1
```

## 6. Create interval text file for each noise file that you made
1. Create the file:
```sh
vim intervals.txt
```

2. If you have created 3 noise files, specify 3 numbers (the metric is seconds) in the file. For example:
```sh
10 20 30
```

3. Save the file.

## 7. Run the Interference Sender

sender
```sh
./sender
```
