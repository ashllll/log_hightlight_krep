# K(r)ep - A high-performance string search utility

![Version](https://img.shields.io/badge/version-1.2-blue)
![License](https://img.shields.io/badge/license-BSD-green)

`krep` is an optimized string search utility designed for maximum throughput and efficiency when processing large files and directories. It is built with performance in mind, offering multiple search algorithms and SIMD acceleration when available.

> **Note:**  
> Krep is not intended to be a full replacement or direct competitor to feature-rich tools like `grep` or `ripgrep`. Instead, it aims to be a minimal, efficient, and pragmatic tool focused on speed and simplicity.  
>  
> Krep provides the essential features needed for fast searching, without the extensive options and complexity of more comprehensive search utilities. Its design philosophy is to deliver the fastest possible search for the most common use cases, with a clean and minimal interface.

## 🎉 NEW: Windows DLL Release Package

This repository now includes a **complete Windows DLL release package** that provides all krep advanced features as a Windows dynamic library!

### 📦 Windows DLL Package Contents

```
krep-advanced-dll-release/
├── README.md                    # This file
├── lib/                         # Library files
│   ├── krep_advanced.dll        # Main DLL library (119KB)
│   └── libkrep_advanced.dll.a   # Import library (10KB)
├── include/                     # Header files
│   └── krep_advanced.h          # Complete API header
├── docs/                        # Documentation
│   ├── USER_GUIDE.md            # Complete usage guide (1500+ lines)
│   └── FINAL_TEST_REPORT.md     # Test report
└── examples/                    # Example code
    ├── basic_search.c           # Basic search example
    ├── file_search.c            # File search example
    ├── performance_demo.c       # Performance demo
    └── Makefile                 # Build script
```

### 🚀 Windows DLL Features

- ✅ **Smart Algorithm Selection** (Boyer-Moore, KMP, memchr, SIMD)
- ✅ **Multi-threading Architecture** (auto CPU detection, smart load balancing)
- ✅ **Memory-mapped I/O** (Windows CreateFileMapping optimization)
- ✅ **SIMD Hardware Acceleration** (SSE4.2 support)
- ✅ **File Type Detection** (binary file skip, directory filtering)
- ✅ **100% Command-line Options Support** (-i, -c, -w, -m, -t, --no-simd)

### 🎯 Quick Start (Windows DLL)

```c
#include "krep_advanced.h"

int main() {
    // 1. Define search patterns
    const char *patterns[] = {"search"};
    size_t lens[] = {6};
    
    // 2. Create search parameters
    advanced_search_params_t params = KREP_PARAMS_INIT(patterns, lens, 1);
    
    // 3. Execute search
    match_result_t *result = match_result_init(10);
    uint64_t matches = search_advanced(&params, "text to search", 14, result);
    
    // 4. Process results
    printf("Found %llu matches\n", matches);
    
    // 5. Cleanup
    match_result_free(result);
    return 0;
}
```

### 📖 Windows DLL Documentation

- **[docs/USER_GUIDE.md](docs/USER_GUIDE.md)** - Complete usage guide (1500+ lines)
- **[docs/FINAL_TEST_REPORT.md](docs/FINAL_TEST_REPORT.md)** - Test report
- **[examples/](examples/)** - Complete example programs

---

## The Story Behind the Name

The name "krep" has an interesting origin. It is inspired by the Icelandic word "kreppan," which means "to grasp quickly" or "to catch firmly." I came across this word while researching efficient techniques for pattern recognition.

Just as skilled fishers identify patterns in the water to locate fish quickly, I designed "krep" to find patterns in text with maximum efficiency. The name is also short and easy to remember—perfect for a command-line utility that users might type hundreds of times per day.

## Key Features

- **Multiple search algorithms**: Boyer-Moore-Horspool, KMP, Aho-Corasick for optimal performance across different pattern types
- **SIMD acceleration**: Uses SSE4.2, AVX2, or NEON instructions when available for blazing-fast searches
- **Memory-mapped I/O**: Maximizes throughput when processing large files
- **Multi-threaded search**: Automatically parallelizes searches across available CPU cores
- **Regex support**: POSIX Extended Regular Expression searching
- **Multiple pattern search**: Efficiently search for multiple patterns simultaneously
- **Recursive directory search**: Skip binary files and common non-code directories
- **Colored output**: Highlights matches for better readability
- **Specialized algorithms**: Optimized handling for single-character and short patterns
- **Match Limiting**: Stop searching a file after a specific number of matching lines are found.

## Installation

### Using Homebrew (macOS)

If you are on macOS and have Homebrew installed, you can install `krep` easily:

```bash
brew install krep
```

### Building from Source

```bash
# Clone the repository
git clone https://github.com/davidesantangelo/krep.git
cd krep

# Build and install
make
sudo make install

# uninstall
sudo make uninstall
```

The binary will be installed to `/usr/local/bin/krep` by default.

### Requirements

- GCC or compatible C compiler
- POSIX-compliant system (Linux, macOS, BSD)
- pthread support

### Build Options

Override default optimization settings in the Makefile:

```bash
# Disable architecture-specific optimizations
make ENABLE_ARCH_DETECTION=0
```

## Usage

```bash
krep [OPTIONS] PATTERN [FILE | DIRECTORY]
krep [OPTIONS] -e PATTERN [FILE | DIRECTORY]
krep [OPTIONS] -f FILE [FILE | DIRECTORY]
krep [OPTIONS] -s PATTERN STRING_TO_SEARCH
krep [OPTIONS] PATTERN < FILE
cat FILE | krep [OPTIONS] PATTERN
```

## Usage Examples

Search for a fixed string in a file:
```bash
krep -F "value: 100%" config.ini
```

Search recursively:
```bash
krep -r "function" ./project
```

Whole word search (matches only complete words):
```bash
krep -w 'cat' samples/text.en
```

Use with piped input:
```bash
cat krep.c | krep 'c'
```

## Command Line Options

- `-i, --ignore-case` Case-insensitive search
- `-c, --count` Count matching lines only
- `-o, --only-matching` Print only the matched parts of lines
- `-e PATTERN, --pattern=PATTERN` Specify pattern(s). Can be used multiple times.
- `-f FILE, --file=FILE` Read patterns from FILE, one per line.
- `-m NUM, --max-count=NUM` Stop searching each file after finding NUM matching lines.
- `-E, --extended-regexp` Use POSIX Extended Regular Expressions
- `-F, --fixed-strings` Interpret pattern as fixed string(s) (default unless -E is used)
- `-r, --recursive` Recursively search directories
- `-t NUM, --threads=NUM` Use NUM threads for file search (default: auto)
- `-s STRING, --string=STRING` Search in the provided STRING instead of file(s)
- `-w, --word-regexp` Match only whole words
- `--color[=WHEN]` Control color output ('always', 'never', 'auto')
- `--no-simd` Explicitly disable SIMD acceleration
- `-v, --version` Show version information
- `-h, --help` Show help message

## Performance Benchmarks

Comparing performance on the same text file with identical search pattern:

| Tool    | Time (seconds) | CPU Usage |
|---------|---------------:|----------:|
| krep    |         0.106  |     328%  |
| grep    |         4.400  |      99%  |
| ripgrep |         0.115  |      97%  |

*Krep is approximately 41.5x faster than grep and slightly faster than ripgrep in this test. Benchmarks performed on Mac Mini M4 with 24GB RAM.*

The benchmarks above were conducted using the subtitles2016-sample.en.gz dataset, which can be obtained with:
```bash
curl -LO 'https://burntsushi.net/stuff/subtitles2016-sample.en.gz'
```

## How Krep Works

Krep achieves its high performance through several key techniques:

### 1. Smart Algorithm Selection

Krep automatically selects the optimal search algorithm based on the pattern and available hardware:

- **Boyer-Moore-Horspool** for most literal string searches
- **Knuth-Morris-Pratt (KMP)** for very short patterns and repetitive patterns
- **memchr optimization** for single-character patterns
- **SIMD Acceleration** (SSE4.2, AVX2, or NEON) for compatible hardware
- **Regex Engine** for regular expression patterns
- **Aho-Corasick** for efficient multiple pattern matching

### 2. Multi-threading Architecture

Krep utilizes parallel processing to dramatically speed up searches:

- Automatically detects available CPU cores
- Divides large files into chunks for parallel processing
- Implements thread pooling for maximum efficiency
- Optimized thread count selection based on file size
- Careful boundary handling to ensure no matches are missed

### 3. Memory-Mapped I/O

Instead of traditional read operations:

- Memory maps files for direct access by the CPU
- Significantly reduces I/O overhead
- Enables CPU cache optimization
- Progressive prefetching for larger files

### 4. Optimized Data Structures

- Zero-copy architecture where possible
- Efficient match position tracking
- Lock-free aggregation of results

### 5. Skipping Non-Relevant Content

When using recursive search (`-r`), Krep automatically:
- Skips common binary file types
- Ignores version control directories (`.git`, `.svn`)
- Bypasses dependency directories (`node_modules`, `venv`)
- Detects binary content to avoid searching non-text files

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Author

- **Davide Santangelo** - [GitHub](https://github.com/davidesantangelo)

## License

This project is licensed under the BSD-2 License - see the LICENSE file for details.

Copyright © 2025 Davide Santangelo