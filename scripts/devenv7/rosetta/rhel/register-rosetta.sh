#!/bin/bash
# Using the path you confirmed: /media/psf/
ROSETTA_PATH="/media/psf/RosettaLinux/rosetta"

# Wait until the Parallels Rosetta share is actually mounted
# Parallels tools will typically mount this shortly after boot
while [ ! -f "$ROSETTA_PATH" ]; do
  sleep 1
done

# Clear ONLY the rosetta entry if it exists to prevent "File exists" errors
if [ -f /proc/sys/fs/binfmt_misc/rosetta ]; then
  echo -1 | sudo tee /proc/sys/fs/binfmt_misc/rosetta
fi

# Register with OC flags (O = open-binary, C = credentials)
# These flags are critical for Rosetta to work within containers and sub-processes
echo ":rosetta:M::\x7fELF\x02\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x00\x3e\x00:\xff\xff\xff\xff\xff\xfe\xfe\x00\xff\xff\xff\xff\xff\xff\xff\xff\xfe\xff\xff\xff:$ROSETTA_PATH:OC" | sudo tee /proc/sys/fs/binfmt_misc/register

