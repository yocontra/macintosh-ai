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

You can customize the build with these options:

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

### Features

- Press Cmd-L to open a chat window with the AI assistant
- Press Cmd-I when focused on a text input to get AI assistance
- Use Cmd-I with selected text to modify only the selection