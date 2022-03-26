# AUTOGENERATED, DON'T CHANGE THIS FILE!


if (NOT USERVER_CHECK_PACKAGE_VERSIONS)
  unset(Nghttp2_FIND_VERSION)
endif()

if (TARGET Nghttp2)
  if (NOT Nghttp2_FIND_VERSION)
      set(Nghttp2_FOUND ON)
      return()
  endif()

  if (Nghttp2_VERSION)
      if (Nghttp2_FIND_VERSION VERSION_LESS_EQUAL Nghttp2_VERSION)
          set(Nghttp2_FOUND ON)
          return()
      else()
          message(FATAL_ERROR
              "Already using version ${Nghttp2_VERSION} "
              "of Nghttp2 when version ${Nghttp2_FIND_VERSION} "
              "was requested."
          )
      endif()
  endif()
endif()

set(FULL_ERROR_MESSAGE "Could not find `Nghttp2` package.\n\tDebian: sudo apt update && sudo apt install libnghttp2-dev\n\tMacOS: brew install nghttp2")


include(FindPackageHandleStandardArgs)

find_library(Nghttp2_LIBRARIES_nghttp2
  NAMES nghttp2
)
list(APPEND Nghttp2_LIBRARIES ${Nghttp2_LIBRARIES_nghttp2})

find_path(Nghttp2_INCLUDE_DIRS_nghttp2_nghttp2_h
  NAMES nghttp2/nghttp2.h
)
list(APPEND Nghttp2_INCLUDE_DIRS ${Nghttp2_INCLUDE_DIRS_nghttp2_nghttp2_h})



if (Nghttp2_VERSION)
  set(Nghttp2_VERSION ${Nghttp2_VERSION})
endif()

if (Nghttp2_FIND_VERSION AND NOT Nghttp2_VERSION)
  include(DetectVersion)

  if (UNIX AND NOT APPLE)
    deb_version(Nghttp2_VERSION libnghttp2-dev)
  endif()
  if (APPLE)
    brew_version(Nghttp2_VERSION nghttp2)
  endif()
endif()

 
find_package_handle_standard_args(
  Nghttp2
    REQUIRED_VARS
      Nghttp2_LIBRARIES
      Nghttp2_INCLUDE_DIRS
      
    FAIL_MESSAGE
      "${FULL_ERROR_MESSAGE}"
)
mark_as_advanced(
  Nghttp2_LIBRARIES
  Nghttp2_INCLUDE_DIRS
  
)

if (NOT Nghttp2_FOUND)
  if (Nghttp2_FIND_REQUIRED)
      message(FATAL_ERROR "${FULL_ERROR_MESSAGE}. Required version is at least ${Nghttp2_FIND_VERSION}")
  endif()

  return()
endif()

if (Nghttp2_FIND_VERSION)
  if (Nghttp2_VERSION VERSION_LESS Nghttp2_FIND_VERSION)
      message(STATUS
          "Version of Nghttp2 is '${Nghttp2_VERSION}'. "
          "Required version is at least '${Nghttp2_FIND_VERSION}'. "
          "Ignoring found Nghttp2."
          "Note: Set -DUSERVER_CHECK_PACKAGE_VERSIONS=0 to skip package version checks if the package is fine."
      )
      set(Nghttp2_FOUND OFF)
      return()
  endif()
endif()

 
if (NOT TARGET Nghttp2)
  add_library(Nghttp2 INTERFACE IMPORTED GLOBAL)

  target_include_directories(Nghttp2 INTERFACE ${Nghttp2_INCLUDE_DIRS})
  target_link_libraries(Nghttp2 INTERFACE ${Nghttp2_LIBRARIES})
  
  # Target Nghttp2 is created
endif()

if (Nghttp2_VERSION)
  set(Nghttp2_VERSION "${Nghttp2_VERSION}" CACHE STRING "Version of the Nghttp2")
endif()