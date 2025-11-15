# CLAUDE.md - AI Assistant Guide for MC75 HomeBox Client

## Project Overview

**MC75 HomeBox Client** is a native C++ application for Motorola MC75 handheld devices running Windows Mobile 6 Professional. The application integrates with the HomeBox inventory management system, providing scanning, data synchronization, and offline queue management capabilities.

### Key Technologies
- **Language**: C++ (Visual Studio 2008)
- **Platform**: Windows Mobile 6 Professional SDK (ARMV4I)
- **Target Device**: Motorola MC75 handheld scanner
- **SDK Dependencies**:
  - Windows Mobile 6 SDK
  - Zebra EMDK (Enterprise Mobility Development Kit)
- **Build System**: Visual Studio 2008 solution/project files (.sln, .vcproj)

## Repository Structure

```
mc75-homebox-client/
├── src/                          # Source files (.cpp)
│   ├── main.cpp                  # Application entry point
│   ├── Controller.cpp            # Main application controller
│   ├── HbClient.cpp              # HomeBox API client
│   ├── HttpClient.cpp            # HTTP communication layer
│   ├── SyncEngine.cpp            # Offline/online sync management
│   ├── Journal.cpp               # Transaction journaling
│   ├── ScannerHAL.cpp            # Hardware abstraction for scanner
│   ├── Config.cpp                # Configuration management
│   ├── Views/                    # UI view components
│   │   ├── ScanView.cpp          # Barcode scanning interface
│   │   ├── ItemView.cpp          # Item display/edit interface
│   │   ├── QueueView.cpp         # Offline queue management
│   │   └── ViewHelpers.cpp       # UI utility functions
│   └── Models/                   # Data models
│       ├── Item.cpp              # Item entity
│       ├── Location.cpp          # Location entity
│       └── JsonLite.cpp          # Lightweight JSON parser
│
├── include/                      # Header files (.hpp, .h)
│   ├── Views/                    # View headers
│   └── Models/                   # Model headers
│
├── proj/                         # Visual Studio project files
│   ├── HBXClient.vcproj          # Main application project
│   └── HBXClientCab.vcproj       # CAB installer project
│
├── resources/                    # Application resources
│   ├── icons/                    # Icon files
│   ├── layout.rc                 # UI layout resources
│   ├── strings.rc                # String resources
│   └── resource.h                # Resource definitions
│
├── tests/                        # Test files
│   ├── unit/                     # Unit tests
│   │   ├── test_json.cpp
│   │   ├── test_journal.cpp
│   │   └── test_http.cpp
│   └── integration/              # Integration tests
│       ├── test_api_endpoints.cpp
│       └── test_offline_sync.cpp
│
├── scripts/                      # Build and deployment scripts
│   ├── build_winmobile.bat       # Windows Mobile build script
│   ├── build_host_debug.sh       # Host debug build script
│   └── deploy_to_device.bat      # Device deployment script
│
├── docs/                         # Documentation
│   ├── API_NOTES.md
│   ├── DESIGN.md
│   ├── BUILD.md
│   └── DEPLOYMENT.md
│
├── bin/                          # Build output (gitignored)
│   ├── Debug/
│   └── Release/
│
├── obj/                          # Intermediate build files (gitignored)
│
└── mc75-homebox-client.sln       # Visual Studio solution file
```

## Architecture

### Design Pattern: MVC-like Structure
The application follows a modified MVC pattern suitable for embedded Windows Mobile development:

- **Models** (`src/Models/`, `include/Models/`): Data entities and lightweight JSON parsing
- **Views** (`src/Views/`, `include/Views/`): UI components for scanning, item display, and queue management
- **Controller** (`Controller.cpp`): Orchestrates application flow and coordinates between views and models

### Core Components

1. **HbClient** (`HbClient.cpp`, `HbClient.hpp`)
   - Handles communication with HomeBox backend API
   - Manages authentication and API requests

2. **HttpClient** (`HttpClient.cpp`, `HttpClient.hpp`)
   - Low-level HTTP communication
   - Handles network requests over WinSock

3. **SyncEngine** (`SyncEngine.cpp`, `SyncEngine.hpp`)
   - Manages offline/online synchronization
   - Queues transactions when offline
   - Syncs pending operations when connectivity is restored

4. **Journal** (`Journal.cpp`, `Journal.hpp`)
   - Transaction logging and persistence
   - Provides audit trail and recovery mechanism

5. **ScannerHAL** (`ScannerHAL.hpp`)
   - Hardware Abstraction Layer for Motorola MC75 scanner
   - Interfaces with Zebra EMDK

6. **JsonLite** (`Models/JsonLite.cpp`)
   - Lightweight JSON parsing library
   - Minimal footprint for embedded environment

