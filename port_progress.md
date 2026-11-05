# Real Soccer 2009 — Progreso del Port (iOS Mach-O)


### Phase 1: Repo and tools
- [x] IPA extracted, `game_app` symlink, ios-loader-boilerplate merged, CMake/`hle.h` adapted to `realsoccer09`.
- [x] Executable decrypted (cryptid 0).
- [x] `rebase.bin` + `docs/macho_report.txt` generated (`python3 -I tools/macho_prep.py game_app/RF09 ux0_data/realsoccer09/rebase.bin --report docs/macho_report.txt`).

### Phase 2: Decompilation and inventory
- [x] `decompiled/macho/` dumps (frameworks, ObjC classes, symbols, imports) -- section 2.
- [ ] Ghidra pseudo-C of `RF09` (optional but needed for crash triage).
- [ ] Classify `imports.txt` into: libc/math/pthread (map to newlib/darwin.c), GL (map to vitaGL), ObjC/CF (objc.c), audio, network/GameKit/StoreKit (stub returning "unavailable").
- [ ] Write the classification as a table in `docs/imports.md` (symbol → shim file → status).

### Phase 3: Real engine analysis
- [ ] Identify the engine by symbols (`grep -i` on `symbols.txt`: `cocos2d`, `Unity`, `SIO2`, `oolong`, `Box2D`, custom `CS_`/`GS_` prefixes), never assume "same engine as another port".
- [ ] Map the lifecycle from section 1's candidates: app delegate class, GL view class (`EAGLView`/`ES1Renderer`), how the frame loop is driven (`NSTimer`, `CADisplayLink`, `performSelector:afterDelay:`, or a game thread).
- [ ] Find where resources are opened (`fopen`, `NSBundle pathForResource:ofType:`, `UIImage imageNamed:`) -- these define what `BUNDLE_PATH` must serve.
- [ ] Detect middleware (OpenAL, FMOD, libpng, zlib, Flurry/analytics) and decide stub vs. implementation. Analytics/ads/StoreKit = stub.

### Phase 4: Loader and ObjC bootstrap (GATE: log reaches the app delegate)
- [ ] `macho_load()` maps `__TEXT`/`__DATA` at `0x98000000`, applies `rebase.bin`, binds `__nl_symbol_ptr`/`__la_symbol_ptr`.
- [ ] Every import in `imports.txt` resolves to a shim or `ml_unimpl_entry`; `[UNIMPL]` lines in the log are the to-do list.
- [ ] `macho_run_inits()` runs `__mod_init_func` (C++ static constructors) without crashing.
- [ ] ObjC: register the game's classes from `__objc_classlist`, implement `alloc/init/retain/release/autorelease`, `NSString`, `NSArray`, `NSDictionary`, `NSUserDefaults` (backed by `saves/`), `NSBundle mainBundle resourcePath`.
- [ ] Call the delegate's `applicationDidFinishLaunching:` (or `application:didFinishLaunchingWithOptions:`) via `call_guest`. Log `[BOOT] delegate ok`.

### Phase 5: Graphics (GATE: first frame on screen)
- [ ] vitaGL init (`vglInitExtended`) at 960x544, SoftFP build; `EAGLContext`/`CAEAGLLayer`/`renderbufferStorage:fromDrawable:` shims backed by the virtual FBO (`source/gles.c`).
- [ ] Orientation: rotate/scale the virtual FBO for portrait games (320x480 / 640x960 / 768x1024) to 960x544 with correct aspect.
- [ ] `presentRenderbuffer:` → `vglSwapBuffers`; drive the game's draw selector from the main loop at its native rate.
- [ ] Textures: CgBI PNG via `source/cgbi.c` (`tools/cgbi_check.py` to verify), PVRTC via vitaGL; `GL_FIXED` vertex arrays converted.
- [ ] GLES2 games: GLSL shaders → `psvita-toolkit shader-transpile` / `shader-live-reload`.

### Phase 6: Touch
- [ ] Front touch → `UITouch` objects with `locationInView:` in the game's logical coordinates (undo the FBO rotation/scale).
- [ ] Deliver `touchesBegan/Moved/Ended:withEvent:` to the view that the game registered; multitouch if `multipleTouchEnabled`.
- [ ] Rear panel disabled or mapped (see Phase 7).

### Phase 7: Physical controls
- [ ] Default mapping: D-pad/stick/✕○□△/L/R/START/SELECT → synthesized touches on the on-screen buttons (`source/input.c` touch layer) or direct calls into the game's input functions when found in `symbols.txt`.
- [ ] Analog deadzone (~8-12) and sensitivity with sane defaults.
- [ ] Remappable `controls.txt` in `ux0:data/realsoccer09/` (`VITA_BUTTON = GAME_ACTION`); missing file = defaults.

### Phase 8: Audio
- [ ] AudioUnit RemoteIO / AudioQueue / OpenAL / `AVAudioPlayer` (whichever `frameworks.txt` shows) → `sceAudioOut` thread (`source/coreaudio.c`).
- [ ] Music (often `.caf`/`.m4a`/`.mp3`) decodes; convert offline with a script if no decoder fits.
- [ ] No crackle/drift; pause on suspend; volume settings respected.

### Phase 9: Videos / FMV
- [ ] Inventory `.m4v`/`.mp4` in the bundle; `MPMoviePlayerController` shim → `sceAvPlayer`.
- [ ] Button skip and graceful skip when the video is missing (never hang on black screen).

### Phase 10: Assets, data, language and saves
- [ ] `NSHomeDirectory()`/`Documents`/`Library/Caches` → `ux0:data/realsoccer09/saves/`; bundle reads → `game_app/`.
- [ ] Boot checker: if `game_app/RF09` or `rebase.bin` is missing, show a dialog with the exact path.
- [ ] `NSLocale preferredLanguages` / `AppleLanguages` → Vita system language (`sceAppUtilSystemParamGetById`), English fallback.
- [ ] Saves survive restart and suspend.

### Phase 11: Performance
- [ ] 444/222/222/166 clocks at boot (already in `main.c`); optional PSVshell note in README.
- [ ] Stable frame time (`psvita-toolkit perf-telemetry`), memory under control (`psvita-toolkit mem-profile`), long session without leaks.

### Phase 12: Configurator and options
- [ ] Persistent settings in `saves/` (deadzones, sensitivity, FPS lock, filter, touch overlay).

### Phase 13: Trophies
- [ ] Map GameKit achievements (if `GameKit` is linked, grep `reportAchievement`/`GKAchievement`) to `sceNpTrophy` unlocks; otherwise a short achievable list.

### Phase 14: LiveArea and VPK
- [ ] `icon0.png` 128x128, `bg.png` 840x500, `startup.png` 280x158 (8-bit indexed) -- `psvita-toolkit livearea --validate`.
- [ ] `param.sfo` with Title ID `PSVRS0009`; VPK installs without warnings.

### Phase 15: Real-hardware testing and release
- [ ] Full playthrough without crashes; suspend/resume OK; Vita 1000/2000/PSTV status stated.
- [ ] Production build: `PORT_TRACE=OFF` or `psvita-toolkit log-trace --remove`.
- [ ] README states the exact IPA version tested and that the user must supply a **decrypted** IPA.
