# Real Soccer 2009 — Progreso del port (iOS → PS Vita)

## 2026-11-04 — Inicialización y runtime completo (todo offline; falta solo probar en consola)

### Hecho
- `psvita-toolkit init --ipa "IPA/Real Soccer 2009_v.1.5.7.ipa" --name "Real Soccer 2009" --titleid PSVRS0009 --slug realsoccer09 --vita-ip 192.168.3.15` (commit del scaffold).
- Binario `RF09`: armv6 (ARM), cryptid 0 (descifrado). MinimumOSVersion 2.2.1.
- Motor: Gameloft C++ propio. ObjC: iRF09AppDelegate, EAGLView (+ IGP/Reachability).
- Arranque: `MainWindow.nib` → delegate `iRF09AppDelegate`, `UIWindow` 320x480 y `EAGLView` (outlets `window`, `glView`), precompilado a `nib.txt`.
- **El boilerplate iOS que copia el toolkit no compila ni tiene runtime ObjC** → se reemplazó por el loader probado de Inotia2-vita + un runtime nuevo y común a los 6 ports iOS (copia maestra en `Ferrari-GT-Evolution-vita/source`, se sincroniza con `tools/sync_runtime.sh <port>`). Detalle en `PORTING_PLAN.md` §1.
- `tools/ios_prep.py` → `rebase.bin` **13215** palabras, `binds.bin` **835** slots / 305 símbolos. Modo: clásico sin relocs (heurístico + estructural). Cobertura de `__text`: 95.8%; 6676 literales puntero, 1664 palabras de tablas de salto; 2499 candidatos rechazados (líneas `DUDA` de `report.txt`).
  - El clasificador se validó contra HAWX (que trae símbolos): 0 punteros perdidos, 0 falsos positivos en datos.
- Cobertura de imports: todos resuelven (implementación, clase vacía autogenerada, NSString constante sintetizada o stub `unimpl` que lo registra en el log).
- C++: `tools/gen_cxx.py` mapea la libstdc++ de iOS (ABI COW) a la de VitaSDK (`source/cxx_imports.c`).
- Audio: OpenAL + `alBufferDataStatic` (vía `alcGetProcAddress`), 96 `.wav`; música `AudioFile`+`AudioQueue` (7 `.m4a` → `.ogg`).
- Video: No trae videos.
- Controles: Partido 100% táctil (botones pase/tiro en pantalla) → `controls.txt` a calibrar; acelerómetro sin uso esperado.
- Rotación del drawable: 1 (Info.plist: UIInterfaceOrientationLandscapeRight).
- Build OK: `psvita-toolkit build --preset debug` → `build/realsoccer09.vpk`.
- Datos preparados: `tools/stage_data.sh` → `ux0_data/realsoccer09/` (bundle, `rebase.bin`, `binds.bin`, `nib.txt`, `controls.txt`, música `.ogg`).

### Para probar en consola (Vita 192.168.3.15, VitaShell FTP en 1337)
1. Instalar `build/realsoccer09.vpk`.
2. `python3 -I tools/deploy_data.py` (sube `ux0_data/realsoccer09/` → `ux0:data/realsoccer09/`).
3. Requisitos en la consola: `kubridge.skprx` en `ur0:tai/config.txt` y `libshacccg.suprx` (ShaRKBR33D).
4. Abrir el juego; si se cierra o queda en negro: `python3 -I tools/deploy_data.py --logs` y leer `logs/realsoccer09_NNN.log` (+ `.psp2dmp`).
5. Rotación incorrecta: mantener SELECT+START 2 s (cicla y guarda `rotation.txt`).
6. Botones: editar `ux0:data/realsoccer09/controls.txt` por FTP (coordenadas de lo que se ve), sin recompilar.

### Riesgos conocidos
- Coordenadas de los botones de pase/tiro: ajustar `controls.txt` en consola.

### Revisión independiente del runtime (misma sesión, antes de la consola)
Un revisor aparte leyó asm.S/objc.c/macho.c. Se corrigió:
- **Crítico:** las cadenas ObjC (selectores, nombres de clase, UTF-16) viven en `__TEXT` y se leían antes de `macho_finalize()` (el bloque RX todavía estaba vacío). Ahora `foundation_fix_cfstrings` + `objc_init_image` corren después; las escrituras en `__TEXT` van por `macho_write_word` (kubridge).
- Pool de autorelease por hilo (TLS); antes era uno solo compartido → use-after-free con hilos/GCD.
- Caché de métodos sin carrera (cls=0 → imp/sel → cls, con barreras y re-chequeo).
- `-dealloc` no puede correr dos veces (rc centinela durante el dealloc).
- `methodForSelector:` devuelve un thunk con `ml_align_call` para IMPs nativas; `objc_ivar_set` libera el valor anterior.
- Pendiente (latente, sin uso en estos binarios): `+initialize`/`+load` no se envían; `nil_imp` en llamadas `_stret` deja el struct sin tocar.
- Verificado correcto por el revisor: balance de pila/registros de todos los trampolines, `ml_dispatch_imp` (Thumb incluido), `objc_msgSendSuper2`, setjmp, cadena de metaclases, mapeo y thunks de macho.c, firmas nativas con CGRect/NSRange/double.

