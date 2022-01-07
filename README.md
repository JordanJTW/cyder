# Build

Ensure submodule is sync'd:
```
$ git submodule update --init --recursive
```

Use Make to build: 
```
mkdir build && cd build
cmake ..
make
```

Use Ninja to build:
```
mkdir -p build/out
cmake -GNinja -Bbuild/out

# Only this command is needed to build from now on
ninja -C build/out
```
