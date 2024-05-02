ROOT=$(pwd)

cd ../3rdParty/emsdk/
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

cd $ROOT
EMSDK_PATH=$HOME/3rdParty/emsdk

PATH=$(printenv PATH):$EMSDK:$EMSCRIPTEN_PATH:$CLANG_PATH:$NODE_PATH:$PYTHON_PATH
rm -r build
make -f Makefile.Web