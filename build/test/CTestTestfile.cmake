# CMake generated Testfile for 
# Source directory: /workspaces/openobsw/test
# Build directory: /workspaces/openobsw/build/test
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(space_packet "/workspaces/openobsw/build/test/test_space_packet")
set_tests_properties(space_packet PROPERTIES  _BACKTRACE_TRIPLES "/workspaces/openobsw/test/CMakeLists.txt;13;add_test;/workspaces/openobsw/test/CMakeLists.txt;0;")
add_test(dispatcher "/workspaces/openobsw/build/test/test_dispatcher")
set_tests_properties(dispatcher PROPERTIES  _BACKTRACE_TRIPLES "/workspaces/openobsw/test/CMakeLists.txt;18;add_test;/workspaces/openobsw/test/CMakeLists.txt;0;")
add_test(tm_store "/workspaces/openobsw/build/test/test_tm_store")
set_tests_properties(tm_store PROPERTIES  _BACKTRACE_TRIPLES "/workspaces/openobsw/test/CMakeLists.txt;23;add_test;/workspaces/openobsw/test/CMakeLists.txt;0;")
subdirs("../_deps/unity-build")
