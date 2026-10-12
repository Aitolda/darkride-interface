set(TARGET_NAME auto-rig)
setup_hifi_project(Core Gui)
setup_memory_debugger()
link_hifi_libraries(shared fbx hfm graphics gpu gl auto-rigging)

include_hifi_library_headers(image)
include_hifi_library_headers(networking)
