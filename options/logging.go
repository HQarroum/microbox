//go:build linux

package options

import (
	"fmt"
	"log/slog"

	"github.com/HQarroum/microbox/logger"
)

/**
 * Parse the log level from a string.
 * @param s the string to parse
 * @return the parsed log level and error if any
 */
func parseLogLevel(s string) (slog.Level, error) {
	switch s {
	case "info":
		return slog.LevelInfo, nil
	case "warn":
		return slog.LevelWarn, nil
	case "error":
		return slog.LevelError, nil
	default:
		return slog.LevelError, fmt.Errorf("unknown log level: %q", s)
	}
}

/**
 * Parse the log format from a string.
 * @param s the string to parse
 * @return the parsed log format and error if any
 */
func parseLogFormat(s string) (logger.LogFormat, error) {
	switch s {
	case "text":
		return logger.LogText, nil
	case "json":
		return logger.LogJSON, nil
	default:
		return logger.LogText, fmt.Errorf("unknown log format: %q", s)
	}
}
