//go:build linux

package options

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/HQarroum/microbox/fs"
	"github.com/HQarroum/microbox/sandbox"
	"github.com/HQarroum/microbox/version"
	"github.com/google/uuid"
	"github.com/goombaio/namegenerator"
	"github.com/inhies/go-bytesize"
	"github.com/urfave/cli/v3"
)

/**
 * Builds an `Options` struct from CLI context.
 * @param c the CLI context
 * @return the built Options and error if any
 */
func buildOptionsFromCLI(c *cli.Command) (*sandbox.SandboxOptions, error) {
	o := &sandbox.SandboxOptions{
		UUID:     uuid.New(),
		Hostname: c.String("hostname"),
		CPUs:     float64(c.Float32("cpus")),
		AllowSys: c.StringSlice("allow-syscall"),
		DenySys:  c.StringSlice("deny-syscall"),
		NameServ: c.StringSlice("dns"),
		ReadOnly: c.Bool("readonly"),
	}

	// Memory size parsing.
	mem, err := bytesize.Parse(c.String("memory"))
	if err != nil {
		return nil, fmt.Errorf("bad --memory %q: %v", c.String("memory"), err)
	}
	o.Memory = uint64(mem)

	// Storage size parsing.
	stor, err := bytesize.Parse(c.String("storage"))
	if err != nil {
		return nil, fmt.Errorf("bad --storage %q: %v", c.String("storage"), err)
	}
	o.Storage = uint64(stor)

	// User namespace parsing.
	ns, err := ParseUserNamespace(c.String("userns"))
	if err != nil {
		return nil, err
	}
	o.NamespaceMode = ns

	// Log level parsing.
	logLevel, err := parseLogLevel(c.String("log-level"))
	if err != nil {
		return nil, err
	}
	o.LogLevel = logLevel

	// Log format parsing.
	logFormat, err := parseLogFormat(c.String("log-format"))
	if err != nil {
		return nil, err
	}
	o.LogFormat = logFormat

	// Filesystem parsing.
	mode, err := parseFsMode(c.String("fs"))
	if err != nil {
		return nil, err
	}
	o.FS = mode

	// Network mode parsing.
	net, err := parseNetMode(c.String("net"))
	if err != nil {
		return nil, err
	}
	o.Net = net

	// Read-only mounts.
	for _, m := range c.StringSlice("mount-ro") {
		ms, err := parseMount(m, true)
		if err != nil {
			return nil, err
		}
		o.MountRO = append(o.MountRO, ms)
	}

	// Read-write mounts.
	for _, m := range c.StringSlice("mount-rw") {
		ms, err := parseMount(m, false)
		if err != nil {
			return nil, err
		}
		o.MountRW = append(o.MountRW, ms)
	}

	// Parse environment variables.
	var userEnv []sandbox.EnvVar
	for _, e := range c.StringSlice("env") {
		ev, err := ParseEnv(e)
		if err != nil {
			return nil, err
		}
		userEnv = append(userEnv, ev)
	}

	// Capabilities.
	addIDs, err := sandbox.FromCapabilities(c.StringSlice("cap-add"))
	if err != nil {
		return nil, fmt.Errorf("bad --cap-add: %w", err)
	}
	dropIDs, err := sandbox.FromCapabilities(c.StringSlice("cap-drop"))
	if err != nil {
		return nil, fmt.Errorf("bad --cap-drop: %w", err)
	}
	o.Capabilities = &sandbox.CapabilityOpts{
		Add:  sandbox.NewCapSet(addIDs...),
		Drop: sandbox.NewCapSet(dropIDs...),
	}

	// Merge default environment variables with user provided ones.
	o.Env = MergeEnv(defaultEnvironment, userEnv)

	// Cross-option checks.
	if o.FS.Mode == fs.FsHost && (len(o.MountRO) > 0 || len(o.MountRW) > 0) {
		return nil, errors.New("--fs host conflicts with --mount-* (requires private mount ns)")
	}

	return o, nil
}

/**
 * Parses CLI flags into an `Options` struct.
 * @param handler a function invoked to handle the command
 * @return a `Command` instance
 */
