# Verificación local de 0.1.0-alpha

Fecha: 2026-11-01 (America/Lima).

## Realizado

- Compilación x64 C++20 con LLVM-MinGW 20261020 / Clang 23.1.4, CMake 4.1.2 y Ninja 1.13.0. El compilador portable está aislado en `.tools`, fuera de la distribución y de Git.
- 29 pruebas automatizadas de comportamiento con archivos sintéticos, incluyendo integridad, reanudación, privacidad, credenciales excluidas e identidad de almacenamiento/USB.
- Una prueba de integración de lectura del adaptador Windows en esta PC: 2 discos, 5 volúmenes y 1 instalación en ejecución. Los campos que requieren permisos se presentan como desconocidos; no se publican identificadores personales en este registro.
- Validación sintáctica de ambos scripts PowerShell sin ejecutarlos y comprobación de los hashes de dependencias vendorizadas.
- Apertura de la interfaz nativa en modo demostración y revisión visual de inicio y guía. La automatización de clics por accesibilidad no fue estable; no se certifica un recorrido UI automatizado completo.
- Empaquetado de la aplicación, guías, scripts, documentación y licencias.

## Pendiente de entorno/laboratorio

- MSVC y ADK/WinPE no están instalados en el entorno de trabajo. El binario LLVM-MinGW de esta entrega solo está verificado en Windows completo y bloquea el constructor WinPE. La compilación MSVC habilita ese constructor, pero no supone certificación automática.
- No se ha generado una imagen WinPE ni grabado/formateado ningún USB real.
- No se ha utilizado una clave BitLocker real ni probado el desbloqueo de un volumen real.
- Faltan arranque Secure Boot, revocaciones de firmware, tres laptops y la prueba con cinco usuarios.
- Faltan Narrator, alto contraste y la matriz completa de escalado/teclado.
- Los workflows se incluyen como código; su ejecución en GitHub requiere publicar el repositorio. No se ha creado un repositorio remoto ni una release pública.

Consulta [VALIDATION.md](VALIDATION.md) para los criterios de aceptación que siguen pendientes. Esta entrega no declara el MVP certificado ni la beta pública completada.
