//go:build linux

package sandbox

import (
	"fmt"
	"log/slog"
	"unsafe"

	"github.com/HQarroum/microbox/fs"
	"github.com/HQarroum/microbox/logger"
	"github.com/HQarroum/microbox/net"
	uuid "github.com/google/uuid"
	"golang.org/x/sys/unix"
)

// Sandbox parameters.
type SandboxOptions struct {
	UUID          uuid.UUID
	FS            fs.FsMount
	ReadOnly      bool
	Net           net.NetworkMode
	MountRO       []fs.MountSpec
	MountRW       []fs.MountSpec
	Capabilities  *CapabilityOpts
	NamespaceMode UserNamespaceMode
	NameServ      []string
	Env           EnvVars
	LogLevel      slog.Level
	LogFormat     logger.LogFormat
	AllowSys      []string
	DenySys       []string
	Commands      []string
	Hostname      string
	CPUs          float64
	Memory        uint64
	Storage       uint64
}

// Describes a running sandbox process.
type SandboxProcess struct {

	// Unique sandbox identifier.
	uuid string

	// Process file descriptor.
	pidfd int

	// Process identifier.
	pid int

	// Network stack associated with the sandbox.
	network *net.NetworkResult

	// Applied cgroup path.
	cgPath string
}

// Linux clone3 ABI struct (uapi/linux/sched.h)
type cloneArgs struct {

	// CLONE_* flags
	Flags uint64

	// int *pidfd (user pointer)
	Pidfd uint64

	// int *ctid
	ChildTid uint64

	// int *ptid
	ParentTid uint64

	// exit signal (e.g., SIGCHLD)
	ExitSignal uint64

	// child stack (0 = inherit)
	Stack uint64

	// size of stack
	StackSize uint64

	// TLS pointer
	TLS uint64

	// pid_t *set_tid
	SetTid uint64

	// len(set_tid)
	SetTidSize uint64

	// int *cgroup fd (since 5.7)
	Cgroup uint64
}

// Default namespace flags for the sandbox.
var defaultFlags = unix.CLONE_NEWPID |
	unix.CLONE_NEWUTS |
	unix.CLONE_NEWIPC |
	unix.CLONE_PIDFD |
	unix.CLONE_NEWCGROUP |
	unix.CLONE_NEWTIME |
	unix.CLONE_NEWNS

/**
 * @return clone3 flags based on the sandbox options.
 */
func createSandboxFlags(opts *SandboxOptions) int {
	flags := defaultFlags

	// If host network is used, we don't create a new network namespace.
	if opts.Net != net.NetHost {
		flags |= unix.CLONE_NEWNET
	}

	// If user namespace is not set to host, create a new user namespace.
	if opts.NamespaceMode != UserNamespaceHost {
		flags |= unix.CLONE_NEWUSER
	}

	return flags
}

/**
 * Create and start a new sandboxed process with the specified options
 * in new namespaces using the clone3 syscall.
 * @param opts the sandbox options
 * @return the sandbox process descriptor, or an error if any
 */
