# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/workspaces/openobsw/build/_deps/unity-src"
  "/workspaces/openobsw/build/_deps/unity-build"
  "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix"
  "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix/tmp"
  "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix/src/unity-populate-stamp"
  "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix/src"
  "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix/src/unity-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix/src/unity-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workspaces/openobsw/build/_deps/unity-subbuild/unity-populate-prefix/src/unity-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
