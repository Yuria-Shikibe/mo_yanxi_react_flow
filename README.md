# Brief
* This library is a light and *module interface only* data graph publisher-receiver framework. Used for change propagation, light task.

![sample.drawio.svg](properties/sample.drawio.svg)

## Supports/Feature
* Supports eager(push), lazy(fetch) and pulse(clock) mode.
* Sync task / SPSC async task.
* RAII and reference count based node manage
* Type Check

## Not Supported
* Task Graph schedule
* Auto overlap clip on data fetch (which means some nodes may being unnecessarily fetched multiple time during one fetch)
* Allocators for nodes and other object that has heap allocation (TODO support when std::indirect and std::polymorphic is available?)


# Note
* node without **cache** should never be set to pulse mode.
* if `make_xxx` function is available, use it instead of call constructors.