func NewSandbox(opts *SandboxOptions) (*SandboxProcess, error) {
	process := &SandboxProcess{
		uuid:  uuid.New().String(),
		pidfd: -1,
		pid:   -1,
	}
	flags := createSandboxFlags(opts)

	cloneArgs := cloneArgs{
		Flags:      uint64(flags),
		Pidfd:      uint64(uintptr(unsafe.Pointer(&process.pidfd))),
		ExitSignal: uint64(unix.SIGCHLD),
	}

	// Check whether the current user is root.
	if unix.Geteuid() != 0 {
		return nil, fmt.Errorf("microbox must be run as root or with sudo")
	}

	// Create a synchronization pipe between parent and child.
	rfd, wfd, err := MakeSyncPipe()
	if err != nil {
		return nil, err
	}

	// Call clone3 to create the new process in a new namespace.
	pid, _, errno := unix.Syscall(
		unix.SYS_CLONE3,
		uintptr(unsafe.Pointer(&cloneArgs)),
		uintptr(unsafe.Sizeof(cloneArgs)),
		0,
	)
	if errno != 0 {
		ClosePipe(rfd, wfd)
		return nil, fmt.Errorf("cannot create sandbox: %w", errno)
	}

	if pid == 0 {
		// Wait for parent to finish setup before proceeding.
		if err := WaitForParent(rfd); err != nil {
			unix.Exit(1)
		}

		// Set the sandbox hostname.
		if opts.Hostname != "" {
			if err := unix.Sethostname([]byte(opts.Hostname)); err != nil {
				logger.Log.Warn("setting sandbox hostname failed", slog.Any("err", err))
			}
		}

		// Setup filesystem.
		if err := fs.SetupFS(&fs.FsOpts{
			Nameservers: opts.NameServ,
			Hostname:    opts.Hostname,
			FS:          opts.FS,
			ReadOnly:    opts.ReadOnly,
			MountRO:     opts.MountRO,
			MountRW:     opts.MountRW,
			Storage:     opts.Storage,
		}); err != nil {
			logger.Log.Error("failed to setup filesystem", slog.Any("err", err))
			unix.Exit(1)
		}

		// Drop capabilities.
		if err := opts.Capabilities.Apply(); err != nil {
			logger.Log.Error("failed to apply capabilities", slog.Any("err", err))
			unix.Exit(1)
		}

		// Setup seccomp filters.
		if err := SetupSeccomp(opts); err != nil {
			logger.Log.Error("failed to setup seccomp rules", slog.Any("err", err))
			unix.Exit(1)
		}

		// Execute the specified command in the process.
		err = unix.Exec(opts.Commands[0], opts.Commands, opts.Env.ToStringArray())

		// If execve returns, something failed.
		logger.Log.Error("failed to execute process", slog.Any("err", err))
		unix.Exit(127)
	}

	// Set up user and group mappings for the child.
	if opts.NamespaceMode != UserNamespaceHost {
		if err := SetupIdMappings(int(pid)); err != nil {
			ClosePipe(rfd, wfd)
			return nil, err
		}
	}

	// Setup CGroup limits.
	cgPath, err := SetupCgroupLimits(int(pid), opts.CPUs, opts.Memory)
	if err != nil {
		ClosePipe(rfd, wfd)
		return nil, err
	}

	// Setup networking if using bridged networks.
	if opts.Net != net.NetHost && opts.Net != net.NetNone {
		result, err := net.SetupContainerNetworking(net.NetworkConfig{
			ChildPID: int(pid),
			Mode:     opts.Net,
		})
		if err != nil {
			ClosePipe(rfd, wfd)
			return nil, err
		}
		process.network = result
	}

	// Saving child process information.
	process.pidfd = int(process.pidfd)
	process.pid = int(pid)
	process.cgPath = cgPath

	// Signal the child to continue.
	if err := SignalChild(wfd); err != nil {
		return nil, err
	}

	return process, nil
}

/**
 * Waits for the sandboxed process to exit, and returns its exit status.
 * Also performs cleanup of cgroups, network interfaces, and IPAM allocations.
 * @return the exit status code, or an error if any
 */
func (p *SandboxProcess) Wait() (int, error) {
	if p == nil || p.pid <= 0 {
		return 0, fmt.Errorf("invalid process")
	}
	defer func() {
		_ = CleanupCgroup(p.cgPath)
	}()

	var ws unix.WaitStatus
	for {
		wpid, err := unix.Wait4(p.pid, &ws, 0, nil)
		if err == unix.EINTR {
			continue
		}
		if err != nil {
			return 0, err
		}
		if wpid == p.pid {
			break
		}
	}

	// Release IPAM allocation.
	if p.network != nil {
		if err := p.network.Cleanup(); err != nil {
			logger.Log.Warn("failed to cleanup networking", slog.Any("err", err))
		}
	}

	if ws.Exited() {
		return ws.ExitStatus(), nil
	}
	if ws.Signaled() {
		return 128 + int(ws.Signal()), nil
	}
	return 0, nil
}
