
cmake_minimum_required (VERSION 3.8)

# デフォルトは Release ビルド
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
	"Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
	FORCE)
endif()


project ("rlibmml")

set(CMAKE_CXX_STANDARD 17)

# ソースをこのプロジェクトの実行可能ファイルに追加します。
add_executable ( ${CMAKE_PROJECT_NAME}
	"./main.cpp"
	"../sequencer/MidiEvent.h"
	"../sequencer/MmlCompiler.cpp"
	"../sequencer/MmlCompiler.h"
	"../sequencer/MmlToSmf.h"
	"../sequencer/Smf.cpp"
	"../sequencer/Smf.h"
	"../sequencer/SmfToMml.h"
	"../sequencer/TempoList.h"
	"../stringformat/StringFormat.h"
)

set(CMAKE_EXECUTABLE_SUFFIX ".js")
#set(CMAKE_CXX_FLAGS "--bind")
#set(CMAKE_CXX_FLAGS "-std=c++17 -O0 -g3 --bind -s NO_DISABLE_EXCEPTION_CATCHING -s WASM=1 -s SINGLE_FILE=1 -s MODULARIZE=1 -s ENVIRONMENT=web")

target_compile_options( ${CMAKE_PROJECT_NAME} PRIVATE
	--bind
	-fexceptions
)

target_link_options( ${CMAKE_PROJECT_NAME} PRIVATE
	-lembind
	-sNO_DISABLE_EXCEPTION_CATCHING
	-sWASM=1
	-sSINGLE_FILE=1
	-sMODULARIZE=1
	-sENVIRONMENT='web'
	-sALLOW_MEMORY_GROWTH
)