## 2026-11-06 — Corrida 002: crash en el primer mod_init
- Data abort en PC 0x980d4bd4 = stub de `memset` (0xd4bd0), llamado desde `CGLFontJP::CGLFontJP` (mod_init[0]); r12=0xea734 sin rebasear.
- Causa: este binario usa `__symbol_stub4` de **12 bytes** (`ldr ip,[pc]; ldr pc,[ip]; .long lazy_ptr`) y `ios_prep.py` solo rebaseaba la variante de 8.
- Fix: `ios_prep.py` acepta stubs de 8 y 12 (literal en `+r2-4`). rebase.bin 13215 → 13440 palabras (+225 stubs).
- Los otros ports tienen el `ios_prep.py` viejo (solo 8); revisar si alguno trae stubs de 12.

## 2026-11-06 — Corrida 004: bad_alloc en GameInit
- Ya pasa mod_init, main(), nib, EAGLView. Crash: `operator new` → `bad_alloc` → abort, desde `GameInit` (0xb78d8).
- Causa: `new CGlobal` usa el literal 0x5770 (= sizeof) en 0xb7938; `ios_prep.py` lo rebaseó (→ 0x98005770).
  El clasificador agregaba a `ends` ("posible inicio de función débil") la dirección tras **cualquier** instrucción
  incondicional, no solo tras saltos; así casi cualquier entero en rango de `__text` usado como argumento pasaba como callback.
- Fix en `ios_prep.py`: `ends` solo tras terminadores reales (los `break` del escaneo: b/bx/pop pc/ldr pc/mov pc/tablas)
  y tras `bl` a funciones noreturn (`__Unwind_SjLj_Resume`, `__cxa_throw`, `abort`, ...; si no, se pierden landing pads SjLj).
- rebase.bin 13440 → 13341 (99 enteros menos). Validado en HAWX (con símbolos): 450 palabras menos, 0 eran destinos con símbolo.

## 2026-11-06 — Corrida 005: otro bad_alloc (tamaño en rango de datos)
- Tras abrir res.pak: `new` de 0x141928 bytes (literal 0x70bc8, apunta dentro de __DATA) rebaseado como puntero.
- Fix en `ios_prep.py`: `_classify_use` devuelve `size` cuando el literal llega al registro de tamaño de una llamada
  a `operator new/new[]`, `malloc/calloc/realloc/bzero` o `memset/memcpy/memmove/memcmp/strncpy/strncmp`
  (`SIZE_ARGS`) → nunca se rebasea (`DUDA lit-size-arg` en report.txt).
- RF09: 13341 → 13339 (0x70bc8 = 0x141928 y 0xb2ba8 = 0x9ab0, ambos `new`). HAWX: sin cambios.

## 2026-11-06 — Corrida 006: data abort en zlib inflate_table (al leer res.pak)
- `lens[17] = 0xe58d` (basura): `order[]` de zlib (0xe7960, halfwords 16,17 = 0x00110010) se rebaseaba porque el
  valor cae en __DATA → `order[1] = 0x9811`, nunca se escribe `lens[17]`. También se corrompían lenfix/distfix (0xe72e8...).
- Fix en `ios_prep.py`: en constantes de `__TEXT`, una palabra heurística vecina de un número (en rango ya descartado,
  o 0 < v < 0x1000) es tabla numérica → `DUDA data->in-num-table`. Solo `__TEXT`: en `__DATA` los structs mezclan
  punteros y números (probado: aplicado a __DATA perdía 72 punteros reales en HAWX).
- RF09: 13339 → 13307. HAWX: −11, ninguno apunta a símbolo.

## 2026-11-06 — Corrida 007: free() de heap corrupto tras inflate fallido
- `uncompress` → `inflate` devuelve error y escribe fuera; `inflateEnd` → `free` revienta (chunk 0x01010101).
- Quedaban rebaseadas tablas de zlib (lenfix/distfix/lbase/lext/dbase): corridas de valores empaquetados sin vecino numérico.
- Fix en `ios_prep.py` (constantes de __TEXT, usando símbolos locales como límites de objeto):
  - objeto con alguna palabra numérica → todas sus palabras heurísticas son números, salvo destino a cadena o
    función que sea ≥1/8 de las palabras (tablas {id, callback}); lenfix tiene {16,7,10} = 0xa0710 = una función por casualidad.
  - objeto que empieza desalineado (`unsigned short lext[]`) → numérico.
- RF09: 13307 → 13233; zlib sin rebases. HAWX −28 (0 con símbolo). Lo quitado en RF09 con "destino símbolo" son
  coincidencias verificadas (tabla de atan en 0xdc340, lenfix).

