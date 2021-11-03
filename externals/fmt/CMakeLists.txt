set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_SHALLOW 1
)

FetchContent_MakeAvailable(fmt)