## Build System

### Solution Structure
- **HBXClient**: Main executable project
- **HBXClientCab**: CAB installer project (depends on HBXClient)

### Build Configurations
- **Debug|Windows Mobile 6 Professional SDK (ARMV4I)**
  - Output: `bin/Debug/HBXClient.exe`
  - Intermediate: `obj/Debug/`
  - Runtime Library: Multi-threaded Debug DLL (/MDd)
  - Optimization: Disabled

- **Release|Windows Mobile 6 Professional SDK (ARMV4I)**
  - Output: `bin/Release/HBXClient.exe`
  - Intermediate: `obj/Release/`
  - Runtime Library: Multi-threaded DLL (/MD)
  - Optimization: Maximize Speed (/O2)
  - Inline: Any suitable (/Ob2)

### Build Dependencies
```
Required Libraries:
- coredll.lib      (Windows CE core)
- aygshell.lib     (Application shell)
- commctrl.lib     (Common controls)
- ole32.lib        (OLE support)
- oleaut32.lib     (OLE automation)
- winsock.lib      (Networking)
```

### Build Scripts
- `scripts/build_winmobile.bat`: Windows Mobile device build
- `scripts/build_host_debug.sh`: Host machine debug build (for development/testing)
- `scripts/deploy_to_device.bat`: Deploys CAB to connected MC75 device

## Development Workflow

### For AI Assistants: Key Considerations

1. **Platform Constraints**
   - This is legacy Windows Mobile 6 code (not modern Windows)
   - Limited C++ standard library support (pre-C++11 era)
   - No STL exceptions in many cases
   - Memory-constrained embedded environment
   - ARMV4I architecture (ARM 32-bit)

2. **Coding Conventions**
   - Use `.cpp` for implementation files, `.hpp` for headers
   - Headers go in `include/`, implementations in `src/`
   - Use Windows-1252 encoding for .vcproj files
   - Unicode character set (CharacterSet="1")
   - Preprocessor defines: `WIN32`, `_WIN32_WCE=0x0600`, `UNDER_CE`, `WIN32_PLATFORM_PSPC`

3. **When Adding New Files**
   - Add `.cpp` files to `src/` (or appropriate subdirectory)
   - Add `.hpp` files to `include/` (or appropriate subdirectory)
   - **MUST** update `proj/HBXClient.vcproj` to include new files
   - Use relative paths from `proj/` directory (e.g., `..\src\NewFile.cpp`)
   - Add to appropriate `<Filter>` section in .vcproj for organization

4. **Resource Files**
   - UI layouts: `resources/layout.rc`
   - Strings: `resources/strings.rc`
   - Icons: `resources/icons/`
   - Resource IDs: `resources/resource.h`

5. **Testing**
   - Unit tests go in `tests/unit/`
   - Integration tests go in `tests/integration/`
   - Test files follow `test_*.cpp` naming convention

### Typical Development Tasks

#### Adding a New Feature
1. Create header file in `include/` (or subdirectory)
2. Create implementation file in `src/` (or subdirectory)
3. Update `proj/HBXClient.vcproj` to include both files
4. Add unit tests in `tests/unit/`
5. Update documentation in `docs/` if needed

#### Modifying Build Configuration
1. Edit `proj/HBXClient.vcproj` for compiler/linker settings
2. Edit `proj/HBXClientCab.vcproj` for installer configuration
3. Update build scripts in `scripts/` if needed

#### Working with the API
1. API client code is in `HbClient.cpp`
2. HTTP layer is in `HttpClient.cpp`
3. JSON parsing uses `JsonLite.cpp` (lightweight parser)
4. API notes should be documented in `docs/API_NOTES.md`

## Git Workflow

