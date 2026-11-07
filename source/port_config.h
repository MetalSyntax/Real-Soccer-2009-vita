#pragma once
// Configuración propia de este juego (el resto de source/ es común a los ports iOS de la familia;
// se sincroniza con tools/sync_runtime.sh desde Ferrari-GT-Evolution-vita).
#define PORT_NAME "Real Soccer 2009"
#define PORT_SLUG "realsoccer09"
#define PORT_EXEC "RF09"            // CFBundleExecutable
#define PORT_ROTATION 1                // drawable vertical -> 1 landscape-right, 2 landscape-left, 0 vertical
#define PORT_STRETCH 0                 // 0 = conservar aspecto (816x544 con barras), 1 = estirar a 960x544
#define PORT_SYSTEM_VERSION "3.1.3"    // [UIDevice systemVersion]
#define PORT_DEVICE_MACHINE "iPhone2,1" // hw.machine (3GS)
#define GLES_POOL_MB 16
#define PORT_ACCEL_FROM_STICK 1        // stick izquierdo = inclinación del teléfono
