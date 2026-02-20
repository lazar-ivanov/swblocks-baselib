# 1. Remove old versions and install dependencies
sudo dnf remove -y docker docker-client docker-client-latest docker-common docker-latest docker-latest-logrotate docker-logrotate docker-engine
sudo dnf install -y yum-utils

# 2. Add the official Docker repo
sudo yum-config-manager --add-repo https://download.docker.com/linux/centos/docker-ce.repo

# 3. Install Docker Engine and Buildx
sudo dnf install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin

# 4. Start and enable Docker (RHEL doesn't start services by default)
sudo systemctl enable --now docker

# 5. User permissions
sudo usermod -aG docker $USER

# 6. Initialize Buildx with Rosetta/Multi-arch support
# Note: On RHEL, you may need to ensure binfmt is registered
sudo systemctl restart systemd-binfmt

# sudo docker buildx rm rosetta-builder || true
# sudo docker buildx create --name rosetta-builder --driver docker-container --use
# sudo docker buildx inspect --bootstrap

