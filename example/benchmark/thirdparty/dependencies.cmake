# ---------------------------------------------------------------------------
# Local dependency prefix
# ---------------------------------------------------------------------------

Include(ExternalProject)

set(ANYBLOB_DEPS_ROOT "${CMAKE_SOURCE_DIR}/../../.deps" CACHE PATH "Dependency source, build, and install root")
set(ANYBLOB_DEPS_SOURCE_DIR "${ANYBLOB_DEPS_ROOT}/src")
set(ANYBLOB_DEPS_BUILD_DIR "${ANYBLOB_DEPS_ROOT}/build")
set(ANYBLOB_DEPS_INSTALL_DIR "${ANYBLOB_DEPS_ROOT}/prefix")
set(ANYBLOB_DEPS_UPDATE_DISCONNECTED OFF CACHE BOOL "Do not update downloaded dependencies")
set(ANYBLOB_JOBS 16 CACHE STRING "Parallel jobs for downloaded dependencies")
set(ANYBLOB_GIT_TAG master CACHE STRING "AnyBlob git tag or commit")
set(AWS_SDK_GIT_TAG 1.11.31 CACHE STRING "AWS SDK for C++ git tag or commit")

file(MAKE_DIRECTORY
    "${ANYBLOB_DEPS_SOURCE_DIR}"
    "${ANYBLOB_DEPS_BUILD_DIR}"
    "${ANYBLOB_DEPS_INSTALL_DIR}")

ExternalProject_Add(liburing
    GIT_REPOSITORY https://github.com/axboe/liburing.git
    GIT_TAG liburing-2.8
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/liburing"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/liburing"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/liburing"
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${ANYBLOB_DEPS_INSTALL_DIR}
    BUILD_COMMAND make -C <SOURCE_DIR> -j${ANYBLOB_JOBS}
    INSTALL_COMMAND make -C <SOURCE_DIR> install
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

ExternalProject_Add(jemalloc
    GIT_REPOSITORY https://github.com/jemalloc/jemalloc.git
    GIT_TAG 5.3.0
    SOURCE_DIR "${ANYBLOB_DEPS_SOURCE_DIR}/jemalloc"
    BINARY_DIR "${ANYBLOB_DEPS_BUILD_DIR}/jemalloc"
    STAMP_DIR "${ANYBLOB_DEPS_BUILD_DIR}/stamps/jemalloc"
    CONFIGURE_COMMAND <SOURCE_DIR>/autogen.sh --prefix=${ANYBLOB_DEPS_INSTALL_DIR}
    BUILD_COMMAND make -C <SOURCE_DIR> -j${ANYBLOB_JOBS}
    INSTALL_COMMAND make -C <SOURCE_DIR> install
    UPDATE_DISCONNECTED ${ANYBLOB_DEPS_UPDATE_DISCONNECTED})

add_custom_target(anyblob-dependencies DEPENDS liburing jemalloc)