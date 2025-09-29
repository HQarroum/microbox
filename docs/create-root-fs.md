## Creating a Root Filesystem

In this section, we will download a simple Ubuntu rootfs image to be used for our isolated sandbox using [`mmdebstrap`](https://manpages.debian.org/testing/mmdebstrap/mmdebstrap.1.en.html).

> This guide assumes a Ubuntu-based host system.

### Prerequisites

Install the required dependencies on the host.

```bash
sudo apt-get install -y \
  debian-archive-keyring \
  mmdebstrap
```

### Create the image

Create a minimal Ubuntu rootfs image using `mmdebstrap`. The below command assumes an `amd64` architecture, update the architecture and packages as needed.

```bash
sudo mmdebstrap \
  --arch=amd64 \
  --components=main,universe \
  --keyring=/usr/share/keyrings/ubuntu-archive-keyring.gpg \
  --include=ca-certificates,apt,iproute2,iputils-ping,curl,procps,less,vim,nano \
  noble ./ubuntu-noble-amd64 \
  http://archive.ubuntu.com/ubuntu
```

### Using the image

Your new root filesystem will be created in the `./ubuntu-noble-amd64` directory. You can use this root filesystem image with `microbox` as shown in the [Quickstart](../README.md#-quickstart) section.
