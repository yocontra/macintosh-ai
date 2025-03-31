## AI for Macintosh

Provides an interface to use AI on a classic Macintosh (built for System 7.0.1 - should work on others).

![Screenshot 2025-03-30 at 7 56 10 PM](https://github.com/user-attachments/assets/7071c62a-cf1f-4234-aa87-e09ee83c194d)
![Screenshot 2025-03-30 at 7 56 35 PM](https://github.com/user-attachments/assets/14663d3e-7c8e-4213-b2d2-0cf73ef832ac)
 
### Requirements

- [Retro68](https://github.com/autc04/Retro68) toolchain for cross-compiling to classic Mac OS
- [Mini vMac](https://www.gryphel.com/c/minivmac/) or [Basilisk II](https://basilisk.cebix.net/) for running the application
- [clang-format](https://clang.llvm.org/docs/ClangFormat.html) and [clang-tidy](https://clang.llvm.org/extra/clang-tidy/) for code formatting and static analysis (optional)

### Building and Running

This project uses a simple Makefile for common operations:

```bash
# Build the application
make build

# Run the application in Mini vMac
make run

# Clean build artifacts
make clean

# Format all C/C++ code
make format

# Run static analysis on all C/C++ code
make lint
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

### Code Style & Linting

This project uses clang-format and clang-tidy to maintain code quality:

- **clang-format**: A tool to automatically format C/C++ code according to configurable style guides
- **clang-tidy**: A static analysis tool that provides additional diagnostics beyond what the compiler offers

Configuration files:
- `.clang-format`: Defines the C/C++ code style (indentation, spacing, etc.)
- `.clang-tidy`: Defines static analysis rules and checks

You can run the formatting and linting tools through:
1. Makefile targets: `make format` and `make lint`
2. CMake targets: `cmake --build build --target format` and `cmake --build build --target lint`

When contributing to this project, please format your code before submitting changes:
```bash
# Format all C/C++ files according to the project style
make format
```
