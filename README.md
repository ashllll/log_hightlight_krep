# krep - A high-performance string search utility

![Version](https://img.shields.io/badge/version-0.1.0-blue)
![License](https://img.shields.io/badge/license-BSD-green)

`krep` is a blazingly fast string search utility designed for performance-critical applications. It implements multiple optimized search algorithms and leverages modern hardware capabilities to deliver maximum throughput.

## Features

- **Multiple optimized search algorithms**
  - Boyer-Moore-Horspool algorithm for efficient pattern matching
  - SIMD acceleration on compatible hardware (SSE4.2, AVX2)
  
- **Maximum performance**
  - Memory-mapped file I/O for optimal throughput
  - Multi-threaded parallel search for large files
  - Automatic algorithm selection based on hardware capabilities

- **Flexible search options**
  - Case-sensitive and case-insensitive matching
  - Direct string search in addition to file search
  - Match counting mode

## Installation

### From Source

```bash
git clone https://github.com/davidesantangelo/krep.git
cd krep
make
sudo make install
```

### Prerequisites

- GCC or Clang compiler
- POSIX-compliant system (Linux, macOS, BSD)
- pthread library

## Usage

```bash
krep [OPTIONS] PATTERN [FILE]
```

### Examples

Search for "error" in a log file:
```bash
krep "error" system.log
```

Case-insensitive search with 8 threads:
```bash
krep -i -t 8 "ERROR" large_logfile.log
```

Count occurrences without displaying matching lines:
```bash
krep -c "TODO" *.c
```

Search within a string instead of a file:
```bash
krep -s "Hello" "Hello world"
```

## Command Line Options

- `-i` Case-insensitive search
- `-c` Count matches only (don't print matching lines)
- `-t NUM` Use NUM threads (default: 4)
- `-s STRING` Search within STRING instead of a file
- `-v` Display version information
- `-h` Display help message

## Performance

`krep` is designed with performance as a primary goal:

- **Memory-mapped I/O**: Avoids costly read() system calls
- **Optimized algorithms**: Uses Boyer-Moore-Horspool algorithm by default
- **SIMD acceleration**: Utilizes SSE4.2 or AVX2 when available
- **Multi-threading**: Processes large files in parallel chunks
- **Minimal allocations**: Reduces memory overhead and fragmentation
- 
## Benchmarks

Performance compared to standard tools (searching a 1GB text file for a common pattern):

| Tool | Time (seconds) | Speed (MB/s) |
|------|----------------|--------------|
| krep | 0.78 | 1,282 |
| grep | 2.95 | 339 |
| ripgrep | 1.48 | 676 |

*Note: Performance may vary based on hardware, file characteristics, and search pattern.*

## How It Works

`krep` uses several strategies to achieve high performance:

1. **Algorithm selection**: Automatically chooses between Boyer-Moore-Horspool and SIMD-accelerated search based on hardware capabilities and pattern characteristics

2. **Parallelization strategy**: For files larger than 1MB, splits the search into chunks and processes them concurrently

3. **Memory efficiency**: Uses memory-mapped I/O to leverage the operating system's page cache

## The Story Behind the Name

The name "krep" has an interesting origin. It is inspired by the Icelandic word "kreppan," which means "to grasp quickly" or "to catch firmly." I came across this word while researching efficient techniques for pattern recognition.

Just as skilled fishers identify patterns in the water to locate fish quickly, I designed "krep" to find patterns in text with maximum efficiency. The name is also short and easy to remember—perfect for a command-line utility that users might type hundreds of times per day.
## Author

- **Davide Santangelo** - [GitHub](https://github.com/davidesantangelo)

## License

This project is licensed under the BSD-2 License - see the LICENSE file for details.

Copyright © 2025 Davide Santangelo
