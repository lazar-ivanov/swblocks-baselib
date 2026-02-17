docker run -it --rm \
  --platform linux/amd64 \
  --user $(id -u):$(id -g) \
  -v /etc/passwd:/etc/passwd:ro \
  -v /etc/group:/etc/group:ro \
  -v /media/psf/RosettaLinux:/media/psf/RosettaLinux \
  -v "$HOME/x64_home":"$HOME" \
  ubuntu-dev

