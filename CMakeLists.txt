# CMakeList.txt : convert の CMake プロジェクト。ソースを含めて、次を定義します:
# プロジェクト専用ロジックはこちらです。
#
cmake_minimum_required (VERSION 3.8)

# デフォルトは Release ビルド
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
	"Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
	FORCE)
endif()

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

project("mmltosmf")

# ソースをこのプロジェクトの実行可能ファイルに追加します。
add_executable( ${CMAKE_PROJECT_NAME}
	"./mmltosmf.cpp"
	"./sequencer/MmlCompiler.cpp"
	"./sequencer/Smf.cpp"
)

# ライブラリ
TARGET_LINK_LIBRARIES( ${CMAKE_PROJECT_NAME} stdc++fs)
TARGET_LINK_LIBRARIES( ${CMAKE_PROJECT_NAME} boost_program_options)
TARGET_LINK_LIBRARIES( ${CMAKE_PROJECT_NAME} boost_regex)


# TODO: テストを追加し、必要な場合は、ターゲットをインストールします。

project("smftomml")

add_executable( ${CMAKE_PROJECT_NAME}
	"./smftomml.cpp"
	"./sequencer/Smf.cpp"
)

# ライブラリ
TARGET_LINK_LIBRARIES( ${CMAKE_PROJECT_NAME} stdc++fs)
TARGET_LINK_LIBRARIES( ${CMAKE_PROJECT_NAME} boost_program_options)
TARGET_LINK_LIBRARIES( ${CMAKE_PROJECT_NAME} boost_regex)
