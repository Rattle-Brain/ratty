<img src="resources/images/ratty-logo.png" alt="Ratty Logo" align="right" width="200"/>

# Ratty

A GPU-accelerated terminal emulator built with modern C++ and OpenGL.

## Overview

Ratty is a Unix-based terminal emulator focused on performance and GPU-accelerated rendering. Built with Qt6 and OpenGL, it aims to provide a fast, responsive terminal experience with efficient text rendering using glyph atlases and hardware acceleration.

**Platform Support:**
- ✅ macOS
- ✅ Linux
- ❌ Windows (not supported, no plans to add support)

### Current Status

**Early Development (v0.1.0)** - The project is actively under development with core foundations in place:

- ✅ PTY (pseudo-terminal) implementation
- ✅ Qt6-based GUI framework with OpenGL rendering context
- ✅ FreeType font loading and glyph rasterization
- ✅ GPU-based text rendering pipeline with shader support
- ✅ Tab and split-pane support architecture
- 🚧 VT escape sequence parsing (in progress)
- 🚧 Full terminal emulation features (in progress)
- 📋 Planned: Configuration system, scrollback buffer, mouse support

## Dependencies

### Required

- **CMake** >= 3.16
- **Clang** C++ compiler with C++17 support
- **Make** or another CMake-supported build system
- **Qt6** (Core, Widgets, OpenGL, OpenGLWidgets modules)
- **FreeType** >= 2.0 for font rendering
- **OpenGL** 3.3+ compatible graphics driver
- **util library** (platform-specific - typically pre-installed on Unix systems)

### Platform-Specific Notes

#### macOS
- Xcode Command Line Tools or full Xcode installation recommended
- Qt6 can be installed via Homebrew: `brew install qt@6`
- FreeType can be installed via Homebrew: `brew install freetype`

#### Linux
- Install development packages for Qt6 and FreeType via your distribution's package manager
- Example for Ubuntu/Debian:
  ```bash
  sudo apt install build-essential cmake clang qt6-base-dev libfreetype6-dev libgl1-mesa-dev
  ```

## Building from Source

### Quick Start

```bash
# Clone the repository
git clone https://github.com/Rattle-Brain/ratty.git
cd ratty

# Create build directory
mkdir -p build && cd build

# Configure with CMake (using Clang)
cmake -DCMAKE_CXX_COMPILER=clang++ ..

# Build
make

# Run
./ratty
```

### Detailed Build Instructions

1. **Configure the project**
   ```bash
   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
   ```

2. **Build the executable**
   ```bash
   cmake --build build --parallel
   ```

3. **Run the terminal**
   ```bash
   ./build/ratty
   ```

### Build Options

- **Debug build**: Replace `Release` with `Debug` in the configure step
- **Custom Qt6 installation**: Set `Qt6_DIR` to your Qt installation path
  ```bash
  cmake -S . -B build -DQt6_DIR=/path/to/qt6/lib/cmake/Qt6
  ```

### Generating compile_commands.json for LSP/IDE Support

The `compile_commands.json` file helps Language Server Protocols (LSP) and IDEs understand your project structure, including header locations and library dependencies. This prevents false errors and enables accurate code completion, navigation, and diagnostics.

**Method 1: Using Bear (recommended)**
```bash
# Install bear if not already installed
# macOS: brew install bear
# Linux: sudo apt install bear

# Generate compile_commands.json in the project root
bear -- make
```

**Method 2: Using CMake (if Method 1 doesn't work)**
```bash
# Generate compile_commands.json in the current directory
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON .

# Build the project
make
```

Most LSP implementations (clangd, ccls) will automatically detect `compile_commands.json` in the project root directory.

### Troubleshooting

- **Qt6 not found**: Ensure Qt6 is installed and `Qt6_DIR` points to the correct location
- **FreeType not found**: Install the FreeType development package for your platform
- **OpenGL errors**: Ensure you have a compatible OpenGL driver (3.3+)
- **Build artifacts in root**: Run `make clean` or remove build artifacts manually

## Contributing

Contributions are welcome! Here's how to get started:

### Opening Pull Requests

1. **Fork the repository** and create a feature branch
   ```bash
   git checkout -b feature/your-feature-name
   ```

2. **Make your changes**
   - Follow the existing code style and structure
   - Keep commits focused and atomic
   - Write clear commit messages in imperative mood (e.g., "Add feature" not "Added feature")

3. **Test your changes**
   - Ensure the project builds successfully
   - Test the functionality on your platform
   - Check for compiler warnings

4. **Submit a pull request**
   - Provide a clear description of what your PR does
   - Reference any related issues
   - Explain the motivation and context for the change

### Coding Guidelines

- **C++ Standard**: Use C++17 features appropriately
- **Code Style**: Follow the existing style in the codebase
  - Use 4 spaces for indentation
  - Place braces on the same line for control structures
  - Use descriptive variable and function names
- **Header Files**: Include guards are handled by the build system
- **Comments**: Write clear comments for complex logic; prefer self-documenting code

### Development Workflow

1. Check existing issues for planned work or bugs
2. Open an issue for new features or significant changes before starting work
3. Keep PRs focused on a single feature or fix
4. Be responsive to code review feedback

### Areas for Contribution

- VT escape sequence handling and ANSI compliance
- Performance optimizations for rendering
- Configuration system implementation
- macOS and Linux testing and bug fixes
- Documentation improvements

### Code of Conduct

- Be respectful and constructive in discussions
- Focus on the code and ideas, not individuals
- Help maintain a welcoming environment for all contributors

## Project Structure

```
ratty/
├── src/
│   ├── core/          # PTY and terminal core logic
│   ├── ui/            # Qt widgets and UI components
│   ├── render/        # OpenGL rendering and font management
│   └── config/        # Configuration handling
├── resources/
│   └── shaders/       # GLSL shader files
├── CMakeLists.txt     # Build configuration
└── README.md
```

## License

This project is licensed under the **GNU General Public License v2.0** (GPL-2.0).

See the [LICENSE](LICENSE) file for the full license text.

### What this means:

- ✅ You are free to use, modify, and distribute this software
- ✅ You can use it for commercial purposes
- ⚠️ If you distribute modified versions, you must:
  - Make the source code available
  - License it under GPL-2.0
  - State your changes
- ⚠️ This software comes with NO WARRANTY

For more information about GPL-2.0, visit: https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

## Acknowledgments

Ratty is inspired by modern terminal emulators like Kitty and Alacritty, focusing on GPU acceleration and performance.

Built with:
- [Qt6](https://www.qt.io/) - Cross-platform application framework
- [FreeType](https://freetype.org/) - Font rendering library
- [OpenGL](https://www.opengl.org/) - Graphics rendering API

## Contact & Support

- **Issues**: Report bugs or request features via [GitHub Issues](https://github.com/Rattle-Brain/ratty/issues)
- **Discussions**: For questions and general discussion, use [GitHub Discussions](https://github.com/Rattle-Brain/ratty/discussions)

---

**Note**: Ratty is in active development. APIs and features may change as the project evolves.
