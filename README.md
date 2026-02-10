# React Flow (MoYanxi Data Flow)

A lightweight, C++23 module-interface-only data graph publisher-receiver framework. Designed for light task/change auto-propagation with minimal overhead.

![sample.drawio.svg](properties/sample.drawio.svg)

## Features

*   **Modern C++**: Built entirely with C++23 modules (`.ixx`).
*   **Flexible Propagation**: Supports Eager (Push), Lazy (Pull), and Pulse (Clock-driven) modes.
*   **Async Support**: Built-in support for asynchronous tasks with progress tracking without blocking the main thread.
*   **Type Safety**: Rigorous compile-time type checking for node connections to prevent invalid data flows.
*   **Low Overhead**: Optimized for performance with move semantics and minimal copying. The overhead on three nodes is about **50ns**.
*   **RAII Management**: Automatic disconnection and cleanup of nodes upon destruction.

## Dependencies

*   **Compiler**: A C++23 compliant compiler (e.g., Clang, MSVC).
*   **Build System**: [xmake](https://xmake.io).
*   **Libraries**:
    *   [MoYanxi Utility](https://github.com/Yuria-Shikibe/mo_yanxi_utility.git) (automatically fetched by xmake).

## Build & Run

Ensure you have `xmake` installed.

### Build Library

```bash
xmake
```

### Run Examples

```bash
xmake run mo_yanxi.react_flow.example
```

### Run Tests

To run tests (requires `gtest`):

```bash
xmake f --add_test=y
xmake
xmake run mo_yanxi.react_flow.test
```

## Usage

### Core Concepts

*   **Manager (`manager`)**: The central object that manages the lifecycle of all nodes and the execution of the graph.
*   **Node (`node`)**: The basic building block of the graph.
    *   **Provider**: The source of data.
    *   **Transformer (Modifier)**: Transforms input data into output data.
    *   **Listener (Terminal)**: The end point of data flow; consumes data.
*   **Connection**: Nodes are connected to form a directed acyclic graph (DAG). Data flows from Providers through Transformers to Listeners.

### Basic Example

Here is a simple example demonstrating how to set up a data flow graph:

```cpp
import mo_yanxi.react_flow;
import std;

using namespace mo_yanxi::react_flow;

int main() {
    manager mgr;

    // 1. Create Nodes
    // Provider: Source of integer data
    auto& input = mgr.add_node<provider_cached<int>>();

    // Transformer: Multiplies input by 2
    auto& doubler = mgr.add_node(make_transformer([](int val) {
        return val * 2;
    }));

    // Listener: Prints the result
    auto& printer = mgr.add_node(make_listener([](int val) {
        std::println("Result: {}", val);
    }));

    // 2. Connect Nodes
    // input -> doubler -> printer
    connect_chain({&input, &doubler, &printer});

    // 3. Propagate Data
    input.update_value(10); // Output: Result: 20
    input.update_value(5);  // Output: Result: 10

    return 0;
}
```

### Node Types

#### Provider
Where the data flow starts; the source of any input data.
*   `provider_cached<T>`: Stores the value and provides it to successors.

#### Modifier (Transformer)
Accepts one or multiple inputs and transforms them into the input of successors.
![modifier.drawio.svg](properties/modifier.drawio.svg)

You can create transformers using `make_transformer`.

```cpp
auto& node = mgr.add_node(make_transformer([](int input) -> std::string {
    return std::to_string(input);
}));
```

#### Terminal (Listener)
Where the data flow ends.
*   `listener`: Executes a function when data is received.

### Async Nodes
The library supports asynchronous operations. Async nodes run on a separate thread managed by the `manager`.

```cpp
auto& async_node = mgr.add_node(make_async_transformer([](const async_context& ctx, std::string input) -> std::string {
    // Perform long-running task
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return input + " processed";
}));
```

Async nodes can report progress via `ctx.task->set_progress()`.

### Propagation Modes
*   **Eager (Push)**: Updates are pushed immediately to successors.
*   **Lazy (Pull)**: Updates are marked, but data is only computed/fetched when a terminal node requests it.
*   **Pulse**: Updates are synchronized with a "pulse" signal (e.g., a clock tick).

## Benchmark

> Input: Small String -> Transform: (`std::from_chars`->`std::optional<int>`) -> Output

Node Graph Implementation: provider -> transformer -> listener

| Benchmark  | Input Size | Time (ns) | CPU (ns) | Iterations |
|------------|------------|-----------|----------|------------|
| **Node**   | 1024       | 195       | 193      | 3733333    |
| **Node**   | 4096       | 177       | 180      | 4072727    |
| **Node**   | 32768      | 172       | 173      | 4072727    |
| **Node**   | 65536      | 179       | 180      | 4072727    |
| **Native** | 1024       | 131       | 131      | 5600000    |
| **Native** | 4096       | 128       | 128      | 5600000    |
| **Native** | 32768      | 122       | 122      | 4977778    |
| **Native** | 65536      | 130       | 131      | 5600000    |

Overhead on three nodes is about **50ns** (29% on this _stoi_ task).

## Limitations

*   **Shared Library**: Do not build as a shared library (due to type info implementation specifics).
*   **Task Graph Scheduling**: Advanced scheduling (beyond basic async) is not yet fully supported.
*   **Cycle Detection**: Basic cycle detection is implemented, but complex scenarios might need care.

## License

MIT License. See [LICENSE](LICENSE) for details.
