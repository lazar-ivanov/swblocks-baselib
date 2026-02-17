# Start from the official base
FROM --platform=linux/amd64 ubuntu:latest

# Prevent interactive prompts during installation
ENV DEBIAN_FRONTEND=noninteractive

# Update and install developer toolsets
RUN apt-get update && apt-get install -y \
      build-essential \
      sudo \
      wget \
      zlib1g-dev \
      libgmp-dev \
      libmpfr-dev \
      libmpc-dev \
      flex \
      bison \
      texinfo \
      libzstd-dev \
      perl \
      libtext-template-perl \
      zip \
      unzip \
      lsb-release \
      python3 \
      iputils-ping \
      ninja-build \
      libxml2-dev \
      libedit-dev \
      swig \
      cmake \
      gdb \
      git \
      curl \
      vim \
      ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# (Optional) Run unminimize if you want man pages and standard server tools
RUN yes | unminimize

CMD ["/bin/bash"]

