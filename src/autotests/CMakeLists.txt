include(ECMAddTests)

set(KONCREPE_TEST_LIBS)

if (CMAKE_SYSTEM_NAME MATCHES "OpenBSD")
    set(KONCREPE_TEST_LIBS ${KONCREPE_TEST_LIBS} kvm)
endif()

ecm_add_test(
    KeyboardTranslatorTest.cpp
    LINK_LIBRARIES keyboardtranslator Qt::Test ${KONCREPE_TEST_LIBS}
)

if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin" AND NOT WIN32)
    ecm_add_test(
        PartTest.cpp
        LINK_LIBRARIES koncrepeprivate KF6::Parts KF6::Pty Qt::Test ${KONCREPE_TEST_LIBS}
    )
endif()

ecm_add_test(
    BookMarkTest.cpp
    LINK_LIBRARIES KF6::Bookmarks Qt::Test
)

ecm_add_tests(
    CharacterColorTest.cpp
    CharacterTest.cpp
    CharacterWidthTest.cpp
    LINK_LIBRARIES koncrepecharacters Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_tests(
    HistoryTest.cpp
    HotSpotFilterTest.cpp
    ScreenTest.cpp
    SessionTest.cpp
    ShellCommandTest.cpp
    TerminalTest.cpp
    LINK_LIBRARIES koncrepeprivate Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    ProcessInfoTest.cpp
    LINK_LIBRARIES koncrepeprivate KF6::CoreAddons Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    ProfileTest.cpp
    LINK_LIBRARIES koncrepeprivate KF6::ConfigGui KF6::I18n Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    TerminalCharacterDecoderTest.cpp
    LINK_LIBRARIES koncrepedecoders koncrepecharacters koncrepecolorscheme Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    Vt102EmulationTest.cpp
    LINK_LIBRARIES koncrepeprivate Qt::Multimedia Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    ContainerDetectorParsingTest.cpp
    LINK_LIBRARIES koncrepecontainers Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    ContainerRegistryTest.cpp
    LINK_LIBRARIES koncrepecontainers Qt::Test ${KONCREPE_TEST_LIBS}
)
target_include_directories(ContainerRegistryTest PRIVATE ${CMAKE_BINARY_DIR}/src)

if(NOT WIN32)
    ecm_add_test(
        PtyTest.cpp
        LINK_LIBRARIES koncrepeprivate KF6::Pty Qt::Test ${KONCREPE_TEST_LIBS}
    )
endif()

ecm_add_test(
    TerminalInterfaceTest.cpp
    LINK_LIBRARIES koncrepeprivate KF6::Parts Qt::Test ${KONCREPE_TEST_LIBS}
)

ecm_add_test(
    ViewManagerTest.cpp
    LINK_LIBRARIES koncrepeapp koncrepeprivate Qt::Test ${KONCREPE_TEST_LIBS}
)
