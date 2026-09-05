#!/bin/bash
# Wait until the Parallels share is actually mounted
while [ ! -f /media/psf/RosettaLinux/rosetta ]; do
  sleep 1
done

# Clear ONLY the rosetta entry if it exists
if [ -f /proc/sys/fs/binfmt_misc/rosetta ]; then
  echo -1 | sudo tee /proc/sys/fs/binfmt_misc/rosetta
fi

# Register with OC flags pointing to the MOUNT
# Note: Path is /media/psf/... and flags are :OC
echo ':rosetta:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x3e\x00:\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:/media/psf/RosettaLinux/rosetta:OC' | sudo tee /proc/sys/fs/binfmt_misc/register

