include(FetchContent)

find_package(nlohmann_json 3.12 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG 55f93686c01528224f448c19128836e7df245f72
    GIT_SHALLOW FALSE
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

function(statewright_resolve_catch2)
  find_package(Catch2 3.8 QUIET)
  if(NOT Catch2_FOUND)
    FetchContent_Declare(
      Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG 2b60af89e23d28eefc081bc930831ee9d45ea58b
      GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(Catch2)
  endif()
endfunction()

