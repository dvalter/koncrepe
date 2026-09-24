add_library(koncrepedecoders OBJECT)

set_target_properties(koncrepedecoders PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(koncrepedecoders PRIVATE
    PlainTextDecoder.cpp
    HTMLDecoder.cpp
)

target_include_directories(koncrepedecoders
    PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# TODO: remove the koncrepecolorscheme dependencies
target_link_libraries(koncrepedecoders PUBLIC koncrepecharacters)