### 2026-11-07 — corrida 009: texturas blancas
- Log de subidas: todas las glTexImage2D/glCompressedTexImage2D sin error → el problema no era la carga.
- Causa: literales `GL_TEXTURE_COORD_ARRAY` (0x8078) y `GL_FRAMEBUFFER/RENDERBUFFER_OES` (0x8D40/41) caen
  dentro de `__text` y `ios_prep.py` los rebaseaba → `glEnableClientState(0x98008078)` nunca activaba UVs.
- Fix `ios_prep.py`: literal pasado en r0–r3 a un import `_gl*` que apunta a `__text` = enum (`lit-gl-enum`).
  rebase: 13233 → 13219 palabras. Queda 0xA3E04 (0x8D41 como arg de presentRenderbuffer:, inofensivo).

### 2026-11-07 — corrida 010: pantalla negra al entrar al partido
- Texturas OK en menús (fix 009 confirmado). Tras la carga del partido, drawView sigue (~40/s) pero casi
  no se presentan frames; el partido arranca con `m_match_intro.m4a`, que fallaba al abrirse.
- Causa del audio: `AudioFileOpenURL` hacía fopen nativo con la ruta iOS `/var/mobile/.../RF09.app/...`.
- Fix: `source/audio.c` sincronizado desde la maestra (Ferrari), que ya traduce con `darwin_path` y trae
  AudioFileStream WAV + GetPropertyInfo coherente. Pendiente confirmar si era lo que bloqueaba el partido.

### 2026-11-07 — corrida 011: partido negro pero vivo (~10 fps, ~75 draws/frame)
- No es cuelgue: se presentan frames durante >70 s, todo negro. (Esta corrida no pasó por "sound init".)
- Causa probable: `GlRenderer::Init` (0x12F84) hace 100 glPushMatrix + 100 glPopMatrix. vitaGL con
  SKIP_ERROR_HANDLING no controla límites (modelview 32, proyección/textura 2, contador uint8) → pisa sus
  globales.
- Fix `gles.c`: pilas de matrices propias (32 por modo, glGetFloatv/glLoadMatrixf, push lleno/pop vacío =
  no-op). vitaGL solo ve push/pop de gles_present y del skinning.

### 2026-11-07 — corrida 012: partido visible (fix pilas de matrices OK), ~9 fps, cruceta/stick sin efecto
- Audio: rutas OK (m_title/m_menu .ogg suenan); SFX fallan en "Error generating buffer: a004" (OpenAL, pendiente).
- Control: el partido mueve con un pad táctil (CTouchScreen zona 0 = vista (0,0)-(160,160), mapa de 8
  direcciones). Nuevo `STICK x y r` en controls.txt (input.c común, opt-in): stick/cruceta = dedo virtual
  (MAX_TOUCH 9 en uikit.c). RF09: `STICK 80 240 55`.
- Rendimiento: sin datos todavía. Perfil temporal en la línea `vivo:` (ms en conversión GL_FIXED, draws de
  vitaGL y swap por cada 10 s) para decidir qué optimizar en la próxima corrida.

### 2026-11-07 — corrida 013: pad OK, falta botón A, 6-7 fps
- Perfil: en el partido GL (fixed+draw+swap) ≈ 230 ms cada 10 s → el cuello NO es el render.
- El juego reintenta cargar sfx_quiet_crowd_1.wav ~7 veces/s (lee ~150 KB + 4 líneas de log cada vez)
  porque alGetError sin contexto daba 0xA004. Fix: `openal.c` sincronizado desde la maestra (al_GetError
  devuelve AL_NO_ERROR sin contexto, como iOS).
- controls.txt: SQUARE 330 270 = A (B = CROSS), el viejo SQUARE 430 50 pasa a TRIANGLE.

### 2026-11-07 — corrida 014: el toque no responde en la pantalla de "¿sonido?"
- Entre 013 y 014 el binario solo cambió openal.c (actúa después de esa pantalla). Sospecha: dedo virtual
  del STICK apoyado por deriva del stick (zona muerta 0.12) → el juego sigue un solo toque e ignora el real.
- Fix input.c: zona muerta radial 0.35 para STICK + log "STICK apoya/suelta" (temporal) para confirmarlo.

### 2026-11-07 — corrida 015: jugable (~25 fps en el partido); tirón al entrar a la pausa
- STICK con zona muerta 0.35 OK (el toque responde). SFX suenan (fix openal 013).
- Pausa: SoundEngineEffect::ClearSourceBuffers saca todos los buffers de una fuente pausada →
  openal-soft AL_INVALID_VALUE (a003) cada frame. Fix openal.c: si piden más que los procesados,
  alSourceStop antes de unqueue. audio.c: .ogg con ov_open + setvbuf 64 KB (reabrir m_menu.ogg ~200 ms).
- Quitados los logs temporales de texturas y STICK. Queda el perfil en `vivo:`.
