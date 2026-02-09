# Brief
* This library is a light and *module interface only* data graph publisher-receiver framework. Used for light task/change auto propagation.

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


# Mode Specify
//TBD

# State Specify
//TBD

# Async Specify
//TBD

# Note
* NEVER make this library as a shared library (issue from type info implementation, may support this by offering an option that using std::type_info and give up some performance.)
* Node without **cache** should never be set to pulse mode.
* If `make_xxx` function is available, use it instead of call constructors.