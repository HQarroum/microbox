<p align="center">
  <img width="280" src="assets/icon.png" alt="logo" />
  <br />
  <p align="center">Create lightweight sandboxes for Linux with host isolation, rootfs images, and networking.</p>
  <p align="center">
    <a href="https://github.com/codespaces/new/HQarroum/microbox">
      <img alt="Github Codespaces" src="https://github.com/codespaces/badge.svg" />
    </a>
  </p>
</p>

## 🔖 Features

- **Sandbox Isolation** - Isolates networking, file-system, processes, and users from the host using Linux namespaces.
- **Filesystem Images** - Uses an [Overlay FS](https://docs.kernel.org/filesystems/overlayfs.html) to mount a custom root file-system from a user image. Also supports minimal `tmpfs` dedicated to bind-mounting.
- **Networking** - Provides full network isolation by default. `bridge` and `host` modes available.
- **Bind-mounts** - Selective read-only/read-write bind-mounting of host directories into the sandbox.
- **Security Features** - Seccomp filtering for syscall restrictions and Linux capability management support.
- **Resource Limits** - Enforce CPU and Memory constraints using CGroups.

## What is it ❓

Microbox is a sandbox runtime that creates ephemeral and isolated execution environments on Linux by combining specific kernel features such as [namespaces](https://en.wikipedia.org/wiki/Linux_namespaces), [cgroups](https://en.wikipedia.org/wiki/Cgroups), [seccomp](https://man7.org/linux/man-pages/man2/seccomp.2.html), and [capabilities](https://man7.org/linux/man-pages/man7/capabilities.7.html). It provides lightweight sandboxes to run container-like applications securely.

Namespacing in `microbox` provide *process*, *network*, *filesystem*, *IPC*, and *user* isolation, while cgroups provide resource limitation management, and can run with different filesystem modes, from completely isolated overlay root filesystems, to controlled access to host directories.

Microbox is built out of an educational purpose to understand how secure non VM based sandboxes can be on Linux. Its philosophy is to provide a maximum isolation by default, unlike other container runtimes such as [`runc`](https://github.com/opencontainers/runc), or [`Podman`](https://github.com/containers/podman) which make the choice of enforcing a good balance between security and usability of applications.

## 🚀 Quickstart

You can run a simple rootfs image in an isolated sandbox in a couple minutes. First, follow the instructions in the [Creating a Root Filesystem](docs/create-root-fs.md) section to create a Ubuntu rootfs image on your host.

> Feel free to adapt the architecture and packages as needed in your image as needed.

#### Install

Download the latest release from the [Releases](https://github.com/HQarroum/microbox/releases) page.

#### Create a sandbox

Run `microbox` to create an isolated sandbox using the rootfs image you just created. This will run an isolated sandbox with no network access, using the new rootfs image.

```bash
./microbox --fs <rootfs> -- /bin/bash -c "echo Hello Sandbox!"
```

## Usage

### Filesystem

The sandbox exposes a file-system that is ephemeral, isolated from the host—unless otherwise specified—and mounted in memory. As such, changes caused by applications within the sandbox's rootfs do not reflect on the host filesystem.

Below is a comparison table of the different root file-system options supported.

Mode | Ephemeral | Isolated | Notes
---- | --------- | -------- | -----
`tmpfs` | Yes | Yes | Default value. Full-isolation from the host, minimal rootfs with no binaries. Only `devfs` and `procfs` mounted.
`rootfs` | Yes | Yes | Full-isolation from the host, mounts a user-provided rootfs.
`host` | No | No | Minimal isolation, host file-system is available in the sandbox and writes reflect on the host.

#### Mount a specific rootfs image

Using a custom rootfs image provides a complete environment with binaries and libraries in the sandbox. You can create your own rootfs image using the instructions in the [Creating a Root Filesystem](docs/create-root-fs.md) section.

```bash
microbox --fs ./ubuntu-24.04 -- /bin/ls
```

#### Control storage size

The default storage size is set to 512MB in the sandbox. Using the `--storage` option, you can control the size of the writable layer.

```bash
microbox --storage 2GB -- /bin/ls
```

#### Minimal Filesystem with `tmpfs`

This is the default, but you can make it explicit by specifying `--fs tmpfs`. In this mode, the sandbox exposes an empty rootfs with only `devfs` and `procfs` mounted.

> In this example, we bind-mount `/bin` and `lib` from the host to provide a minimal environment.

```bash
microbox \
  --mount-ro /bin:/bin \
  --mount-ro /lib:/lib \
  -- /bin/ls
```

You can bind mount host directories into the sandbox using the `--mount-ro` and `--mount-rw` options.

> Note that writes made by the sandbox to writable bind mounts will affect the host filesystem.

```bash
./microbox \
  --mount-ro /etc:/etc \
  --mount-rw /tmp:/tmp \
  -- /bin/bash
```

### Network

The default network mode is `none`, which means no network access. You can change this behavior using the `--net` option.

#### `none`

> No networking in sandboxes is the default, you can make it explicit by specifying `--net none`.

```bash
$ microbox --fs <rootfs> -- /bin/curl google.com
curl: (6) Could not resolve host: google.com
```

#### `bridge` Network

In `bridge` mode, the sandbox gets its own network interface and IP address. It uses [`veth` pairs](https://man7.org/linux/man-pages/man4/veth.4.html) and NAT to provide network access between the host and the sandbox.

```bash
$ microbox --fs <rootfs> --net bridge \
-- curl https://api.ipify.org/
1.2.3.4
```

#### `host` network access

Host networking does not provide any network isolation with the host. The sandbox shares the host network interface and network stack.

```bash
$ microbox --fs <rootfs> --net host \
-- /bin/curl https://api.ipify.org/
1.2.3.4
```

### Other Options

#### Limit CPU

You can limit the number of CPU cores available to the sandbox using the `--cpus` option.

```bash
./microbox --cpus 1 -- /bin/bash
```

#### Limit Memory

You can limit the amount of memory available to the sandbox using the `--memory` option.

```bash
./microbox --memory 256M -- /bin/bash
```

#### Environment Variables

You can chain environment variables in the sandbox using the `--env` option.

```bash
./microbox --env MY_VAR=hello -- /bin/bash -c 'echo $MY_VAR'
```

#### Security Profiles

You can restrict the syscalls available to the sandbox using the `--allow-syscall` and `--deny-syscall` options.

> See the [Default Seccomp Profile](./sandbox/seccomp.go) section for more details on syscalls denied by default.

```bash
./microbox \
  --fs ./rootfs \
  --allow-syscall unshare \
  --allow-syscall setns \
  -- /bin/bash
```

#### Capabilities

You can add or drop Linux capabilities in the sandbox using the `--cap-add` and `--cap-drop` options.

```bash
./microbox \
  --fs ./rootfs \
  --cap-drop CAP_SETPCAP \
  -- /bin/bash
```

#### Custom DNS Servers

You can set custom DNS servers for the sandbox using the `--dns` option.

> By default, microbox uses [Google DNS Servers](https://developers.google.com/speed/public-dns/docs/using) for the sandbox.

```bash
./microbox \
  --fs ./rootfs \
  --dns 1.1.1.1 \
  --dns 8.8.8.8 \
  -- /bin/bash
```

#### Custom Hostname

You can set a custom hostname for the sandbox using the `--hostname` option.

```bash
$ ./microbox \
  --fs ./rootfs \
  --hostname my-sandbox \
  -- /bin/bash -c 'hostname'
my-sandbox
```

#### Read-only Rootfs

You can mount the root filesystem as read-only using the `--readonly` option to disable any writes to the rootfs.

```bash
./microbox \
  --fs <rootfs> \
  --readonly \
  -- /bin/bash
```

#### User Namespace

By default, the sandbox runs in a new user namespace, mapping the root user in the sandbox to a user on the host. This can break the normal operations of some applications. You can disable this behavior using the `--userns host` option.

> This is recommended for working with applications such as `apt` which attempt to change the effective user ID at runtime.

```bash
./microbox \
  --fs <rootfs> \
  --userns host \
  -- /bin/bash
```

#### Logging

You can control the log level and format using the `--log-level` and `--log-format` options.

##### Log Levels

Log Level | Scope
--------- | -----
`info` | Informational logs + warnings + errors
`warn` | Warnings about potential issues + errors
`error` | Display only errors

##### Log Formats

Format | Description
------ | -----------
`text` | Human-readable text format (default)
`json` | Structured JSON format

#### Example

```bash
./microbox \
  --fs ./rootfs \
  --log-level info \
  --log-format json \
  -- /bin/bash
```

## 🛡️ Isolation

Below is a description of the isolation features provided by `microbox` by default.

### Namespaces

Namespace | Symbol | Enabled
--------- | ------ | -------
Process | [`CLONE_NEWPID`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | ✅
Hostname and Domain name | [`CLONE_NEWUTS`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | ✅
Mount | [`CLONE_NEWNS`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | ✅
IPC | [`CLONE_NEWIPC`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | ✅
Time | [`CLONE_NEWTIME`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | ✅
Cgroups | [`CLONE_NEWCGROUP`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | ✅
Network | [`CLONE_NEWNET`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | Enabled by default ✅. Can be disabled with `--net host`.
User | [`CLONE_NEWUSER`](https://man7.org/linux/man-pages/man7/namespaces.7.html) | Enabled by default ✅. Can be disabled with `--userns host`.

### Limits

Limit | Description | Default | Modifiable
----- | ----------- | ------- | ----------
CPU | Limit on the number of CPUs usable by the sandbox | 1 CPU | Yes
Memory | Limit on the amount of memory usable by the sandbox | 1 GB | Yes
Storage | Limit on the size of the mounted storage | 512 MB | Yes
Swap | Limit on the amount of swap usable by the sandbox | Disabled | No

## 📟 Options

- `--fs MODE|DIR` - Filesystem mode: `host` (uses host filesystem), `tmpfs` (temporary filesystem), or a path to use a directory as the rootfs
- `--net MODE` - Network mode: `none` (no network), `host` (use host network), `bridge` (bridged network with NAT)
- `--mount-ro HOST:DEST` - Create read-only bind mount from host path to sandbox destination
- `--mount-rw HOST:DEST` - Create read-write bind mount from host path to sandbox destination
- `--readonly` - Mount the root filesystem as read-only
- `--env KEY=VALUE` - Set environment variable in the sandbox
- `--allow-syscall SYSCALL` - Allow specific system calls in the sandbox using seccomp
- `--deny-syscall SYSCALL` - Deny specific system calls in the sandbox using seccomp
- `--dns SERVER` - Set custom DNS server for the sandbox
- `--hostname NAME` - Set custom hostname for the sandbox
- `--cpus N` - Set CPU limit (e.g., 0.5 for half a core, 2 for two cores)
- `--memory SIZE` - Set memory limit (e.g., 10MB, 2GB)
- `--storage SIZE` - Set storage limit for the sandbox filesystem (e.g., 1GB, 10GB)
- `--log-level LEVEL` - Set log level between `info`, `warn`, `error` (default: `error`)
- `--log-format FORMAT` - Set log format: `text` or `json` (default: `json`)
- `--cap-add CAPABILITY` - Add a Linux capability to the sandbox (e.g., `CAP_NET_ADMIN`)
- `--cap-drop CAPABILITY` - Drop a specific Linux capability from the sandbox (e.g., `CAP_SYS_TIME`)
- `--help` - Display help message

## 🚧 Limitations

- **Rootless Unsupported** - Sandbox creation currently requires root privileges to create namespaces, cgroups and a network bridge.
- **OCI Support** - Not a full container runtime replacement, no OCI image support, focused on process isolation.
- **No AppArmor/SELinux** - Does not currently support AppArmor or SELinux profiles for additional security layers.

## 👀 See Also

- [Linux Namespaces](https://man7.org/linux/man-pages/man7/namespaces.7.html) - Kernel documentation on namespaces
- [Seccomp](https://man7.org/linux/man-pages/man2/seccomp.2.html) - System call filtering
- [Cgroups](https://man7.org/linux/man-pages/man7/cgroups.7.html) - Resource control groups
- [OverlayFS](https://docs.kernel.org/filesystems/overlayfs.html) - Union filesystem for layering filesystems
- [runc](https://github.com/opencontainers/runc) - Container runtime
