sudo apt-get update
sudo apt-get install -y qemu-user-static binfmt-support docker.io
sudo apt-get install -y docker-buildx
sudo usermod -aG docker $USER
sudo docker buildx rm rosetta-builder || true
sudo docker buildx create --name rosetta-builder --driver docker-container --use
sudo docker buildx inspect --bootstrap

