// Parches de este juego. Vacío hasta la primera corrida en consola: cada parche se agrega con
// evidencia (log/dump + desensamblado con tools/annotate.py) y su dirección del Mach-O.
#include "imports.h"
#include "log.h"
#include "macho.h"
#include "patch.h"

const import_entry patch_imports[] = {
    {"", 0, 0},
};
TABLE_END(patch)

void patch_init(void) {}
