# K(r)ep - A high-performance string search utility

![Version](https://img.shields.io/badge/version-0.4.2-blue)
![License](https://img.shields.io/badge/license-BSD-green)

`Krep (krep)` is a blazingly fast string search utility designed for performance-critical applications. It implements multiple optimized search algorithms and leverages modern hardware capabilities to deliver maximum throughput.

![diagram](https://github.com/user-attachments/assets/6ea2ab4c-ef0e-4423-8481-2dcee1aba9e3)

## Blog Post

Building a High-Performance String Search Utility

https://dev.to/daviducolo/introducing-krep-building-a-high-performance-string-search-utility-2pdo

## Features

- **Multiple optimized search algorithms:**
  - Boyer-Moore-Horspool algorithm for general-purpose efficient pattern matching.
  - Knuth-Morris-Pratt (KMP) algorithm optimized for very short patterns.
  - Rabin-Karp algorithm suitable for longer patterns.
- **Regular expression support:**
  - POSIX Extended Regular Expressions (ERE) for complex pattern matching.
  - Case-sensitive and case-insensitive regex matching.
- **SIMD acceleration:**
  - ARM NEON implementation with specialized optimization for short patterns
  - SSE4.2 implementation (currently falls back to Boyer-Moore)
  - AVX2 support (placeholder with fallback)
- **Maximum performance:**
  - Memory alignment optimizations for SIMD operations
  - Cache-aware prefetching for reduced CPU stalls
  - Memory-mapped file I/O (`mmap`) with optimized flags for potentially better throughput on large sequential reads.
  - Single-threaded mode for accurate position/line tracking (Multi-threaded support deprecated).
  - Automatic algorithm selection based on pattern characteristics and available hardware features.
- **Enhanced output options:**
  - Case-sensitive and case-insensitive matching (`-i`).
  - Direct string search (`-s`) in addition to file search.
  - Pattern specification via `-e PATTERN`.
  - Recursive directory search (`-r`) with automatic skipping of binary files and common non-code directories.
  - Match counting mode (`-c`).
  - Output only matched parts (`-o`), similar to grep -o.
  - Color highlighting of matched text with configurable behavior.
  - Optional detailed search summary (`-d`).
  - Reports unique matching lines when printing lines (default mode).

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
krep [OPTIONS] PATTERN [FILE | DIRECTORY]
```
or
```bash
krep [OPTIONS] -e PATTERN [FILE | DIRECTORY]
```
or
```bash
krep [OPTIONS] -s PATTERN STRING_TO_SEARCH
```

### Examples

Search for "error" in a log file:
```bash
krep "error" system.log
```

Case-insensitive search:
```bash
krep -i "ERROR" large_logfile.log
```

Search using a regular expression:
```bash
krep -E "^[Ee]rror: .*" system.log
```

Count occurrences without displaying matching lines:
```bash
krep -c "TODO" source.c
```

Print only matched parts:
```bash
krep -o -E '[0-9]+' data.log | sort | uniq -c
```

Search within a string instead of a file:
```bash
krep -s "Hello" "Hello world"
```

Display detailed search summary:
```bash
krep -d "function" source.c
```

Search recursively in a directory:
```bash
krep -r "TODO" ./project
```

Specify pattern that starts with dash:
```bash
krep -e "-pattern" file.txt
```

Case-insensitive recursive search:
```bash
krep -ir "error" .
```

## Command Line Options

- `-i` Case-insensitive search
- `-c` Count matching lines only (don't print matching lines)
- `-o` Only matching. Print only the matched parts of lines (like grep -o)
- `-d` Display detailed search summary (ignored with -c or -o)
- `-e PATTERN` Specify pattern. Useful for patterns starting with '-' or multiple patterns.
- `-E` Interpret PATTERN as a POSIX Extended Regular Expression (ERE)
- `-r` Recursively search directories. Skips binary files and common non-code directories
- `-t NUM` Use NUM threads (currently ignored, single-threaded for accuracy)
- `-s` Search in STRING_TO_SEARCH instead of a FILE or DIRECTORY
- `--color[=WHEN]` Control color output ('always', 'never', 'auto'). Default: 'auto'
- `-v` Display version information and exit
- `-h` Display help message and exit

## Regular Expressions

`krep` supports POSIX Extended Regular Expressions (ERE) with the `-E` flag, allowing you to perform complex pattern matching beyond simple string searches.

### Basic Regex Syntax

- `.` - Matches any single character
- `[]` - Character class, matches any character inside the brackets
- `[^]` - Negated character class, matches any character NOT inside the brackets
- `^` - Matches the start of a line
- `$` - Matches the end of a line
- `*` - Matches 0 or more occurrences of the previous character/group
- `+` - Matches 1 or more occurrences of the previous character/group
- `?` - Matches 0 or 1 occurrence of the previous character/group
- `{n}` - Matches exactly n occurrences of the previous character/group
- `{n,}` - Matches n or more occurrences of the previous character/group
- `{n,m}` - Matches between n and m occurrences of the previous character/group
- `|` - Alternation, matches either the pattern before or after it
- `()` - Grouping, groups patterns together for applying operators

### Example Patterns

Find all error messages (case-insensitive):
```bash
krep -E -i "error:.*" log.txt
```

Match IP addresses:
```bash
krep -E "[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}" network.log
```

Find all email addresses:
```bash
krep -E "[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}" contacts.txt
```

Match lines that begin with "Date:":
```bash
krep -E "^Date:" message.txt
```

Find words that are exactly 5 characters long:
```bash
krep -E "(^| )[a-zA-Z]{5}( |$)" text.txt
```

Count occurrences of words starting with a vowel:
```bash
krep -E -c "(^| )[aeiouAEIOU][a-zA-Z]*( |$)" document.txt
```

Match both "color" and "colour" spellings:
```bash
krep -E "colou?r" document.txt
```

### Performance Considerations

- Regular expression searches may be slower than literal string searches, especially for complex patterns
- The regex engine uses POSIX ERE standards which do not support some features found in other regex flavors (like Perl or PCRE)
- Overly complex regex patterns with excessive backtracking may impact performance on very large files

### Limitations

- Lookbehind and lookahead assertions are not supported (POSIX ERE limitation)
- Backreferences are not supported in the current implementation
- Non-greedy (lazy) matching is not available in POSIX ERE

## Performance

`krep` is designed with performance as a primary goal:

- **Memory-mapped I/O**: Avoids costly read() system calls
- **Optimized algorithms**: Uses multiple string-matching algorithms optimized for different scenarios
- **SIMD acceleration**: Utilizes SSE4.2, AVX2, or ARM Neon when available
- **Minimal allocations**: Reduces memory overhead and fragmentation
- **Efficient line tracking**: Optimized for reporting unique matching lines

## Benchmarks

Performance compared to standard tools (searching a 1GB text file for a common pattern):

| Tool | Time (seconds) | Speed (MB/s) |
|------|----------------|--------------|
| krep | 0.78 | 1,282 |
| grep | 2.95 | 339 |

*Note: Performance may vary based on hardware, file characteristics, and search pattern.*

## How It Works

`krep` uses several strategies to achieve high performance:

1. **Intelligent algorithm selection**: Automatically chooses the optimal algorithm based on pattern characteristics:
   - KMP for very short patterns (< 3 characters)
   - ARM NEON for patterns up to 16 characters (on ARM processors)
   - SIMD/AVX2 for medium-length patterns (when hardware supports it)
   - Boyer-Moore for medium-length patterns (when SIMD is unavailable)
   - Rabin-Karp for longer patterns (> 32 characters)

2. **Specialized optimizations**: 
   - Ultra-fast path for patterns of 4 bytes or less using ARM NEON
   - Branch prediction hints for modern CPU pipeline optimization
   - Memory alignment for optimal SIMD performance
   - Strategic prefetching to minimize cache misses

3. **Single-threaded efficiency**: Optimized for accurate line tracking and position reporting, with focused algorithms that maximize single-thread performance.

4. **Memory efficiency**: Uses memory-mapped I/O (`mmap` with `PROT_READ`, `MAP_PRIVATE` and `MADV_SEQUENTIAL`) to leverage the operating system's page cache efficiently for sequential reads.

5. **Hardware acceleration**: Detects availability of SSE4.2, AVX2 and ARM Neon instructions at compile time (though full SIMD implementations are currently placeholders/fallbacks).

## Testing

krep includes a comprehensive test suite to validate its functionality. To run the tests:

```bash
# From the project root directory
make test
```

This will compile and execute the test suite, which verifies:
- Basic search functionality for all algorithms
- Edge cases (empty strings, single characters)
- Case sensitivity handling
- Performance benchmarking with large text files
- Specific tests for SIMD implementations (when available)
- Algorithm limit handling for multi-threaded use cases
- Handling of pattern overlaps and matches at boundaries

### Example test output:

```
Running krep tests...

=== Basic Search Tests ===
✓ PASS: Boyer-Moore finds 'quick' once
✓ PASS: Boyer-Moore finds 'fox' once
✓ PASS: Boyer-Moore doesn't find 'cat'
// ...more test results...

=== Test Summary ===
Tests passed: 23
Tests failed: 0
Total tests: 23
```

## The Story Behind the Name

The name "krep" has an interesting origin. It is inspired by the Icelandic word "kreppan," which means "to grasp quickly" or "to catch firmly." I came across this word while researching efficient techniques for pattern recognition.

Just as skilled fishers identify patterns in the water to locate fish quickly, I designed "krep" to find patterns in text with maximum efficiency. The name is also short and easy to remember—perfect for a command-line utility that users might type hundreds of times per day.

## Author

- **Davide Santangelo** - [GitHub](https://github.com/davidesantangelo)

## License

This project is licensed under the BSD-2 License - see the LICENSE file for details.

Copyright © 2025 Davide Santangelo
