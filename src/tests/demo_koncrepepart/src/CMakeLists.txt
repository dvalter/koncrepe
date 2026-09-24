add_executable(demo_koncrepepart
    main.cpp
    demo_koncrepepart.cpp
)

target_link_libraries(demo_koncrepepart
    KF6::CoreAddons
    KF6::I18n
    KF6::Parts
    KF6::XmlGui
    KF6::WindowSystem
    Qt::Widgets
)
