docker run -it --rm \
  --platform linux/amd64 \
  --user $(id -u):$(id -g) \
  -v /etc/passwd:/etc/passwd:ro \
  -v /etc/group:/etc/group:ro \
  -v /media/psf/RosettaLinux:/media/psf/RosettaLinux:z \
  -v "$HOME/x64_home":"$HOME":z \
  --workdir "$HOME" \
  rhel-dev

