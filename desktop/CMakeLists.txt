

########### install files ###############

install( PROGRAMS org.kde.koncrepe.desktop  DESTINATION ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.koncrepe.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
install(FILES org.kde.koncrepe.desktop DESTINATION ${KDE_INSTALL_DATADIR}/kglobalaccel)

install(FILES koncrepe.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install( PROGRAMS koncreperun.desktop  DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus )

include(ECMInstallIcons)
ecm_install_icons(ICONS
    icons/16-apps-koncrepe.png
    icons/22-apps-koncrepe.png
    icons/32-apps-koncrepe.png
    icons/48-apps-koncrepe.png
    icons/64-apps-koncrepe.png
    icons/128-apps-koncrepe.png
    icons/256-apps-koncrepe.png
    icons/sc-apps-koncrepe.svg
    DESTINATION ${KDE_INSTALL_ICONDIR})
