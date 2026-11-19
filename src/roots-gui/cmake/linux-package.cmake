if(NOT roots_gui_standalone OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64" OR CMAKE_CROSSCOMPILING OR ROOTS_GUI_BUILD_TESTS)
  message(FATAL_ERROR "The Linux distribution requires a standalone x86-64 Linux build with tests disabled")
endif()

# Bundle only the application-level TLS and password-protection shared objects.
# X11, GL and its driver stack, libc, and desktop libraries come from the OS.
find_package(OpenSSL 3 REQUIRED)
find_library(roots_gui_package_sodium NAMES libsodium.so.23 REQUIRED)
foreach(library IN ITEMS "${OPENSSL_SSL_LIBRARY}" "${OPENSSL_CRYPTO_LIBRARY}" "${roots_gui_package_sodium}")
  file(REAL_PATH "${library}" resolved_library)
  get_filename_component(library_name "${library}" NAME)
  if(library STREQUAL OPENSSL_SSL_LIBRARY)
    set(library_name libssl.so.3)
  elseif(library STREQUAL OPENSSL_CRYPTO_LIBRARY)
    set(library_name libcrypto.so.3)
  endif()
  install(FILES "${resolved_library}" DESTINATION lib/roots-gui RENAME "${library_name}")
endforeach()
find_file(roots_gui_package_openssl_notice copyright
  PATHS /usr/share/doc/libssl3t64 NO_DEFAULT_PATH REQUIRED)
find_file(roots_gui_package_sodium_notice copyright
  PATHS /usr/share/doc/libsodium23 NO_DEFAULT_PATH REQUIRED)

set_target_properties(roots-gui PROPERTIES INSTALL_RPATH "$ORIGIN/../lib/roots-gui")
install(TARGETS roots-gui RUNTIME DESTINATION bin)
install(FILES
  "${roots_gui_ui_source}/imkit/assets/fonts/Inter-Regular.ttf"
  "${roots_gui_ui_source}/imkit/assets/fonts/Inter-SemiBold.ttf"
  "${roots_gui_ui_source}/imkit/assets/fonts/Inter-OFL.txt"
  DESTINATION bin/roots-gui-assets/fonts)
install(FILES packaging/org.bitcoinroots.RootsGui.desktop DESTINATION share/applications)
install(FILES assets/brand/bitcoinroots-logo.svg
  DESTINATION share/icons/hicolor/scalable/apps RENAME org.bitcoinroots.RootsGui.svg)
install(FILES docs/linux-package.md DESTINATION share/doc/roots-gui)
install(PROGRAMS packaging/install.py DESTINATION .)

set(notice_dir share/doc/roots-gui/licenses)
install(FILES packaging/COPYING.txt packaging/embedded-notices.txt DESTINATION "${notice_dir}")
install(FILES third_party/NOTICES.md DESTINATION "${notice_dir}" RENAME third-party.md)
install(FILES assets/brand/NOTICES.md DESTINATION "${notice_dir}" RENAME artwork.md)
install(FILES "${roots_gui_package_openssl_notice}" DESTINATION "${notice_dir}" RENAME openssl.txt)
install(FILES "${roots_gui_package_sodium_notice}" DESTINATION "${notice_dir}" RENAME libsodium.txt)
install(FILES third_party/licenses/Apache-2.0.txt third_party/qr/Bytewords-LICENSE DESTINATION "${notice_dir}")
install(FILES third_party/qr/quirc/LICENSE DESTINATION "${notice_dir}" RENAME quirc.txt)
install(FILES third_party/qr/stb/LICENSE DESTINATION "${notice_dir}" RENAME stb-image.txt)
install(FILES "${roots_gui_ui_source}/imkit/LICENSE" DESTINATION "${notice_dir}" RENAME imkit.txt)
install(FILES "${roots_gui_ui_source}/imkit/THIRD_PARTY_NOTICES.md" DESTINATION "${notice_dir}" RENAME imkit-dependencies.md)
install(FILES "${roots_gui_ui_source}/imgui/LICENSE.txt" DESTINATION "${notice_dir}" RENAME imgui.txt)
install(FILES "${roots_gui_ui_source}/glfw/LICENSE.md" DESTINATION "${notice_dir}" RENAME glfw.md)
install(FILES "${roots_gui_ui_source}/curl/COPYING" DESTINATION "${notice_dir}" RENAME curl.txt)
install(FILES "${roots_gui_provider_source}/LICENSE" DESTINATION "${notice_dir}" RENAME libwally.txt)
install(FILES "${roots_gui_provider_source}/src/secp256k1/COPYING" DESTINATION "${notice_dir}" RENAME secp256k1.txt)
install(FILES "${roots_gui_provider_source}/src/ctaes/COPYING" DESTINATION "${notice_dir}" RENAME ctaes.txt)
# The per-module LICENSE entries link to these common license texts.
install(FILES "${roots_gui_provider_source}/src/ccan/licenses/BSD-MIT"
  DESTINATION "${notice_dir}" RENAME ccan-BSD-MIT.txt)
install(FILES "${roots_gui_provider_source}/src/ccan/licenses/CC0"
  DESTINATION "${notice_dir}" RENAME ccan-CC0.txt)
foreach(component IN ITEMS libXcursor libXfixes libXi libXinerama libXrandr)
  install(FILES "${roots_gui_ui_source}/x11/notices/${component}/COPYING"
    DESTINATION "${notice_dir}" RENAME "${component}.txt")
endforeach()