func ParseCli(ctx context.Context, args []string) (*sandbox.SandboxOptions, error) {
	var resultOpts *sandbox.SandboxOptions
	var generator = namegenerator.NewNameGenerator(
		time.Now().UTC().UnixNano(),
	)

	cmd := &cli.Command{
		Name:    "microbox",
		Usage:   "Lightweight sandboxes for Linux.",
		Version: version.Version(),
		Flags: []cli.Flag{

			// Filesystem
			&cli.StringFlag{
				Name:  "fs",
				Value: "tmpfs",
				Usage: "Root filesystem (host|tmpfs|<directory path>)",
			},

			// Network
			&cli.StringFlag{
				Name:  "net",
				Value: "none",
				Usage: "Network mode (none|host|bridge)",
			},

			// Read-only bind mounts
			&cli.StringSliceFlag{
				Name:  "mount-ro",
				Usage: "Read-only bind mounts from the host (`HOST:SANDBOX`)",
			},

			// Read-write bind mounts
			&cli.StringSliceFlag{
				Name:  "mount-rw",
				Usage: "Read-write bind mounts from the host (`HOST:SANDBOX`)",
			},

			// Read-only filesystem.
			&cli.BoolFlag{
				Name:  "readonly",
				Value: false,
				Usage: "Whether to mount the root filesystem as read-only",
			},

			// Environment variables
			&cli.StringSliceFlag{
				Name:  "env",
				Usage: "Sets an environment variable as `KEY=VALUE` in the sandbox",
			},

			// Allowed syscalls
			&cli.StringSliceFlag{
				Name:  "allow-syscall",
				Usage: "A `syscall` to allow in the sandbox",
			},

			// Denied syscalls
			&cli.StringSliceFlag{
				Name:  "deny-syscall",
				Usage: "A `syscall` to deny in the sandbox",
			},

			// DNS nameservers
			&cli.StringSliceFlag{
				Name:  "dns",
				Usage: "A DNS nameserver to use in the sandbox",
			},

			// Hostname
			&cli.StringFlag{
				Name:  "hostname",
				Value: generator.Generate(),
				Usage: "Sets the hostname of the sandbox",
			},

			// CPUs
			&cli.Float32Flag{
				Name:  "cpus",
				Value: 1.0,
				Usage: "CPU shares to allocate to the sandbox",
			},

			// Memory
			&cli.StringFlag{
				Name:  "memory",
				Value: "1GB",
				Usage: "Memory to allocate to the sandbox (e.g., 512MB, 2GB)",
			},

			// Storage
			&cli.StringFlag{
				Name:  "storage",
				Value: "512MB",
				Usage: "Storage space to allocate to the sandbox (e.g., 1GB, 10GB)",
			},

			// Verbosity
			&cli.StringFlag{
				Name:  "log-level",
				Value: "error",
				Usage: "Log verbosity (info|warn|error)",
			},

			// Log format.
			&cli.StringFlag{
				Name:  "log-format",
				Value: "text",
				Usage: "Log format (text|json)",
			},

			// Add capabilities to the sandbox.
			&cli.StringSliceFlag{
				Name:  "cap-add",
				Usage: "Add a capability to the sandbox (e.g., CAP_NET_ADMIN)",
			},

			// Remove capabilities from the sandbox.
			&cli.StringSliceFlag{
				Name:  "cap-drop",
				Usage: "Drop a capability from the sandbox (e.g., CAP_CHOWN)",
			},

			// User namespace options.
			&cli.StringFlag{
				Name:  "userns",
				Usage: "Specifies the user namespace mode (isolated|host)",
				Value: "isolated",
			},
		},

		// Parse arguments into an `Options` struct.
		Action: func(ctx context.Context, c *cli.Command) error {
			opts, err := buildOptionsFromCLI(c)
			if err != nil {
				return err
			}

			// Command to execute in the sandbox.
			argv := c.Args().Slice()
			if len(argv) == 0 {
				return fmt.Errorf("missing command; usage: microbox [options] -- command [args...]")
			}

			opts.Commands = argv
			resultOpts = opts
			return nil
		},
	}

	if err := cmd.Run(ctx, args); err != nil {
		// display help if no arguments were provided
		_ = cli.ShowAppHelp(cmd)
		return nil, err
	}

	return resultOpts, nil
}
