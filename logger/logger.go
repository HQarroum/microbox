//go:build linux

package logger

import (
	"log/slog"
	"os"
)

/**
 * Represents a log format.
 */
type LogFormat int

/**
 * Supported log formats.
 */
const (
	LogText LogFormat = iota
	LogJSON
)

/**
 * Logger options.
 */
type LoggerOpts struct {
	LogLevel  slog.Level
	LogFormat LogFormat
}

/**
 * The global logger instance.
 */
var Log *slog.Logger

/**
 * Creates a global structured logger.
 * @param opts the logger options.
 * @return the created logger instance.
 */
func CreateLogger(opts *LoggerOpts) *slog.Logger {
	var logHandler slog.Handler

	if Log != nil {
		return Log
	}

	handlerOpts := &slog.HandlerOptions{
		Level: opts.LogLevel,
	}

	// Choose the log format.
	if opts.LogFormat == LogText {
		logHandler = slog.NewTextHandler(os.Stdout, handlerOpts)
	} else {
		logHandler = slog.NewJSONHandler(os.Stdout, handlerOpts)
	}

	// Create a new structured logger.
	logger := slog.New(logHandler)

	// Add context fields.
	Log = logger.With(
		slog.Int("pid", os.Getpid()),
	)

	// Set as the default logger.
	slog.SetDefault(Log)

	return Log
}
