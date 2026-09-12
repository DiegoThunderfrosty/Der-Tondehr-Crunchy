# Changelog

## 0.10.25

- Improved Middle-control audibility after the saturated amplifier stages.
- Preserved the existing passive tone-stack topology and component values.
- Added a broad 450 Hz behavioral calibration from -3 dB to +3 dB across the control range.
- Kept Middle 5 at unity to preserve the previous default sound.
- Added a saturated-Lead regression check at 450 Hz.

## 0.10.24

- Corrected standalone audio-system build integration.
- Added DirectSound, ASIO, and WASAPI selection.
- Added separate device, channel, sample-rate, and buffer profiles for each backend.
- Added safer settings application and bounded driver shutdown.
- Added mono selection between the two channels of the configured input pair.
- Corrected WASAPI compilation and link requirements.
- Kept host-controlled plugin processing unchanged.

Detailed private development history is not included in the public source package because it contains research references and files that are not required to build the project.
