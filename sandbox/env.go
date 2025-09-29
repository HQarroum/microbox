package sandbox

import "fmt"

/**
 * Environment variable type.
 */
type EnvVar struct {
	Key string `json:"key"`
	Val string `json:"value"`
}

/**
 * Represents a list of environment variables.
 */
type EnvVars []EnvVar

/**
 * Convert environment variables to a string array in
 * the form of KEY=VALUE.
 * @return the string array
 */
func (env EnvVars) ToStringArray() []string {
	var result []string

	for _, e := range env {
		result = append(result, fmt.Sprintf("%s=%s", e.Key, e.Val))
	}
	return result
}
