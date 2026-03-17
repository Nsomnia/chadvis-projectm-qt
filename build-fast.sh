cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER_LAUNCHER=sccache -DCMAKE_CXX_FLAGS="-O0 -g" && ninja -C build -j$(nproc)
