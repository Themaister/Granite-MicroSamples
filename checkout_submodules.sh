#!/bin/bash

git submodule update --init Granite

cd Granite
git submodule update --init third_party/volk
git submodule update --init third_party/spirv-cross
cd ..
