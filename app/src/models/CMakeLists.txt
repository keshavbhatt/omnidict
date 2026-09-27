# models: Qt item models over core data, for the views in ui. No widgets.

qt_add_library(omnidict_models STATIC
    results_model.cpp
    results_model.h
)
omnidict_set_warnings(omnidict_models)

target_include_directories(omnidict_models PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(omnidict_models
    PUBLIC
        omnidict_core
)
