#!/usr/bin/env python3
"""CLI integration tests for upscayl-bin.

These tests run the compiled binary with various argument combinations
and verify exit codes and error messages. They do NOT require a GPU
because they test argument validation paths that exit before GPU init.

Usage:
    UPSCAYL_BIN=build/upscayl-bin python3 tests/test_cli_integration.py

Set UPSCAYL_GPU_TESTS=1 to also run GPU-dependent tests (requires models/).
"""
import subprocess
import os
import sys
import tempfile

BINARY = os.environ.get("UPSCAYL_BIN", "build/upscayl-bin")


def run(args, timeout=10):
    """Run the binary with args, return (returncode, stdout, stderr)."""
    result = subprocess.run(
        [BINARY] + args,
        capture_output=True, text=True, timeout=timeout
    )
    return result.returncode, result.stdout, result.stderr


def test_no_args_shows_usage():
    """No arguments should print usage and exit non-zero."""
    rc, out, err = run([])
    assert rc != 0, f"Expected non-zero exit with no args, got {rc}"
    assert "Usage:" in err, f"Expected usage message in stderr, got: {err[:200]}"


def test_missing_output_shows_usage():
    """Missing -o should print usage and exit non-zero."""
    rc, out, err = run(["-i", "somefile.png"])
    assert rc != 0, f"Expected non-zero exit with missing -o, got {rc}"
    assert "Usage:" in err, f"Expected usage message, got: {err[:200]}"


def test_help_flag():
    """-h should print usage."""
    rc, out, err = run(["-h"])
    assert "Usage:" in err, f"Expected usage in stderr, got: {err[:200]}"


def test_invalid_format():
    """Invalid -f format should error."""
    rc, out, err = run(["-i", "x.png", "-o", "y.png", "-f", "bmp"])
    assert rc != 0, f"Expected non-zero exit for invalid format, got {rc}"


def test_invalid_tile_size():
    """Tile size < 32 (and != 0) should error."""
    with tempfile.NamedTemporaryFile(suffix=".png") as f:
        rc, out, err = run(["-i", f.name, "-o", "out.png", "-t", "16"])
        assert rc != 0, f"Expected non-zero exit for tile size 16, got {rc}"


def test_invalid_thread_count():
    """Thread count of 0 should error."""
    with tempfile.NamedTemporaryFile(suffix=".png") as f:
        rc, out, err = run(["-i", f.name, "-o", "out.png", "-j", "0:2:2"])
        assert rc != 0, f"Expected non-zero exit for 0 load threads, got {rc}"


def test_resize_help():
    """-r help should print resize usage info."""
    rc, out, err = run(["-i", "x.png", "-o", "y.png", "-r", "help"])
    # This prints to stdout via printf
    combined = out + err
    assert "filter" in combined.lower() or "default" in combined.lower(), \
        f"Expected resize help output, got: {combined[:200]}"


if __name__ == "__main__":
    if not os.path.isfile(BINARY):
        print(f"Binary not found at {BINARY}")
        print("Build first with: make build")
        sys.exit(1)

    tests = [(name, func) for name, func in sorted(globals().items())
             if name.startswith("test_") and callable(func)]

    passed = failed = skipped = 0
    for name, test in tests:
        try:
            test()
            print(f"  PASS: {name}")
            passed += 1
        except AssertionError as e:
            print(f"  FAIL: {name}: {e}")
            failed += 1
        except Exception as e:
            print(f"  ERROR: {name}: {e}")
            failed += 1

    print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
    sys.exit(1 if failed else 0)
