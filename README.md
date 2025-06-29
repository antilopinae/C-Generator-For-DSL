# C Code Generator for Block Diagram Schemas

This project is a prototype of a C code generator. It takes a block diagram description from an XML file, representing a control system or an algorithm, and generates a corresponding C source file that simulates the system's behavior.

## Overview

The generator parses a domain-specific language (DSL) defined in an XML format, builds an in-memory graph representation of the block diagram, and then translates this graph into executable C code.

## How to Build and Run

### Prerequisites

-   A C++17 compliant compiler (e.g., GCC, Clang).

### Compilation

Compile the generator using the following command:

```bash
g++ -std=c++17 -O2 generator.cpp tinyxml2.cpp -o gen
```

### Usage

Run the generator by providing the input XML file. An output file name is optional and defaults to `nwocg_generated.c`.

```bash
./gen <path_to_input_schema.xml> [output_file.c]
```

**Example:**
```bash
./gen sheme.xml
```

This will generate a file named `nwocg_generated.c` in the current directory, which can then be compiled as part of a larger simulation project.
