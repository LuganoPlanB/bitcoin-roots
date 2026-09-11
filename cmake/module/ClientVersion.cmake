# Copyright (c) 2026-present The Bitcoin Roots developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

include_guard(GLOBAL)

function(set_client_version_from_tag release_tag)
  if(NOT release_tag MATCHES "^v([0-9]+)\\.([0-9]+)(\\.([0-9]+))?([-+][0-9A-Za-z]+([.-][0-9A-Za-z]+)*)?$")
    message(FATAL_ERROR "Invalid release version tag: ${release_tag}")
  endif()

  set(version_major "${CMAKE_MATCH_1}")
  set(version_minor "${CMAKE_MATCH_2}")
  set(version_build "${CMAKE_MATCH_4}")
  if(version_build STREQUAL "")
    set(version_build 0)
  endif()

  set(version_rc 0)
  if(release_tag MATCHES "-rc([0-9]+)$")
    set(version_rc "${CMAKE_MATCH_1}")
  endif()

  string(SUBSTRING "${release_tag}" 1 -1 version_string)
  set(CLIENT_VERSION_MAJOR "${version_major}" PARENT_SCOPE)
  set(CLIENT_VERSION_MINOR "${version_minor}" PARENT_SCOPE)
  set(CLIENT_VERSION_BUILD "${version_build}" PARENT_SCOPE)
  set(CLIENT_VERSION_RC "${version_rc}" PARENT_SCOPE)
  set(CLIENT_VERSION_STRING "${version_string}" PARENT_SCOPE)
  set(CLIENT_VERSION_FULL "${release_tag}" PARENT_SCOPE)
  set(CLIENT_VERSION_NUMERIC "${version_major}.${version_minor}.${version_build}" PARENT_SCOPE)
endfunction()

function(configure_tagged_document source_path output_path)
  if(NOT CLIENT_VERSION_TAG)
    message(FATAL_ERROR "configure_tagged_document requires CLIENT_VERSION_TAG")
  endif()

  file(READ "${source_path}" document_content)
  set(base_version "v${CLIENT_VERSION_BASE_STRING}")
  string(FIND "${document_content}" "${base_version}" version_position)
  if(version_position EQUAL -1)
    message(FATAL_ERROR "Version placeholder ${base_version} not found in ${source_path}")
  endif()

  string(REPLACE "${base_version}" "${CLIENT_VERSION_FULL}" document_content "${document_content}")
  get_filename_component(output_dir "${output_path}" DIRECTORY)
  file(MAKE_DIRECTORY "${output_dir}")
  file(WRITE "${output_path}" "${document_content}")
endfunction()
