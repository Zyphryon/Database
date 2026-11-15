## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
## Copyright (C) 2021-2026 by Agustin L. Alvarez. All rights reserved.
##
## This work is licensed under the terms of the MIT license.
##
## For a copy, see <https://opensource.org/licenses/MIT>.
## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

CMAKE_MINIMUM_REQUIRED(VERSION 3.26)

## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
## Project
## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

PROJECT(ZyDatabase VERSION 1.0.0 LANGUAGES C CXX)

INCLUDE(FetchContent)

## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
## Dependency (Engine)
## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

FetchContent_Declare(External_ZyEngine
        GIT_REPOSITORY https://github.com/Zyphryon/Engine
        GIT_TAG        main
        GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(External_ZyEngine)

## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
## Modules
## -=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

OPTION(ZY_DATABASE_SQLITE   "Build the link to SQLite, a database kept in one file"    ON)
OPTION(ZY_DATABASE_POSTGRES "Build the link to PostgreSQL, a database behind a server" ON)

ADD_SUBDIRECTORY(${CMAKE_CURRENT_SOURCE_DIR}/Module/Database.Core)

ZyAddTarget(${PROJECT_NAME} STATIC SOURCE "Source")
TARGET_LINK_LIBRARIES(${PROJECT_NAME} PUBLIC "ZyDatabase_Core")

IF (ZY_DATABASE_SQLITE)
    ADD_SUBDIRECTORY(${CMAKE_CURRENT_SOURCE_DIR}/Module/Database.SQLite)

    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PUBLIC "ZyDatabase_SQLite")
    TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PUBLIC ZY_DATABASE_SQLITE)
ENDIF (ZY_DATABASE_SQLITE)

IF (ZY_DATABASE_POSTGRES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Module/Database.Postgres/CMakeLists.txt")
    ADD_SUBDIRECTORY(${CMAKE_CURRENT_SOURCE_DIR}/Module/Database.Postgres)

    TARGET_LINK_LIBRARIES(${PROJECT_NAME} PUBLIC "ZyDatabase_Postgres")
    TARGET_COMPILE_DEFINITIONS(${PROJECT_NAME} PUBLIC ZY_DATABASE_POSTGRES)
ENDIF ()