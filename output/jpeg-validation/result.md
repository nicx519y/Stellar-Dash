JPEG implementation verification

Passed: Web TypeScript; JPEG/GIF/Mock targeted tests; server image-gallery tests;
production QSPI handler and JPEG player native tests; unlocked STM32 compilation;
hosted build and variant isolation; rebuilt browser page and real Worker GIF/JPEG check.
Two unrelated WebHID source-text assertions fail on both committed and modified source.
The new Mock test assertion was corrected from error.kind to error.reason and passed.
Final browser fix: explicit .cjs/.d.cts shared module; no algorithm changes.
Existing passing tests were reused at user request. Hosted rebuild: 35.28 seconds.
Browser assertions: 6 FPS / 6 playback ticks / 2 unique JPEG frames, RGB color check,
no uncaught page errors. Screenshot: output/playwright/jpeg-page-fixed.png.
Static background preview: 127.0.0.1:4017, PID in preview-server.pid.
No device flashing or hardware performance acceptance performed.
