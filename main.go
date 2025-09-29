//go:build linux

package main

import (
	"context"
	"fmt"
	"log/slog"
	"os"

	"github.com/HQarroum/microbox/logger"
	"github.com/HQarroum/microbox/options"
	"github.com/HQarroum/microbox/sandbox"
)

/**
 * Application entry point.
 */
func main() {
	// Parse command-line options.
	opts, err := options.ParseCli(context.Background(), os.Args)
	if err != nil {
		fmt.Fprintln(os.Stderr, "parsing error:", err)
		os.Exit(1)
	} else if opts == nil {
		// No options means help or version was printed.
		os.Exit(0)
	}

	// Create the application logger.
	log := logger.CreateLogger(&logger.LoggerOpts{
		LogLevel:  opts.LogLevel,
		LogFormat: opts.LogFormat,
	})
	log.Info("Options", slog.Any("opts", opts))

	// Spawn a new sandboxed process.
	box, err := sandbox.NewSandbox(opts)
	if err != nil {
		log.Error("error while creating sandbox", slog.Any("err", err))
		os.Exit(1)
	}

	// Wait for the sandboxed process to finish.
	code, err := box.Wait()
	if err != nil {
		log.Error("error while executing for sandbox", slog.Any("err", err))
		os.Exit(1)
	}

	os.Exit(code)
}
