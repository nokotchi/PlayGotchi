cmake_minimum_required(VERSION 3.14)
set(CMAKE_C_STANDARD 11)

set(ENVSDK $ENV{PLAYDATE_SDK_PATH})

if (NOT ${ENVSDK} STREQUAL "")
	# Convert path from Windows
	file(TO_CMAKE_PATH ${ENVSDK} SDK)
else()
	execute_process(
			COMMAND bash -c "egrep '^\\s*SDKRoot' $HOME/.Playdate/config"
			COMMAND head -n 1
			COMMAND cut -c9-
			OUTPUT_VARIABLE SDK
			OUTPUT_STRIP_TRAILING_WHITESPACE
	)
endif()

if (NOT EXISTS ${SDK})
	message(FATAL_ERROR "SDK Path not found; set ENV value PLAYDATE_SDK_PATH")
	return()
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release")
set(CMAKE_XCODE_GENERATE_SCHEME TRUE)

# Game Name Customization
set(PLAYDATE_GAME_NAME playgotchi)
set(PLAYDATE_GAME_DEVICE playgotchi_DEVICE)

project(${PLAYDATE_GAME_NAME} C ASM)

file(GLOB_RECURSE TAMALIB_SRC_FILES src/tamalib/*.c)

if (TOOLCHAIN STREQUAL "armgcc")
	add_executable(${PLAYDATE_GAME_DEVICE} src/main.c ${TAMALIB_SRC_FILES})
	target_include_directories(${PLAYDATE_GAME_DEVICE} PUBLIC src/tamalib)
else()
	add_library(tamalib ${TAMALIB_SRC_FILES})
	add_library(${PLAYDATE_GAME_NAME} SHARED src/main.c)
	target_include_directories(${PLAYDATE_GAME_NAME} PUBLIC src/tamalib)
	target_link_libraries(${PLAYDATE_GAME_NAME} tamalib)
endif()

include(${SDK}/C_API/buildsupport/playdate_game.cmake)

