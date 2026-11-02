# RecoveryDesk

**Recuperación guiada, diagnóstico y protección de archivos para Windows.**

Aplicación nativa C++20/Win32, local, sin cuentas de servicio ni telemetría. Una misma interfaz ofrece un asistente personal y una vista técnica en Windows y en un entorno WinPE construido localmente.

> Estado: **0.1.0-alpha**. No es una versión certificada para producción. La validación de arranque con Secure Boot, la matriz de hardware y las sesiones con usuarios requieren el laboratorio descrito en [docs/VALIDATION.md](docs/VALIDATION.md). No se distribuye una imagen de Microsoft.

Código público: [americoCB28/RecoveryDesk](https://github.com/americoCB28/RecoveryDesk). [Descargar alpha para Windows x64](https://github.com/americoCB28/RecoveryDesk/releases/tag/v0.1.0-alpha.1). La CI verifica la compilación MSVC, las pruebas, el análisis estático y las importaciones reales del ejecutable. Las releases se preparan como borrador antes de publicar sus binarios.

## Funciones implementadas

- Seis pantallas: inicio, detección, opciones oficiales de acceso, respaldo, preparación USB y resultado/informe.
- Detección mediante APIs Win32 de discos, volúmenes, instalaciones accesibles y permisos. No depende de `C:` y excluye el Windows PE en ejecución del inventario de instalaciones recuperables.
- Estado BitLocker mediante WMI local. Los valores desconocidos no se representan como “sin cifrar”. Desbloqueo con una clave legítima, sin argumentos de comandos ni almacenamiento de la clave.
- Guías versionadas en español para contraseña Microsoft/local, PIN, BitLocker y cuentas corporativas. Autenticación exclusivamente en los portales oficiales, mediante el navegador del usuario.
- Copia selectiva de una carpeta a otra unidad física, SHA-256, detección de conflictos y manifiesto de reanudación. Los archivos EFS, enlaces y archivos no descargados se omiten.
- Informes JSON y HTML autocontenidos. Exportación anonimizada por defecto y detalles técnicos opcionales.
- Constructor integrado: comprueba ADK/WinPE, prepara una imagen local y permite revisar/confirmar el USB antes de invocar MakeWinPEMedia con `/bootex`.
- Modo de demostración `RecoveryDesk.exe --demo`, con datos identificados como simulados y operaciones sobre discos deshabilitadas.

No obtiene contraseñas originales, edita SAM, evade cifrado, modifica políticas de seguridad ni repara automáticamente el arranque. Una guía completada no se registra como acceso recuperado.

## Compilar en Windows

Requisitos de desarrollo: Visual Studio 2022 con **Desktop development with C++**, Windows SDK, CMake 3.24 o posterior (3.25 para los presets) y Git. No hace falta ADK para compilar la app o ejecutar las pruebas.

```powershell
cmake --preset msvc-release
cmake --build --preset release
ctest --preset release
```

Ejecutable: `build/msvc/Release/RecoveryDesk.exe`. Conserva junto a él las carpetas `data`, `boot` y las licencias copiadas automáticamente.

```powershell
cmake --install build/msvc --config Release --prefix dist/RecoveryDesk
cpack --config build/msvc/CPackConfig.cmake -C Release
```

La compilación MSVC utiliza el CRT estático `/MT`; no usa MFC, ATL, CLR, Electron ni WebView2. Hay soporte de compilación secundario para LLVM-MinGW x64, utilizado para la verificación local cuando MSVC no está instalado. Ese binario bloquea la construcción de WinPE porque sus dependencias UCRT no se han validado en PE. Para preparar medios, utiliza una compilación MSVC y el ADK elegido; su aceptación física sigue pendiente.

El script `scripts/Install-Toolchain.ps1 -ProjectRoot <carpeta>` instala las herramientas Microsoft desde una consola elevada, verificando las firmas de los instaladores. `scripts/Complete-WinPE.ps1` compila con MSVC, ejecuta las pruebas, revisa las dependencias y genera una ISO de laboratorio. Estas tareas se ejecutan por solicitud del operador y no forman parte del arranque normal de la aplicación.

## Uso

1. Abre la aplicación en Windows. Puedes consultar las guías con permisos normales.
2. Para diagnosticar, elige **Detectar equipo**. El encabezado identifica el equipo y la instalación seleccionada. Algunos datos necesitan administrador.
3. En **Opciones de acceso**, selecciona lo que muestra la laptop afectada y sigue la documentación oficial.
4. Para respaldar, selecciona una carpeta accesible y una carpeta dedicada en **otro disco físico**. La primera copia requiere un destino vacío.
5. Comprueba el acceso en la laptop y registra el resultado. Exporta el informe si lo necesitas.

Para revisar la interfaz sin examinar el equipo:

```powershell
.\RecoveryDesk.exe --demo
```

## Crear el USB

Necesitas otra PC donde puedas iniciar Windows como administrador, los componentes oficiales **ADK Deployment Tools + Windows PE Add-on amd64/en-us**, varios GB libres y un USB dedicado. RecoveryDesk no descarga el ADK ni acepta sus licencias por ti.

1. Instala un ADK actualizado con soporte `/bootex`; consulta las [instrucciones de Microsoft](https://learn.microsoft.com/en-us/windows-hardware/get-started/adk-install).
2. Ejecuta RecoveryDesk como administrador y detecta el USB.
3. En **Preparar USB**, crea una imagen local dentro de una carpeta de trabajo nueva.
4. Selecciona el USB y revisa modelo, tamaño, número de disco, identidad y contenido visible.
5. Confirma el borrado. El MVP acepta un USB con una sola partición existente y accesible de hasta 32 GB. Rechaza discos internos, de sistema, con instalaciones Windows detectadas y las unidades que contienen la app, la imagen de trabajo o las carpetas de respaldo seleccionadas en la sesión.
6. Espera la comprobación de archivos y hash del WIM. Después valida el arranque en la laptop con Secure Boot.

Durante DISM y la grabación no se fuerza la cancelación. Cerrar la ventana solicita esperar el fin de la etapa. El firmware debe confiar en UEFI CA 2023; la app no modifica certificados ni desactiva Secure Boot. Véase [la creación de medios de Microsoft](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/winpe-create-usb-bootable-drive?view=windows-11).

Para validar, `scripts/prepare_secureboot_vm.py` crea una VM nueva y aislada que confía exclusivamente en Windows UEFI CA 2023. `scripts/Run-SecureBootValidation.ps1` arranca una ISO de validación y exige evidencia nativa por puerto serie de WinPE, Secure Boot activo, GUI iniciada y proveedor BitLocker disponible. No cambia el firmware del equipo anfitrión. Las pruebas físicas siguen el [protocolo de hardware](docs/HARDWARE-TESTING.md).

## Límites de los respaldos

- Copia archivos y directorios seleccionados, no imágenes completas del sistema ni ACL, ADS, flujos EFS o estados de aplicaciones.
- No utiliza VSS: los archivos abiertos para escritura se omiten. Las omisiones producen un resultado parcial o fallido.
- No atraviesa junctions, enlaces simbólicos, hardlinks ni marcadores de OneDrive. No descarga contenido cloud.
- Excluye directorios Windows y almacenes conocidos de credenciales (DPAPI, Vault, perfiles de navegador, SSH y configuraciones cloud). No inspecciona el contenido de documentos: las carpetas seleccionadas pueden contener información sensible que debes custodiar.
- Reanudación: elige las mismas carpetas. Se vuelven a verificar origen y destino; una copia modificada fuera del respaldo se conserva y se informa como conflicto.
- El archivo `.recoverydesk-manifest.json` contiene rutas relativas y hashes; es información local del respaldo y no un informe anonimizado.
- La comprobación previa de espacio es conservadora y puede pedir espacio adicional al reanudar una copia grande.
- Las carpetas vacías y los archivos borrados del origen no se sincronizan; no se borran archivos de destino.
- La lectura desde Windows no equivale a un bloqueo forense de escritura del sistema operativo.

## Privacidad y seguridad

Los datos permanecen locales. No se guardan contraseñas, tokens ni claves BitLocker. Un informe técnico puede revelar nombres de archivo, rutas e identificadores y requiere una elección explícita. Los registros del constructor contienen rutas de trabajo del ADK, no claves de recuperación.

El software se ejecuta con los permisos de su proceso; no es un mecanismo para demostrar la propiedad de un dispositivo. Úsalo exclusivamente en equipos propios o con autorización. Consulta [SECURITY.md](SECURITY.md) para comunicar vulnerabilidades sin publicar datos personales.

## Arquitectura y contribuciones

- `include/recoverydesk`: contratos versionados y API interna.
- `src`: evaluación, informes, adaptadores Windows, respaldo y medios.
- `apps`: interfaz Win32 compartida; tareas largas en un worker.
- `data`: guías locales declarativas; no contienen comandos ejecutables.
- `boot`: construcción local con herramientas Microsoft.
- `tests`: pruebas de comportamiento con archivos sintéticos y almacenamiento simulado.

Lee [CONTRIBUTING.md](CONTRIBUTING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) y [docs/VALIDATION.md](docs/VALIDATION.md). La incorporación de otro entorno debe añadir adaptadores y pruebas sin acoplar las decisiones de recuperación a las pantallas.

## Licencia

Código propio bajo [Apache-2.0](LICENSE); `nlohmann/json` 3.12.0 bajo MIT. Véanse [NOTICE](NOTICE) y el [inventario fijado de dependencias](third_party/manifest.json).

WinPE, ADK y Windows mantienen sus propios términos. La licencia de RecoveryDesk no concede derechos sobre ellos. No se incluyen componentes Microsoft en el repositorio o ZIP de la aplicación. El nombre RecoveryDesk no implica afiliación con Microsoft.
