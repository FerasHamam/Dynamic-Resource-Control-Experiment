#!/bin/bash

# Mininet Full Setup Script with SSH Configuration
# This script installs Mininet and sets up SSH for seamless operation.

# Exit immediately if a command exits with a non-zero status
set -e

# Define colors for output
GREEN='\033[0;32m'
NC='\033[0m' # No Color
RED='\033[0;31m'

# Function to print messages
print_info() {
    echo -e "${GREEN}[INFO] $1${NC}"
}

print_error() {
    echo -e "${RED}[ERROR] $1${NC}"
}

# Ensure the script is run with sudo
if [[ $EUID -ne 0 ]]; then
   print_error "This script must be run as root. Try using 'sudo'."
   exit 1
fi

# Detect the current user
CURRENT_USER=$(logname)
HOME_DIR=$(eval echo ~$CURRENT_USER)

print_info "Detected user: $CURRENT_USER"

# Update package lists
print_info "Updating package lists..."
apt update -y

# Install necessary packages
print_info "Installing required packages: git, openssh-server, python3, pip, and other dependencies..."
apt install -y git openssh-server python3 python3-pip build-essential python3-minimal python3-setuptools python3-networkx

# Clone the Mininet repository if not already cloned
if [[ ! -d "$HOME_DIR/mininet" ]]; then
    print_info "Cloning Mininet repository..."
    sudo -u $CURRENT_USER git clone https://github.com/mininet/mininet.git "$HOME_DIR/mininet"
else
    print_info "Mininet repository already exists. Pulling latest changes..."
    sudo -u $CURRENT_USER git -C "$HOME_DIR/mininet" pull
fi

# Install Mininet and its dependencies
print_info "Running Mininet install script..."
bash "$HOME_DIR/mininet/util/install.sh" -a

# SSH key generation and configuration
SSH_DIR="$HOME_DIR/.ssh"
AUTHORIZED_KEYS="$SSH_DIR/authorized_keys"

print_info "Setting up SSH keys for user $CURRENT_USER..."

# Check if SSH key exists, if not, generate one
if [[ ! -f "$SSH_DIR/id_rsa.pub" ]]; then
    print_info "Generating SSH key..."
    sudo -u $CURRENT_USER ssh-keygen -t rsa -b 4096 -N "" -f "$SSH_DIR/id_rsa"
else
    print_info "SSH key already exists."
fi

# Ensure authorized_keys file exists and add the public key if not present
if [[ ! -f "$AUTHORIZED_KEYS" ]]; then
    print_info "Creating authorized_keys file..."
    sudo -u $CURRENT_USER touch "$AUTHORIZED_KEYS"
    chmod 600 "$AUTHORIZED_KEYS"
fi

# Add public key to authorized_keys if not already present
if ! grep -q "$(cat "$SSH_DIR/id_rsa.pub")" "$AUTHORIZED_KEYS"; then
    print_info "Adding SSH public key to authorized_keys..."
    cat "$SSH_DIR/id_rsa.pub" >> "$AUTHORIZED_KEYS"
else
    print_info "SSH public key is already in authorized_keys."
fi

# Set proper permissions for .ssh directory and files
chown -R $CURRENT_USER:$CURRENT_USER "$SSH_DIR"
chmod 700 "$SSH_DIR"
chmod 600 "$AUTHORIZED_KEYS"

# Enable and start SSH service
print_info "Enabling and starting SSH service..."
systemctl enable ssh
systemctl restart ssh

# Verify SSH is running
if systemctl is-active --quiet ssh; then
    print_info "SSH service is running."
else
    print_error "SSH service failed to start."
    exit 1
fi

print_info "Mininet installation and SSH setup completed successfully!"

# Optional: Run a simple Mininet test
read -p "Do you want to run a simple Mininet test? (y/n): " RUN_TEST
if [[ $RUN_TEST =~ ^[Yy]$ ]]; then
    print_info "Running Mininet test..."
    sudo -u $CURRENT_USER sudo mn --test pingall
else
    print_info "Skipping Mininet test."
fi

print_info "Setup completed. You can now use Mininet and SSH into hosts."

exit 0
