#pragma once
#define PLUG_NAME "Der Tondehr Crunchy"
#define PLUG_MFR "Der Tondehr"
#define PLUG_VERSION_HEX 0x00001019
#define PLUG_VERSION_STR "0.10.25"
#define PLUG_UNIQUE_ID 'DtCr'
#define PLUG_MFR_ID 'DTdh'
#define PLUG_URL_STR ""
#define PLUG_EMAIL_STR ""
#define PLUG_COPYRIGHT_STR "Copyright 2026 Diego Rodriguez"
#define PLUG_CLASS_NAME DerTondehrCrunchy
#define BUNDLE_NAME "DerTondehrCrunchy"
#define BUNDLE_MFR "DerTondehr"
#define BUNDLE_DOMAIN "com"
#define SHARED_RESOURCES_SUBPATH "DerTondehrCrunchy"
#define PLUG_CHANNEL_IO "1-1 1-2 2-2"
#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 0
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
#define PLUG_HAS_UI 1
#define PLUG_WIDTH 1180
#define PLUG_HEIGHT 520
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 1
#define PLUG_MIN_WIDTH 413
#define PLUG_MIN_HEIGHT 182
// IMPORTANT: on the Windows APP wrapper these values are used by WM_GETMINMAXINFO
// as TOP-LEVEL WINDOW tracking limits, not as IGraphics client-area limits.
// Keep generous headroom for title/menu/frame + Windows DPI. The actual visible
// editor maximum is still restricted to the six screen-aware phases in
// ui/EditorScale.h / ConstrainEditorResize().
#define PLUG_MAX_WIDTH 8192
#define PLUG_MAX_HEIGHT 8192
#define VST3_SUBCATEGORY "Fx|Distortion|Guitar"
#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64
