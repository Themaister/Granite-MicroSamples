# Granite-MicroSamples

This is a sample repository for [my blog post series](http://themaister.net/blog/2019/04/14/a-tour-of-granites-vulkan-backend-part-1/)
about [Granite's](https://github.com/Themaister/Granite) Vulkan backend design.

## Checkout submodules

There is a script which conveniently only checks out the required submodules:

```
./checkout_submodules.sh
```

## Build

Standard CMake.

```
mkdir build
cd build
cmake ..
cmake --build .
./03-frame-contexts
```
