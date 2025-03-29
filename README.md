## AI for Macintosh

Provides an interface to use AI on a classic Macintosh (tested on System 7.0.1).

### Requirements

- [Retro68](https://github.com/autc04/Retro68) toolchain for cross-compiling to classic Mac OS
- [Mini vMac](https://www.gryphel.com/c/minivmac/) or [Basilisk II](https://basilisk.cebix.net/) for running the application

### Building and Running

This project uses a simple Makefile for common operations:

```bash
# Build the application
make build

# Run the application in Mini vMac
make run

# Clean build artifacts
make clean
```

You can find the output .dsk and .bin files in the `dist/` folder. Simply drag the .dsk file onto your Mini vMac to test.

#### Other Build Parameters

```bash
# Specify a custom Retro68 build path
make build RETRO68_PATH=/path/to/Retro68-build

# Use Basilisk II instead of Mini vMac
make run USE_BASILISK=ON

# Change the application name (default is "App")
make build APP_NAME=MyApp

# Combine options
make build RETRO68_PATH=/path/to/Retro68-build USE_BASILISK=ON APP_NAME=MyApp
```

### Using `make run`

You will need to set up LaunchAPPL.cfg as documented [here](https://github.com/autc04/Retro68/tree/3672e5e663802e1956407065c75d2aff130ae50e?tab=readme-ov-file#launchappl-and-the-test-suite). Currently this supports Mini vMac and Basilisk II (I personally use Mini vMac for simplicity).

This doesn't seem to work super well for Mini vMac at the moment due to how it copies the .app file and Gatekeeper on modern macOS.