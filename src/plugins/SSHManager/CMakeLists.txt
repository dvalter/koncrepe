
kcoreaddons_add_plugin(koncrepe_sshmanagerplugin
SOURCES
    sshmanagerplugin.cpp
    sshmanagerpluginwidget.cpp
    sshmanagermodel.cpp
    sshmanagerfiltermodel.cpp
    sshtreeview.cpp
INSTALL_NAMESPACE
    "koncrepeplugins"
)

ki18n_wrap_ui(koncrepe_sshmanagerplugin sshwidget.ui)

ecm_qt_declare_logging_category(koncrepe_sshmanagerplugin
    HEADER sshmanagerplugindebug.h
    IDENTIFIER SshManagerPluginDebug
    CATEGORY_NAME org.kde.koncrepe.plugin.sshmanager
    DESCRIPTION "Koncrepe plugin (SSH Manager)"
    EXPORT KONCREPE
)

configure_file(koncrepe_sshmanager.in.json koncrepe_sshmanager.json)
target_link_libraries(koncrepe_sshmanagerplugin
    koncrepeprivate
    koncrepeapp
    KF6::CoreAddons
    KF6::Crash
    KF6::I18n
)
