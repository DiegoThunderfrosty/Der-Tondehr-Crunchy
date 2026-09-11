set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
file(READ "${ROOT}/DerTondehrCrunchy/CMakeLists.txt" AUDIO_CMAKE)
file(READ "${ROOT}/DerTondehrCrunchy/DerTondehrCrunchy.cpp" AUDIO_CPP)
file(READ "${ROOT}/DerTondehrCrunchy/DerTondehrCrunchy.h" AUDIO_H)

set(REQUIRED_CMAKE
  "if(WIN32 AND TARGET iPlug2::APP)"
  "RtAudio::WINDOWS_WASAPI"
  "target_compile_definitions(DerTondehrCrunchy-app PRIVATE __WINDOWS_WASAPI__)"
  "RTAUDIO_MINIMIZE_LATENCY"
  "options.numberOfBuffers = 2"
  "iParams.firstChannel = (mState.mAudioInChanL > 0)"
  "oParams.firstChannel = (mState.mAudioOutChanL > 0)"
  "audio_profile_directsound"
  "audio_profile_asio"
  "audio_profile_wasapi"
  "CrunchyQueryASIOBufferCaps"
  "CrunchyASIOBufferAllowed"
  "CrunchyOpenASIOControlPanelStopped"
  "sanitizeAudioStateForCurrentBackend"
  "_this->CloseAudio();"
  "audio stream open failed"
  "audio stream start failed"
  "no input buffer"
  "target_link_libraries(DerTondehrCrunchy-app PRIVATE"
  "ksuser.lib"
  "avrt.lib"
)
foreach(token IN LISTS REQUIRED_CMAKE)
  string(FIND "${AUDIO_CMAKE}" "${token}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Standalone audio integration missing token: ${token}")
  endif()
endforeach()

set(REQUIRED_CPP
  "class StandaloneMonoInputControl"
  "MONO IN"
  "LoadStandaloneAudioPreferences"
  "SaveStandaloneAudioPreferences"
  "mStandaloneMonoInputBlend"
  "0.008 * sampleRate"
  "monoScratch"
)
foreach(token IN LISTS REQUIRED_CPP)
  string(FIND "${AUDIO_CPP}${AUDIO_H}" "${token}" pos)
  if(pos EQUAL -1)
    message(FATAL_ERROR "Standalone input integration missing token: ${token}")
  endif()
endforeach()

# The backend patch may patch iPlug2::APP INTERFACE_SOURCES, but WASAPI compile/link requirements belong to the concrete Standalone executable. A regression that explicitly adds them to VST3 would violate host ownership.
string(FIND "${AUDIO_CMAKE}" "target_link_libraries(DerTondehrCrunchy-vst3" vst3_link)
if(NOT vst3_link EQUAL -1)
  message(FATAL_ERROR "Standalone backend libraries must not be linked directly to VST3")
endif()

message(STATUS "Standalone audio/driver policy validation passed")
