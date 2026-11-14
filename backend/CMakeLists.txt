cmake_minimum_required(VERSION 3.16)
project(sgis_cpp_backend LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(HTTPLIB REQUIRED IMPORTED_TARGET cpp-httplib)

add_executable(sgis-server src/main.cpp)

target_link_libraries(sgis-server PRIVATE
    PkgConfig::HTTPLIB
    SQLite::SQLite3
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Threads::Threads
    CURL::libcurl
    ${OpenCV_LIBS}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/processor/sgis-process.cpp")
    add_executable(sgis-process
        processor/sgis-process.cpp
        processor/Algorithum.cpp
    )

    target_include_directories(sgis-process PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(sgis-process PRIVATE ${OpenCV_LIBS})
endif()
