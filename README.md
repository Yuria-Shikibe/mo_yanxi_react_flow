# Brief
* This library is a light and *module interface only* data graph publisher-receiver framework. Used for light task/change auto propagation.
* Like a extremely light version of _taskflow_, but makes trivial operations have little overhead. Which also means this library is NOT designed for high concurrency and parallel required condition.

![sample.drawio.svg](properties/sample.drawio.svg)

### [Check the similar code example here](examples/example.cpp)

## Supports/Feature
* Supports eager(push), lazy(fetch) and pulse(clock) mode.
* Sync task / SPSC async task
* RAII and reference count based node manage
* Type Check
* Try its best to move non trivial data 

## Not Supported
* Task Graph schedule (maybe supported in the future)
* Auto overlap-clip on data fetch (which means some nodes may being unnecessarily fetched multiple time during one fetch)
* Allocators for nodes and other object that has heap allocation (TODO support when std::indirect and std::polymorphic is available?)

# Node Specify
//TBD

## Provider
Where the data flow starts; the source of any input data.

## Terminal
Where the data flow ends.

## Modifier
Accept one or multiple input and transform it into the input of successors.
![modifier.drawio.svg](properties/modifier.drawio.svg)

The significance of having this set of conversions within the node is to facilitate caching and view passing, ensuring that subsequent operations involve zero redundant copies.

**Note:** Currently, safe built-in conversions are only performed for types satisfying the `std::ranges::view` concept. Using types such as `std::reference_wrapper` as transform result may lead to **dangling references**. Future updates will aim to ensure this is prohibited at compile-time.

# Async Specification
* Except for async node task relevant operations and async push task to manager, everything should happens on manager thread.

# Mode Specification
//TBD

# State Specification
//TBD

# Async Specification
//TBD

# Note
* NEVER make this library as a shared library (issue from type info implementation, may support this by offering an option that using std::type_info and give up some performance.)
* Node without any **cache** at local or context is not recommended to be set to pulse mode.
* If `make_xxx` function is available, use it instead of call constructors.