add_library(koncrepecolorscheme STATIC)

# Needed to link this static lib to shared libs
set_target_properties(koncrepecolorscheme PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(koncrepecolorscheme PRIVATE
    hsluv.c
    ColorScheme.cpp
    ColorSchemeManager.cpp
    ColorSchemeWallpaper.cpp
    ColorSchemeEditor.cpp
    ColorSchemeViewDelegate.cpp
    RandomizationRange.cpp
)

ki18n_wrap_ui(koncrepecolorscheme
    ColorSchemeEditor.ui
)

ecm_qt_declare_logging_category(koncrepecolorscheme
    HEADER colorschemedebug.h
    IDENTIFIER ColorSchemeDebug
    CATEGORY_NAME org.kde.koncrepe.colorScheme
    DESCRIPTION "Koncrepe - colorscheme"
    EXPORT KONCREPE
)

target_link_libraries(koncrepecolorscheme
    PUBLIC
        Qt::Widgets
        #TODO: using "CharacterColor.h" frm koncrepecharacter in ColorSchemeWallpaper.h
    PRIVATE
        KF6::ConfigCore
        KF6::I18n
        KF6::WidgetsAddons
)