### Branch Strategy
- **main**: Stable release branch
- **claude/**: Prefix for AI assistant development branches
- Format: `claude/claude-md-<identifier>-<session-id>`

### Commit Guidelines
- Write clear, descriptive commit messages
- Reference issue numbers when applicable
- Commit logical units of work
- Test before committing when possible

### Push Requirements
- Always use: `git push -u origin <branch-name>`
- Branch must start with `claude/` and match session ID
- Retry on network failures (up to 4 times with exponential backoff: 2s, 4s, 8s, 16s)

## Common Pitfalls and Best Practices

### Pitfalls to Avoid
1. **Do not use modern C++ features** (C++11/14/17/20)
   - No `auto`, range-based for loops, lambdas, smart pointers, etc.
   - Compiler is Visual Studio 2008 (pre-C++11)

2. **Memory management**
   - Manual memory management (new/delete)
   - Be careful with memory leaks on embedded device
   - Limited heap space available

3. **String handling**
   - Windows CE uses Unicode (WCHAR, TCHAR)
   - Use Windows string functions, not standard C++
   - Be mindful of Unicode vs. ANSI conversions

4. **File paths in .vcproj**
   - Must use relative paths from `proj/` directory
   - Use `..` to navigate up, forward slashes or backslashes both work
   - Paths are case-sensitive in XML

5. **Network code**
   - Assume intermittent connectivity
   - All network operations should work with SyncEngine
   - Queue operations when offline

### Best Practices

1. **Before making changes**
   - Read related source files to understand existing patterns
   - Check if similar functionality already exists
   - Consider memory and performance implications

2. **When adding dependencies**
   - Minimize external dependencies (embedded device)
   - Prefer Windows CE APIs over third-party libraries
   - Document any new library dependencies

3. **UI Development**
   - Keep UI responsive (avoid blocking operations)
   - Use resource files for strings (localization support)
   - Test on actual device resolution (MC75 screen)

4. **Error Handling**
   - Log errors to Journal for debugging
   - Provide user-friendly error messages
   - Handle network failures gracefully

5. **Testing**
   - Write unit tests for new functionality
   - Test offline scenarios (sync queue)
   - Test on actual MC75 device when possible

## Configuration

### Application Configuration
- Configuration file: `hb_conf.json` (deployed with CAB)
- Format: JSON
- Contains: API endpoints, authentication, device settings

### Deployment Package
The CAB installer (`HBXClientCab`) includes:
- `HBXClient.exe` (main executable)
- Resource files (strings.rc, layout.rc in Debug)
- `hb_conf.json` (if exists)
- Install location: `\Program Files\HBXClient\`

## Troubleshooting

### Build Issues
- Verify Windows Mobile 6 SDK is installed at expected path
- Check Zebra EMDK installation
- Ensure .vcproj paths are correct (relative from `proj/`)
- Check all referenced files exist

### Runtime Issues
- Check device connectivity
- Verify EMDK scanner drivers are installed on device
- Check application logs/journal
- Verify API endpoint configuration in hb_conf.json

### Sync Issues
- Check Journal for queued transactions
- Verify network connectivity
- Check SyncEngine state
- Review offline queue in QueueView

## Quick Reference

### File Extensions
- `.cpp`: C++ implementation files
- `.hpp`, `.h`: C++ header files
- `.vcproj`: Visual Studio project file (XML)
- `.sln`: Visual Studio solution file
- `.rc`: Resource script file
- `.cab`, `.CAB`: Windows CE installer package

### Important Paths
- Include directories: `../include`, Windows Mobile SDK, Zebra EMDK
- Library directories: Windows Mobile SDK lib, Zebra EMDK lib
- Output: `../bin/{Debug|Release}/`
- Intermediate: `../obj/{Debug|Release}/`

### Build Artifacts (gitignored)
- `*.exe`: Executables
- `*.obj`: Object files
- `*.pdb`: Debug symbols
- `*.cab`: Installer packages
- `/build/`: Build output directory

## Additional Resources

### Documentation Files
- `docs/API_NOTES.md`: API integration notes
- `docs/DESIGN.md`: Architecture and design decisions
- `docs/BUILD.md`: Detailed build instructions
- `docs/DEPLOYMENT.md`: Deployment procedures

### External Documentation
- Windows Mobile 6 SDK Documentation
- Zebra EMDK for C Documentation
- Visual Studio 2008 Documentation
- Windows CE API Reference

## Notes for AI Assistants

When working with this codebase:

1. **Always check Visual Studio project files** when adding/removing source files
2. **Respect the platform limitations** - this is embedded C++ from 2008 era
3. **Consider offline-first design** - MC75 devices often operate without connectivity
4. **Memory and performance matter** - embedded device with limited resources
5. **Test assumptions** - read existing code to understand patterns before implementing
6. **Update documentation** - keep docs/ files current with changes
7. **Preserve XML formatting** in .vcproj files (Windows-1252, proper indentation)

### When in Doubt
- Check existing similar code for patterns
- Read the Visual Studio project file to understand build configuration
- Consider asking the user about platform-specific behavior
- Document assumptions and decisions in code comments

## Version Information

- **Visual Studio**: 2008 (Format Version 10.00)
- **Platform SDK**: Windows Mobile 6 Professional SDK (ARMV4I)
- **Application Version**: 1.0.0.0 (configurable in HBXClientCab.vcproj)
- **Manufacturer**: Homestead
- **Product Name**: HBXClient

---

**Last Updated**: 2025-11-15
**Target Platform**: Motorola MC75, Windows Mobile 6 Professional
