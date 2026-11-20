file(GLOB SOURCE
    PipeCall.cpp
    FileSystem.cpp
    ElfLoader.cpp
    Ustring.cpp
    Environment.cpp
    Path.cpp
)
list(SORT SOURCE)
target_sources(core_obj PUBLIC ${SOURCE})
