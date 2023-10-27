# CMakeList.txt : convert の CMake プロジェクト。ソースを含めて、次を定義します:
# プロジェクト専用ロジックはこちらです。
#
cmake_minimum_required (VERSION 3.8)

project ("main")

# デフォルトは Release ビルド
if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING
	"Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
	FORCE)
endif()

# C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)


# ソースをこのプロジェクトの実行可能ファイルに追加します。
add_executable (mmltosmf
	"./mmltosmf.cpp"
	"./sequencer/MmlCompiler.cpp"
	"./sequencer/Smf.cpp"
)

# ライブラリ
TARGET_LINK_LIBRARIES(mmltosmf stdc++fs)
TARGET_LINK_LIBRARIES(mmltosmf boost_program_options)
TARGET_LINK_LIBRARIES(mmltosmf boost_regex)


# TODO: テストを追加し、必要な場合は、ターゲットをインストールします。


add_executable (smftomml
	"./smftomml.cpp"
	"./sequencer/MmlCompiler.cpp"
	"./sequencer/Smf.cpp"
)

# ライブラリ
TARGET_LINK_LIBRARIES(smftomml stdc++fs)
TARGET_LINK_LIBRARIES(smftomml boost_program_options)
TARGET_LINK_LIBRARIES(smftomml boost_regex)
