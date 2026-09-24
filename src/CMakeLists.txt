add_definitions(-DTRANSLATION_DOMAIN=\"koncrepe\")

### Handle DragonFlyBSD here instead of using __DragonFly__
if(${CMAKE_SYSTEM_NAME} MATCHES "DragonFly")
  set(HAVE_OS_DRAGONFLYBSD 1)
else()
  set(HAVE_OS_DRAGONFLYBSD 0)
endif()

include(ECMAddAppIcon)

configure_file(config-koncrepe.h.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/config-koncrepe.h)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XKBCOMMON QUIET xkbcommon)
    endif()
    if(XKBCOMMON_FOUND)
        message(STATUS "Found xkbcommon, keys report their base layout identity")
        set(KONCREPE_HAVE_XKBCOMMON TRUE)
    else()
        message(STATUS "xkbcommon not found — keys report only their active layout identity")
    endif()
else()
    message(STATUS "Not on Linux — keys report only their active layout identity")
endif()

### Tests
if(BUILD_TESTING)
        find_package(Qt6Test ${QT_MIN_VERSION} CONFIG REQUIRED)
        add_subdirectory(autotests)
        add_subdirectory(tests)
endif()

### Security concerns about reading arbitrary screen positions
option(ENABLE_DECRQCRA "Koncrepe: enable DEC request checksum rectangular area" OFF)

### Development tools
option(KONCREPE_BUILD_UNI2CHARACTERWIDTH "Koncrepe: build uni2characterwidth executable" OFF)

set(dbus_xml_srcs)
if(HAVE_DBUS)
    ### Koncrepe source files shared between embedded terminal and main application
    # qdbuscpp2xml -m  Session.h -o org.kde.koncrepe.Session.xml
    # qdbuscpp2xml -M -s ViewManager.h -o org.kde.koncrepe.Koncrepe.xml

    # Generate dbus .xml files; do not store .xml in source folder
    qt_generate_dbus_interface(ViewManager.h org.kde.koncrepe.Window.xml OPTIONS -m)

    qt_add_dbus_adaptor(windowadaptors_SRCS
                        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.koncrepe.Window.xml
                        ViewManager.h
                        Koncrepe::ViewManager)

    # qdbuscpp2xml -m  Session.h -o org.kde.koncrepe.Session.xml
    # Generate dbus .xml files; do not store .xml in source folder
    qt_generate_dbus_interface(session/Session.h org.kde.koncrepe.Session.xml OPTIONS -m)

    qt_add_dbus_adaptor(
        sessionadaptors_SRCS
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.koncrepe.Session.xml
        session/Session.h
        Koncrepe::Session
    )

    set(dbus_xml_srcs
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.koncrepe.Session.xml
        ${CMAKE_CURRENT_BINARY_DIR}/org.kde.koncrepe.Window.xml
    )
endif()

