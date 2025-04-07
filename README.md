# K(r)ep - A high-performance string search utility

![Version](https://img.shields.io/badge/version-1.0.0-blue)
![License](https://img.shields.io/badge/license-BSD-green)

`krep` is an optimized string search utility designed for maximum throughput and efficiency when processing large files and directories. It's built with performance in mind, offering multiple search algorithms and SIMD acceleration when available.

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

## Installation

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
krep [OPTIONS] -s PATTERN STRING_TO_SEARCH
krep [OPTIONS] PATTERN < FILE
cat FILE | krep [OPTIONS] PATTERN
```

### Examples

Search for a pattern in a file:
```bash
krep "error" system.log
```

Case-insensitive search:
```bash
krep -i "ERROR" large_logfile.log
```

Count occurrences:
```bash
krep -c "TODO" source.c
```

Use regular expressions:
```bash
krep -E "^[Ee]rror: .*" system.log
```

Search for literal text that contains regex characters:
```bash
krep -F "value: 100%" config.ini
```

Search recursively:
```bash
krep -r "function" ./project
```

Use with piped input:
```bash
cat krep.c | krep 'c'
```

## Command Line Options

- `-i` Case-insensitive search
- `-c` Count matching lines only
- `-o` Print only the matched parts of lines
- `-e PATTERN` Specify pattern (useful for patterns starting with '-')
- `-E` Use POSIX Extended Regular Expressions
- `-F` Interpret pattern as fixed string (not regex)
- `-r` Recursively search directories
- `-t NUM` Use NUM threads for file search
- `-s` Search in string instead of file
- `--color[=WHEN]` Control color output ('always', 'never', 'auto')
- `--no-simd` Explicitly disable SIMD acceleration
- `-v` Show version information
- `-h` Show help message

## Performance Benchmarks

Comparing performance on the same text file with identical search pattern:

| Tool | Time (seconds) | CPU Usage |
|------|----------------|-----------|
| krep | 0.106 | 328% |
| grep | 4.400 | 99% |
| ripgrep | 0.115 | 97% |

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
