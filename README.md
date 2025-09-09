<p align="center">
  <img width="450" src="assets/icon.png" alt="logo" />
  <br />
  <p align="center">Lightweight sandboxes for Linux with process isolation, namespaces and cgroups.</p>
  <p align="center">
    <a href="https://github.com/codespaces/new/HQarroum/microbox">
      <img alt="Github Codespaces" src="https://github.com/codespaces/badge.svg" />
    </a>
  </p>
</p>

## 🔖 Features

- **Isolation** - Secure sandbox isolating networking, filesystem, processes, users, time, and more.
- **Filesystem** - Supports mounting a custom `rootfs` (with an OverlayFS), an empty `tmpfs`, or the `host` filesystem.
- **Networking** - No network by default. `bridge` and `host` modes available.
- **Bind Mounts** - Selective mounting of host directories with read-only or read-write permissions
- **Security Features** - Seccomp filtering for syscall restrictions and capability dropping.
- **Resource Limits** - CPU and memory constraints using CGroups.


## How it works ❓

Microbox creates isolated execution environments by leveraging Linux kernel features. It uses namespaces to provide process, network, filesystem, and user isolation. The sandbox can run with different filesystem modes, from completely isolated tmpfs to controlled access to host directories.

<p align="center">
  <img width="900" src="assets/diagram.png" alt="sandbox architecture" />
</p>

The sandbox process is spawned using `clone3()` with specific namespace flags, then configured with resource limits, filesystem mounts, and security policies before executing the target command.

## 🚀 Quickstart

### Building from source

**Prerequisites:**
- Linux system with GCC compiler
- libseccomp development headers

Install dependencies on Ubuntu/Debian:
```bash
sudo apt update
sudo apt install build-essential libseccomp-dev
```

Install dependencies on RHEL/CentOS/Fedora:
```bash
sudo yum install gcc make libseccomp-devel  # RHEL/CentOS
# or
sudo dnf install gcc make libseccomp-devel  # Fedora
```

**Build:**
```bash
git clone https://github.com/HQarroum/microbox.git
cd microbox
make
```

Alternative single-command build:
```bash
gcc -o microbox *.c -lm -lseccomp
```

### Basic Usage

Run a simple command in an isolated environment:

```bash
./microbox -- /bin/sh -c "echo 'Hello from sandbox!'"
```

### Filesystem Isolation

Create a sandbox with a temporary filesystem:

```bash
./microbox --fs tmpfs -- /bin/ls /
```

Mount a custom rootfs directory:

```bash
./microbox --fs /path/to/rootfs -- /bin/bash
```

### Network Isolation

Run with no network access:

```bash
./microbox --net none -- curl google.com
```

Run with host network access:

```bash
./microbox --net host -- curl google.com
```

Run with bridge networking (container gets its own IP):

```bash
./microbox --net bridge -- curl google.com
```

### Bind Mounts

Mount host directories into the sandbox:

```bash
./microbox --mount-ro /etc:/etc --mount-rw /tmp:/tmp -- /bin/bash
```

## 📟 Options

- `--fs MODE|DIR` - Filesystem mode: `host` (use host filesystem), `tmpfs` (temporary filesystem), or path to rootfs directory
- `--net MODE` - Network mode: `none` (no network), `host` (host network), `private` (isolated network), `bridge` (bridged network with NAT)
- `--proc` - Mount `/proc` filesystem in the sandbox
- `--dev` - Mount `/dev` filesystem in the sandbox  
- `--mount-ro HOST:DEST` - Create read-only bind mount from host path to sandbox destination
- `--mount-rw HOST:DEST` - Create read-write bind mount from host path to sandbox destination
- `--env KEY=VALUE` - Set environment variable in the sandbox
- `--hostname NAME` - Set custom hostname for the sandbox
- `--cpus N` - Set CPU limit (e.g., 0.5 for half a core, 2 for two cores)
- `--memory SIZE` - Set memory limit (e.g., 10M, 2G)
- `--allow-syscall SYSCALL` - Allow specific system call (whitelist mode)
- `--deny-syscall SYSCALL` - Deny specific system call (blacklist mode)
- `-v, --verbose` - Enable verbose output for debugging
- `--help` - Display help message

