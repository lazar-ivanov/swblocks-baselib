#!/bin/bash
# RHEL 9/10 Installation Script for Rosetta

# 1. Copy the registration script
sudo cp ./register-rosetta.sh /usr/local/bin/register-rosetta.sh
sudo chmod +x /usr/local/bin/register-rosetta.sh

# 2. Copy the systemd service
sudo cp ./rosetta-binfmt.service /etc/systemd/system/rosetta-binfmt.service

# 3. Reload systemd and enable the service
sudo systemctl daemon-reload
sudo systemctl enable rosetta-binfmt.service

# 4. Start it immediately
echo "Starting Rosetta registration service..."
sudo systemctl restart rosetta-binfmt.service

# 5. Verify status
systemctl status rosetta-binfmt.service --no-pager
echo "---"
if [ -f /proc/sys/fs/binfmt_misc/rosetta ]; then
    echo "SUCCESS: Rosetta is registered in the kernel."
    cat /proc/sys/fs/binfmt_misc/rosetta
else
    echo "ERROR: Rosetta registration failed."
fi

