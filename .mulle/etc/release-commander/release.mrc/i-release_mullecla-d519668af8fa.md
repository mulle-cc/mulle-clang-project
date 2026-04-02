So at this point we are releasing (!) not developing the compiler.
It's assumed its done and dusted, including a proper version number and the `COMPATIBLE_MULLE_OBJC_RUNTIME_LOAD_VERSION` version is properly set for the to-be-released runtime in  [CGObjCMulleRuntime.cpp](mulle-clang-project/clang/lib/CodeGen/CGObjCMulleRuntime.cpp) 

In the Settings change `MULLE_CLANG_PROJECT_TAG` to the desired release tag.