add_library(koncrepelogging OBJECT)
set_target_properties(koncrepelogging PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(koncrepelogging PUBLIC Qt::Core)

ecm_qt_declare_logging_category(koncrepelogging
    HEADER koncrepedebug.h
    IDENTIFIER KoncrepeDebug
    CATEGORY_NAME org.kde.koncrepe
    DESCRIPTION "Koncrepe"
    EXPORT KONCREPE
)

add_subdirectory(colorscheme)
add_subdirectory(keyboardtranslator)
add_subdirectory(characters)
add_subdirectory(decoders)
add_subdirectory(containers)
add_subdirectory(plugins)

add_library(koncrepeprivate)

target_sources(koncrepeprivate PRIVATE
    ${windowadaptors_SRCS}
    BookmarkHandler.cpp
    BookmarkMenu.cpp
    CheckableSessionModel.cpp
    CopyInputDialog.cpp
    Emulation.cpp
    EscapeSequenceUrlExtractor.cpp
    FontDialog.cpp
    HistorySizeDialog.cpp
    KeyBindingEditor.cpp
    LabelsAligner.cpp
    NullProcessInfo.cpp
    PrintOptions.cpp
    ProcessInfo.cpp
    Pty.cpp
    RenameTabDialog.cpp
    SSHProcessInfo.cpp
    SaveHistoryTask.cpp
    SaveHistoryAutoTask.cpp
    Screen.cpp
    ScreenWindow.cpp
    ScrollState.cpp
    SearchHistoryTask.cpp
    ShellCommand.cpp
    ShittyEmulation.cpp
    ShittyStreamScanner.cpp
    ShouldApplyProperty.cpp
    UnixProcessInfo.cpp
    ViewManager.cpp
    ViewProperties.cpp
    WindowSystemInfo.cpp
    ZModemDialog.cpp
    filterHotSpots/EscapeSequenceUrlFilter.cpp
    filterHotSpots/EscapeSequenceUrlFilterHotSpot.cpp
    filterHotSpots/FileFilter.cpp
    filterHotSpots/FileFilterHotspot.cpp
    filterHotSpots/Filter.cpp
    filterHotSpots/FilterChain.cpp
    filterHotSpots/HotSpot.cpp
    filterHotSpots/RegExpFilter.cpp
    filterHotSpots/RegExpFilterHotspot.cpp
    filterHotSpots/TerminalImageFilterChain.cpp
    filterHotSpots/UrlFilter.cpp
    filterHotSpots/UrlFilterHotspot.cpp
    filterHotSpots/ColorFilter.cpp
    filterHotSpots/ColorFilterHotSpot.cpp
    history/HistoryFile.cpp
    history/HistoryScroll.cpp
    history/HistoryScrollFile.cpp
    history/HistoryScrollNone.cpp
    history/HistoryType.cpp
    history/HistoryTypeFile.cpp
    history/HistoryTypeNone.cpp
    history/compact/CompactHistoryScroll.cpp
    history/compact/CompactHistoryType.cpp
    widgets/DetachableTabBar.cpp
    widgets/EditProfileDialog.cpp
    widgets/HistorySizeWidget.cpp
    widgets/IncrementalSearchBar.cpp
    widgets/IncrementalSearchBarSizeGrip.cpp
    widgets/RenameTabWidget.cpp
    widgets/TabTitleFormatButton.cpp

    searchtabs/SearchTabs.cpp
    searchtabs/SearchTabsModel.cpp

    terminalDisplay/extras/CompositeWidgetFocusWatcher.cpp
    terminalDisplay/extras/AutoScrollHandler.cpp
    terminalDisplay/extras/HighlightScrolledLines.cpp

    terminalDisplay/TerminalDisplay.cpp
    terminalDisplay/TerminalPainter.cpp
    terminalDisplay/TerminalScrollBar.cpp
    terminalDisplay/TerminalColor.cpp
    terminalDisplay/TerminalFonts.cpp
    terminalDisplay/TerminalBell.cpp

    profile/Profile.cpp
    profile/ProfileCommandParser.cpp
    profile/ProfileGroup.cpp
    profile/ProfileList.cpp
    profile/ProfileReader.cpp
    profile/ProfileWriter.cpp
    profile/ProfileManager.cpp
    profile/ProfileModel.cpp

    ${sessionadaptors_SRCS}
    session/Session.cpp
    session/SessionController.cpp
    session/SessionDisplayConnection.cpp
    session/SessionGroup.cpp
    session/SessionListModel.cpp
    session/SessionManager.cpp
    session/SessionTask.cpp

    widgets/TerminalDisplayAccessible.cpp
    widgets/TerminalHeaderBar.cpp
    widgets/ViewContainer.cpp
    widgets/ViewSplitter.cpp
    widgets/KoncrepePrintManager.cpp

    ${dbus_xml_srcs}

    ../data/data.qrc
    ../desktop/koncrepe.qrc
)

if(WIN32)
    # windows conpty backend
    target_sources(koncrepeprivate PRIVATE
        ptyqt/iptyprocess.cpp
        ptyqt/conptyprocess.cpp
    )
endif()

kconfig_add_kcfg_files(koncrepeprivate settings/KoncrepeSettings.kcfgc)

ki18n_wrap_ui(koncrepeprivate
    CopyInputDialog.ui
    HistorySizeDialog.ui
    KeyBindingEditor.ui
    PrintOptions.ui       # Temporary to build session static library
    RenameTabDialog.ui
    settings/GeneralSettings.ui
    settings/MemorySettings.ui
    settings/PartInfo.ui
    settings/ProfileSettings.ui
    settings/TabBarSettings.ui
    settings/TemporaryFilesSettings.ui
    settings/ThumbnailsSettings.ui
    widgets/EditProfileAdvancedPage.ui
    widgets/EditProfileAppearancePage.ui
    widgets/EditProfileGeneralPage.ui
    widgets/EditProfileKeyboardPage.ui
    widgets/EditProfileMousePage.ui
    widgets/EditProfileScrollingPage.ui
    widgets/EditProfileTabsPage.ui
    widgets/HistorySizeWidget.ui
    widgets/RenameTabWidget.ui
)

generate_export_header(koncrepeprivate)

if(KONCREPE_HAVE_XKBCOMMON)
    target_compile_definitions(koncrepeprivate PUBLIC HAVE_XKBCOMMON)
    target_include_directories(koncrepeprivate PUBLIC ${XKBCOMMON_INCLUDE_DIRS})
    target_link_libraries(koncrepeprivate PRIVATE ${XKBCOMMON_LIBRARIES})
endif()

target_link_libraries(koncrepeprivate
    PUBLIC
        KF6::ConfigCore
        KF6::XmlGui
    PRIVATE
        koncrepecolorscheme
        keyboardtranslator
        koncrepecharacters
        koncrepedecoders
        koncrepecontainers
        koncrepelogging
        KF6::Bookmarks
        KF6::BookmarksWidgets
        KF6::GuiAddons
        KF6::I18n
        KF6::IconWidgets
        KF6::KIOWidgets
        KF6::NewStuffCore
        KF6::NewStuffWidgets
        KF6::Notifications
        KF6::TextWidgets
        KF6::WindowSystem
        Qt::PrintSupport
        Qt::Xml
        ICU::uc
        ICU::i18n
        Shitty::Vt
)

if (HAVE_LIBSSH)
    target_link_libraries(koncrepeprivate PRIVATE ssh)
endif()

if(NOT WIN32)
    target_link_libraries(koncrepeprivate PRIVATE KF6::Pty)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    #kinfo_getfile() is in libutil
    target_link_libraries(koncrepeprivate PRIVATE util)
endif()

if(HAIKU)
    target_link_libraries(koncrepeprivate PRIVATE network) # for undefined reference to `inet_aton'
endif()

set_target_properties(koncrepeprivate PROPERTIES
    VERSION ${RELEASE_SERVICE_VERSION}
)

install(TARGETS koncrepeprivate ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)

### Koncrepe Application

add_library(koncrepeapp
    Application.cpp
    MainWindow.cpp
    settings/ConfigurationDialog.cpp
    settings/ConfigDialogButtonGroupManager.cpp
    settings/TemporaryFilesSettings.cpp
    settings/GeneralSettings.cpp
    settings/MemorySettings.cpp
    settings/ProfileSettings.cpp
    settings/TabBarSettings.cpp
    settings/ThumbnailsSettings.cpp
    pluginsystem/IKoncrepePlugin.cpp
    pluginsystem/PluginManager.cpp
    delegates/ProfileShortcutDelegate.cpp
)

generate_export_header(koncrepeapp)

target_compile_definitions(koncrepeapp PRIVATE -DRELEASE_SERVICE_VERSION="${RELEASE_SERVICE_VERSION}")

target_link_libraries(koncrepeapp
    PRIVATE
        koncrepeprivate
        koncrepelogging
        KF6::Bookmarks
        KF6::Crash
        KF6::I18n
        KF6::KIOWidgets
        KF6::NotifyConfig
        KF6::WindowSystem
)
if(HAVE_DBUS)
    target_link_libraries(koncrepeapp
        PRIVATE
            KF6::GlobalAccel
    )
endif()

set_target_properties(koncrepeapp PROPERTIES
    VERSION ${RELEASE_SERVICE_VERSION}
)

install(TARGETS koncrepeapp ${KDE_INSTALL_TARGETS_DEFAULT_ARGS} LIBRARY NAMELINK_SKIP)


# Sets the icon on Windows and OSX
ecm_add_app_icon(ICONS_SOURCES ICONS ${ICONS_SRCS})

add_executable(koncrepe
    main.cpp
)

target_link_libraries(koncrepe
    koncrepeprivate
    koncrepeapp
    KF6::CoreAddons
    KF6::Crash
    KF6::I18n
    KF6::IconThemes
    KF6::WindowSystem
)
if(HAVE_DBUS)
    target_link_libraries(koncrepe
        KF6::DBusAddons
    )
endif()

if(APPLE)
    set_target_properties(koncrepe PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.kde.koncrepe"
        MACOSX_BUNDLE_BUNDLE_NAME "Koncrepe"
        MACOSX_BUNDLE_DISPLAY_NAME "Koncrepe"
        MACOSX_BUNDLE_INFO_STRING "Koncrepe, the KDE terminal emulator"
        MACOSX_BUNDLE_LONG_VERSION_STRING "Koncrepe ${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${RELEASE_SERVICE_VERSION_MAJOR}.${RELEASE_SERVICE_VERSION_MINOR}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${RELEASE_SERVICE_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "1997-2022 The Koncrepe Developers")
endif()

install(TARGETS koncrepe ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES settings/koncrepe.kcfg DESTINATION ${KDE_INSTALL_KCFGDIR})

### Embedded Koncrepe KPart

add_library(koncrepepart MODULE
    Part.cpp
    settings/PartInfo.cpp
    settings/ProfileSettings.cpp
    delegates/ProfileShortcutDelegate.cpp
)

target_link_libraries(koncrepepart
    koncrepeprivate
    koncrepelogging
    KF6::I18n
    KF6::Parts
)

install(TARGETS koncrepepart DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/parts)

# Add cmake-options -DBUILD_COVERAGE=ON -DCOVERAGE_GENERATE_HTML=ON
if(COVERAGE_GENERATE_HTML)
  # find required tools - no error checking
  find_program(LCOV lcov REQUIRED)
  find_program(GENHTML genhtml REQUIRED)

  add_custom_target(coverage
    # runs tests to get .gcda files
    COMMAND ctest --progress
    # generate data
    COMMAND ${LCOV} --directory . --capture --output-file coverage.info
    # generate report
    COMMAND ${GENHTML} --ignore-errors source --demangle-cpp -o coverage coverage.info
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
