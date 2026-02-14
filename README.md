# React Flow
A lightweight, C++23 module-interface-only data graph publisher-receiver framework. Designed for light task/change auto-propagation with minimal overhead.

* Like a extremely light version of _taskflow_, but makes trivial operations have little overhead. Which also means this library is NOT designed for high concurrency and parallel required condition.

![sample.drawio.svg](properties/sample.drawio.svg)

### [Check code example similar to the sketch map](examples/example.cpp)

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

Overhead on three node is about **50ns** (29% on this _stoi_ task)

## Dependency:
* **C++23** supported compiler and standard library.
* [_Utility_](https://github.com/Yuria-Shikibe/mo_yanxi_utility.git), basically meta programming thing and little concurrent facility.
* [_Xmake_](https://xmake.io) is used as the build system.
* _gtest_ and _benchmark_, not auto included when install the library.

## Supports/Feature
* Supports eager(push), lazy(fetch) and pulse(clock) mode.
* Sync task / SPSC async task
* RAII and reference count based node manage
* Type Check
* Try its best to move non trivial data 
* If no pulse and async mode is used, the manager is optional.

## Not Supported
* Task Graph schedule (maybe supported in the future)
* Auto overlap-clip on data fetch (which means some nodes may being unnecessarily fetched multiple time during one fetch)
* Allocators for nodes and other object that has heap allocation (TODO support when std::indirect and std::polymorphic is available?)

# Node Specify
## Provider
Where the data flow starts; the source of any input data. use `update_value` to push an update.

## Terminal
Where the data flow ends. override `on_update` or use listener to perform operation when receiving updates.

## Modifier
Accept one or multiple input and transform it into the input of successors.
![modifier.drawio.svg](properties/modifier.drawio.svg)

The significance of having this set of conversions within the node is to facilitate caching and view passing, ensuring that subsequent operations involve zero redundant copies.

**Note:** Currently, safe built-in conversions are only performed for types satisfying the `std::ranges::view` concept. Using types such as `std::reference_wrapper` as transform result may lead to **dangling references**. Future updates will aim to ensure this is prohibited at compile-time.

# Thread Safe Specification
* Except for async node task relevant operations and async push task to manager, everything should happen on manager thread.

# Note
* NEVER make this library as a shared library (issue from type info implementation, may support this by offering an option that using std::type_info and give up some performance.)
* Node without any **cache** at local or context is not recommended to be set to pulse mode.
* If `make_xxx` function is available, use it instead of call constructors.