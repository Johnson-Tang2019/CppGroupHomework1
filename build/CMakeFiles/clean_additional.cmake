# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\RemoteSensingQtStarter_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\RemoteSensingQtStarter_autogen.dir\\ParseCache.txt"
  "RemoteSensingQtStarter_autogen"
  )
endif()
