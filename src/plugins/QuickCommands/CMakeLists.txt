
kcoreaddons_add_plugin(koncrepe_quickcommandsplugin
SOURCES
    quickcommandsplugin.cpp
    quickcommandswidget.cpp
    quickcommandsmodel.cpp
    filtermodel.cpp
INSTALL_NAMESPACE
    "koncrepeplugins"
)

ki18n_wrap_ui(koncrepe_quickcommandsplugin qcwidget.ui)

configure_file(koncrepe_quickcommands.in.json koncrepe_quickcommands.json)
target_link_libraries(koncrepe_quickcommandsplugin
    koncrepeprivate
    koncrepeapp
    koncrepelogging
    KF6::CoreAddons
    KF6::I18n
)
