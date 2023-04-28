HalVD: HAL Violation Detection
=========
Detect where applications directly access hardware via raw MMIO addresses, bypassing the Hardware Abstraction Layer (HAL). 
### Compile
```bash
export LLVM_DIR=<installation/dir/of/llvm/14>
mkdir build
cd build
cmake -DLT_LLVM_INSTALL_DIR=$LLVM_DIR ..
make
```

### Run
Run HalVD on LLVM bitcode file of an application:
```bash
$LLVM_DIR/bin/opt \
  -load-pass-plugin build/lib/libFindMMIOFunc.so \
  -load-pass-plugin build/lib/libFindHALBypass.so \
  --passes='print<hal-bypass>' --disable-output <path/to/bitcode-file.bc> \
  2> some-app.analysis
```

Run HalVD on every application in bitcode dataset:
``` bash
export RTOSExploration=/abs/path/to/folder/artifact
./run.sh
```

Development Environment
=======================
## Platform Support And Requirements
This project has been tested on **Ubuntu 20.04**. In
order to build **HalVD** you will need:
  * LLVM 14
  * C++ compiler that supports C++14
  * CMake 3.13.4 or higher

In order to run the passes, you will need:
  * **clang-14** (to generate input LLVM files)
  * [**opt**](http://llvm.org/docs/CommandGuide/opt.html) (to run the passes)


## Installing LLVM 14 on Ubuntu
On Ubuntu Bionic, you can [install modern
LLVM](https://blog.kowalczyk.info/article/k/how-to-install-latest-clang-6.0-on-ubuntu-16.04-xenial-wsl.html)
from the official [repository](http://apt.llvm.org/):

```bash
wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | sudo apt-key add -
sudo apt-add-repository "deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-14 main"
sudo apt-get update
sudo apt-get install -y llvm-14 llvm-14-dev llvm-14-tools clang-14
```
This will install all the required header files, libraries and tools in
`/usr/lib/llvm-14/`.

## Building LLVM 14 From Sources
Building from sources can be slow and tricky to debug. It is not necessary, but
might be your preferred way of obtaining LLVM 14. The following steps will work
on Linux and Mac OS X:

```bash
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git checkout release/14.x
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=host -DLLVM_ENABLE_PROJECTS=clang <llvm-project/root/dir>/llvm/
cmake --build .
```
For more details read the [official
documentation](https://llvm.org/docs/CMake.html).




License
========
The MIT License (MIT)

Copyright (c) 2023 Mingjie Shen

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
