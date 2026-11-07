# Real Soccer 2009 (iOS → PS Vita)

Port con **loader Mach-O + runtime Objective-C propios** (no soloader, no JNI). Leer `PORTING_PLAN.md`
antes de tocar nada; el estado y la bitácora están en `port_progress.md`.

## Estructura
- `source/` común a los 6 ports iOS (copia maestra en `../Ferrari-GT-Evolution-vita/source`; sincronizar
  con `tools/sync_runtime.sh <port>`). Propios de este juego: `source/port_config.h`, `source/patch.c`.
- `tools/ios_prep.py` (rebase.bin/binds.bin), `tools/nib_prep.py`, `tools/gen_cxx.py`,
  `tools/stage_data.sh` (arma `ux0_data/realsoccer09/`), `tools/deploy_data.py` (FTP, `--logs`).
- En la Vita: `ux0:data/realsoccer09/{game_app/, rebase.bin, binds.bin, nib.txt, nibs/, controls.txt, Documents/, logs/}`.

## Reglas
- ABI iOS: softfp (igual que la Vita), r9 temporal (todo con `-ffixed-r9`; llamar al juego con `call_guest`),
  pila alineada a 4 (todo lo nuestro entra por `ml_align_call`), int64 sin alinear a par de registros.
- Struct > 4 bytes se devuelve por puntero oculto (`objc_msgSend_stret`): la IMP nativa recibe `(ret*, self, _cmd, ...)`.
- Crash: PC − 0x98000000 = dirección en el Mach-O. Dirección de datos < 0x02000000 = rebase faltante (`report.txt`).
- Nada del IPA ni derivados al repo. Un bug por corrida de consola, guiado por el log. Nunca Vita3K.
