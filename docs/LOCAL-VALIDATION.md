# Verificación de 0.1.0-alpha

Fecha de trabajo: 2026-11-01 (America/Lima). Los registros automáticos utilizan UTC.

## MSVC y publicación

- Repositorio público: [americoCB28/RecoveryDesk](https://github.com/americoCB28/RecoveryDesk), con comunicación privada de vulnerabilidades habilitada.
- [CI MSVC aprobada](https://github.com/americoCB28/RecoveryDesk/actions/runs/41368050460): compilación x64 con CRT estático, pruebas, integración de lectura del adaptador real, análisis estático, revisión de scripts e inspección de importaciones PE.
- [Construcción de la versión candidata](https://github.com/americoCB28/RecoveryDesk/actions/runs/41366300758): ZIP de código propio, SHA-256, inventario de componentes y atestación de procedencia generada por GitHub. La procedencia del ZIP descargado también se verificó con `gh attestation verify` (salida 0).
- Compilador del candidato: MSVC 19.44.35229.0. El constructor WinPE está habilitado para esta compilación. La habilitación no equivale a certificar hardware.
- SHA-256 del ZIP candidato: `1a5ecf04ef6d81e41a2c217f9a9a0c726f0e9bf75d8a0ee919fdbda8493cb615`.
- SHA-256 del ejecutable candidato: `07948beca73208f7f630bc305534d4c1d99f0994e2937b2d6517b89b3009c86a`.
- La inspección del ejecutable no detecta importaciones de CRT dinámico. Solo importa COMCTL32, GDI32, bcrypt, ole32, OLEAUT32, ADVAPI32, SHELL32, KERNEL32 y USER32. Esta inspección no demuestra por sí sola arranque en WinPE.
- Ejecución del candidato en Windows real, sin elevación: GUI iniciada y detección de lectura completada. Secure Boot y el proveedor BitLocker quedaron como desconocidos/no disponibles con esos permisos; no se presentan como desactivados ni se consideran aprobados.
- [Alpha publicada](https://github.com/americoCB28/RecoveryDesk/releases/tag/v0.1.0-alpha.1), con el mismo ZIP y checksum indicados arriba. La publicación distingue expresamente Windows verificado de WinPE y hardware pendientes.

## MSVC local y herramientas de WinPE

- Instalados Visual Studio Build Tools 2022 17.14.37827.9 y Windows SDK 10.0.26100.0, usando instaladores Microsoft con firma válida.
- Compilación local con MSVC 19.44.35229.0 en `build/msvc-local`: 30 casos de comportamiento y una prueba del adaptador Windows real, **31 aprobadas, 0 fallidas**. El diagnóstico solo leyó inventario y publicó recuentos; no modificó cuentas ni discos.
- Inspección PE local aprobada sin CRT dinámico; SHA-256 del ejecutable local: `1313f0d2c691e706084c9d90aecd34e10b2b4149930d462fda075ed7e4162cbc`. El ejecutable de `dist/RecoveryDesk` ahora es MSVC y habilita el constructor WinPE. El ZIP público conserva su propio hash y procedencia.
- Instalado ADK Deployment Tools; versión DISM del ADK: `10.0.26100.9457`. La instalación del complemento WinPE se inició por separado y su finalización debe comprobarse en `.tools/install-status.json`.
- La primera tarea elevada agotó su espera de una hora antes de terminar la descarga de WinPE. El script corregido espera hasta tres horas. La continuación solicita UAC de Windows para montar y generar la ISO; no se registra un arranque como aprobado sin evidencia del invitado.

## Verificación inicial de Windows

Antes de instalar MSVC se compiló con LLVM-MinGW 20261020 / Clang 23.1.4, CMake 4.1.2 y Ninja 1.13.0, aislados en `.tools`. Ese ejecutable experimental mantiene bloqueado el constructor WinPE.

Se ejecutaron pruebas de comportamiento con archivos sintéticos: integridad, reanudación, privacidad, exclusión de almacenes de credenciales e identidad de almacenamiento/USB. También se ejecutó una integración de lectura del adaptador Windows y se revisó visualmente la interfaz en modo demostración. La automatización de clics por accesibilidad no fue estable; no se certifica un recorrido UI automatizado completo.

## Alcance de laboratorio

La instalación y los artefactos locales se registran en `.tools` y `artifacts`, excluidos de Git y de las publicaciones. La prueba de WinPE exige evidencia emitida por el ejecutable dentro del invitado: GUI iniciada, entorno WinPE, Secure Boot activo y proveedor BitLocker disponible. La configuración de la VM por sí sola no cuenta como resultado aprobado.

No se ha grabado ni formateado un USB físico. No se ha utilizado una clave BitLocker real ni probado un desbloqueo real. Siguen pendientes la matriz de revocaciones, tres laptops, cinco usuarios, Narrator, alto contraste y la matriz completa de escalado/teclado.

Consulta [VALIDATION.md](VALIDATION.md) y [HARDWARE-TESTING.md](HARDWARE-TESTING.md). Esta versión alpha no declara la beta ni el MVP certificados.
