# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.10

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/jovanovicl/DPOR/src

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jovanovicl/DPOR/src/misc

# Include any dependencies generated for this target.
include CMakeFiles/mcmini.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/mcmini.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/mcmini.dir/flags.make

CMakeFiles/mcmini.dir/launch.c.o: CMakeFiles/mcmini.dir/flags.make
CMakeFiles/mcmini.dir/launch.c.o: ../launch.c
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jovanovicl/DPOR/src/misc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building C object CMakeFiles/mcmini.dir/launch.c.o"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -o CMakeFiles/mcmini.dir/launch.c.o   -c /home/jovanovicl/DPOR/src/launch.c

CMakeFiles/mcmini.dir/launch.c.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing C source to CMakeFiles/mcmini.dir/launch.c.i"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -E /home/jovanovicl/DPOR/src/launch.c > CMakeFiles/mcmini.dir/launch.c.i

CMakeFiles/mcmini.dir/launch.c.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling C source to assembly CMakeFiles/mcmini.dir/launch.c.s"
	/usr/bin/cc $(C_DEFINES) $(C_INCLUDES) $(C_FLAGS) -S /home/jovanovicl/DPOR/src/launch.c -o CMakeFiles/mcmini.dir/launch.c.s

CMakeFiles/mcmini.dir/launch.c.o.requires:

.PHONY : CMakeFiles/mcmini.dir/launch.c.o.requires

CMakeFiles/mcmini.dir/launch.c.o.provides: CMakeFiles/mcmini.dir/launch.c.o.requires
	$(MAKE) -f CMakeFiles/mcmini.dir/build.make CMakeFiles/mcmini.dir/launch.c.o.provides.build
.PHONY : CMakeFiles/mcmini.dir/launch.c.o.provides

CMakeFiles/mcmini.dir/launch.c.o.provides.build: CMakeFiles/mcmini.dir/launch.c.o


# Object files for target mcmini
mcmini_OBJECTS = \
"CMakeFiles/mcmini.dir/launch.c.o"

# External object files for target mcmini
mcmini_EXTERNAL_OBJECTS =

mcmini: CMakeFiles/mcmini.dir/launch.c.o
mcmini: CMakeFiles/mcmini.dir/build.make
mcmini: CMakeFiles/mcmini.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/jovanovicl/DPOR/src/misc/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking C executable mcmini"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/mcmini.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/mcmini.dir/build: mcmini

.PHONY : CMakeFiles/mcmini.dir/build

CMakeFiles/mcmini.dir/requires: CMakeFiles/mcmini.dir/launch.c.o.requires

.PHONY : CMakeFiles/mcmini.dir/requires

CMakeFiles/mcmini.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/mcmini.dir/cmake_clean.cmake
.PHONY : CMakeFiles/mcmini.dir/clean

CMakeFiles/mcmini.dir/depend:
	cd /home/jovanovicl/DPOR/src/misc && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jovanovicl/DPOR/src /home/jovanovicl/DPOR/src /home/jovanovicl/DPOR/src/misc /home/jovanovicl/DPOR/src/misc /home/jovanovicl/DPOR/src/misc/CMakeFiles/mcmini.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/mcmini.dir/depend

