

########### install files ###############

install( PROGRAMS org.kde.koncrepe.desktop  DESTINATION ${KDE_INSTALL_APPDIR} )
install( FILES org.kde.koncrepe.appdata.xml DESTINATION ${KDE_INSTALL_METAINFODIR} )
install(FILES org.kde.koncrepe.desktop DESTINATION ${KDE_INSTALL_DATADIR}/kglobalaccel)

install(FILES koncrepe.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})
install( PROGRAMS koncreperun.desktop  DESTINATION ${KDE_INSTALL_DATADIR}/kio/servicemenus )
