# Container detection and management for Koncrepe
# Provides support for Toolbox, Distrobox, and other container types

add_library(koncrepecontainers STATIC)

set_target_properties(koncrepecontainers PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(koncrepecontainers PRIVATE
    ContainerList.cpp
    ContainerRegistry.cpp
    DistroboxListParser.cpp
    IContainerDetector.cpp
    ToolboxListParser.cpp
    ToolboxDetector.cpp
    DistroboxDetector.cpp
)

if(HAVE_KAPSULE)
    target_sources(koncrepecontainers PRIVATE KapsuleDetector.cpp)
endif()

# Some of the koncrepecontainers API needs to be exported once linked into koncrepeprivate,
# for that the export header of koncrepeprivate is reused,
# which needs here setting the export flag or static build flag manually
if(BUILD_SHARED_LIBS)
    target_compile_definitions(koncrepecontainers PRIVATE koncrepeprivate_EXPORTS)
else()
    target_compile_definitions(koncrepecontainers PRIVATE KONCREPEPRIVATE_STATIC_DEFINE)
endif()

target_link_libraries(koncrepecontainers
    PUBLIC
        Qt::Core
    PRIVATE
        koncrepelogging
        KF6::I18n
        KF6::CoreAddons  # For KSandbox
        Qt::Widgets
)

if(HAVE_KAPSULE)
    target_link_libraries(koncrepecontainers PRIVATE Kapsule::KapsuleQt)
endif()

target_include_directories(koncrepecontainers
    PRIVATE
    ${CMAKE_BINARY_DIR}/src  # For config-koncrepe.h
)