## 🚧 Limitations

- **Linux Only** - Requires Linux kernel with namespace support (kernel 3.8+)
- **Privilege Requirements** - Some features require elevated privileges:
  - **Bridge Networking** - Requires CAP_NET_ADMIN or root privileges for network setup
  - **CGroups** - CPU and memory limits require CAP_SYS_ADMIN or root privileges
  - **Mount Operations** - Custom rootfs and bind mounts may require additional privileges
- **Seccomp Support** - System call filtering requires kernel seccomp support
- **Architecture** - Currently designed for x86_64 Linux systems
- **Container Runtime** - Not a full container runtime replacement, focused on process isolation
- **NAT Dependencies** - Bridge mode requires iptables and IP forwarding capabilities

## 🛠️ Advanced Usage

### Resource Constraints

Limit sandbox to half a CPU core and 512MB memory:

```bash
./microbox --cpus 0.5 --memory 512M -- stress --cpu 1 --vm 1 --vm-bytes 256M
```

### Security Profiles

Create a restrictive sandbox that only allows basic syscalls:

```bash
./microbox --allow-syscall read --allow-syscall write --allow-syscall exit_group -- ./my-program
```

### Custom Environment

Run with specific environment and hostname:

```bash
./microbox --hostname sandbox --env USER=sandbox --env HOME=/tmp -- /bin/bash
```

### Development Environment

Create a development sandbox with selective host access:

```bash
./microbox --fs tmpfs --mount-ro /usr:/usr --mount-rw $PWD:/workspace --proc --dev -- /bin/bash
```

## 🧪 Testing

A basic test suite is provided to validate functionality:

```bash
# Run the test suite
./test_microbox.sh

# Build and run tests
make && ./test_microbox.sh
```

**Note:** Some tests require elevated privileges to fully validate sandbox functionality. The test suite will skip privileged tests when run as a regular user.

### Test Categories:
- **Basic Tests**: Binary existence, help output, error handling
- **Unprivileged Tests**: Argument parsing, basic execution attempts
- **Privileged Tests**: Full sandbox functionality (requires root)

## 🔧 Troubleshooting

### Build Issues

**Error: `seccomp.h: No such file or directory`**
```bash
# Install libseccomp development headers
sudo apt install libseccomp-dev  # Ubuntu/Debian
sudo yum install libseccomp-devel # RHEL/CentOS
sudo dnf install libseccomp-devel # Fedora
```

### Runtime Issues

**Error: `setup_cgroup_limits() failed: Permission denied`**
- CGroup limits require elevated privileges (root or CAP_SYS_ADMIN)
- Run with sudo for full functionality: `sudo ./microbox ...`
- Or disable resource limits by not using `--cpus` or `--memory` flags

**Error: `Failed to create network namespace`**
- Network isolation requires CAP_NET_ADMIN or root privileges
- Use `--net host` to use host networking without privileges
- Or run with sudo for full network isolation

**Error: `mount: Operation not permitted`**
- Filesystem operations may require additional privileges
- Use `--fs host` to avoid filesystem isolation
- Or run with appropriate capabilities/privileges

## 👀 See Also

- [Linux Namespaces](https://man7.org/linux/man-pages/man7/namespaces.7.html) - Kernel documentation on namespaces
- [Seccomp](https://man7.org/linux/man-pages/man2/seccomp.2.html) - System call filtering
- [Cgroups](https://man7.org/linux/man-pages/man7/cgroups.7.html) - Resource control groups
- [Docker](https://www.docker.com/) - Full-featured container platform
- [Podman](https://podman.io/) - Daemonless container engine
- [Firejail](https://firejail.wordpress.com/) - SUID sandbox program