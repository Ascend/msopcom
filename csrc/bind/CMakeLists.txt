file(GLOB OPPROF_SRC BindOpprof.cpp)
list(SORT OPPROF_SRC)
target_sources(msopprof_injection PRIVATE ${OPPROF_SRC})

file(GLOB SANITIZER_SRC BindSanitizer.cpp BindHcclSanitizer.cpp)
list(SORT SANITIZER_SRC)
target_sources(mssanitizer_injection PRIVATE ${SANITIZER_SRC})
