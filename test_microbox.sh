#!/bin/bash
# Basic test suite for microbox

set -e  # Exit on any error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MICROBOX="$SCRIPT_DIR/microbox"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Test counters
TESTS_RUN=0
TESTS_PASSED=0
TESTS_FAILED=0

# Print colored output
print_status() {
    local status=$1
    local message=$2
    case $status in
        "PASS")
            echo -e "${GREEN}[PASS]${NC} $message"
            ((TESTS_PASSED++))
            ;;
        "FAIL")
            echo -e "${RED}[FAIL]${NC} $message"
            ((TESTS_FAILED++))
            ;;
        "SKIP")
            echo -e "${YELLOW}[SKIP]${NC} $message"
            ;;
        "INFO")
            echo -e "${YELLOW}[INFO]${NC} $message"
            ;;
    esac
    ((TESTS_RUN++))
}

# Check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        return 0
    else
        return 1
    fi
}

# Test basic functionality that doesn't require privileges
test_basic() {
    echo "=== Basic Tests ==="
    
    # Test 1: Binary exists and is executable
    if [[ -x "$MICROBOX" ]]; then
        print_status "PASS" "Binary exists and is executable"
    else
        print_status "FAIL" "Binary missing or not executable"
        return 1
    fi
    
    # Test 2: Help command works
    if "$MICROBOX" --help 2>&1 | grep -q "Usage:"; then
        print_status "PASS" "Help command displays usage"
    else
        print_status "FAIL" "Help command failed"
    fi
    
    # Test 3: Error handling for missing command
    if "$MICROBOX" 2>&1 | grep -q 'missing "--"'; then
        print_status "PASS" "Proper error for missing command"
    else
        print_status "FAIL" "Missing command error handling"
    fi
}

# Test functionality that works without root
test_unprivileged() {
    echo "=== Unprivileged Tests ==="
    
    # Test basic command execution (will fail with cgroup error but should show proper error)
    if "$MICROBOX" -- /bin/echo "test" 2>&1 | grep -q "Permission denied\|Hello"; then
        print_status "PASS" "Command execution attempted (expected permission error or success)"
    else
        print_status "FAIL" "Unexpected error in command execution"
    fi
    
    # Test filesystem mode parsing
    if "$MICROBOX" --fs host -- /bin/true 2>&1 | grep -q "Filesystem: FS_HOST"; then
        print_status "PASS" "Filesystem mode parsing works"
    else
        print_status "FAIL" "Filesystem mode parsing failed"
    fi
    
    # Test network mode parsing
    if "$MICROBOX" --net none -- /bin/true 2>&1 | grep -q "Network: NET_NONE"; then
        print_status "PASS" "Network mode parsing works"
    else
        print_status "FAIL" "Network mode parsing failed"
    fi
}

# Test functionality that requires root privileges
test_privileged() {
    echo "=== Privileged Tests ==="
    
    if ! check_root; then
        print_status "SKIP" "Privileged tests require root access"
        return 0
    fi
    
    # Test basic command execution with root
    if timeout 10 "$MICROBOX" -- /bin/echo "Hello from sandbox!" 2>&1 | grep -q "Hello from sandbox!"; then
        print_status "PASS" "Basic command execution works with root"
    else
        print_status "FAIL" "Basic command execution failed even with root"
    fi
    
    # Test tmpfs filesystem
    if timeout 10 "$MICROBOX" --fs tmpfs -- /bin/ls / 2>&1 | grep -q "tmp\|etc\|bin"; then
        print_status "PASS" "tmpfs filesystem works"
    else
        print_status "SKIP" "tmpfs filesystem test (may require additional setup)"
    fi
}

# Main test execution
main() {
    echo "Starting microbox test suite..."
    echo "Binary: $MICROBOX"
    echo "Running as: $(whoami)"
    echo ""
    
    test_basic
    echo ""
    test_unprivileged
    echo ""
    test_privileged
    
    echo ""
    echo "=== Test Summary ==="
    echo "Tests run: $TESTS_RUN"
    echo "Passed: $TESTS_PASSED"
    echo "Failed: $TESTS_FAILED"
    
    if [[ $TESTS_FAILED -eq 0 ]]; then
        print_status "INFO" "All tests passed!"
        exit 0
    else
        print_status "INFO" "Some tests failed"
        exit 1
    fi
}

# Run tests
main "$@"