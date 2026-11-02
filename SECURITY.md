# Política de seguridad

RecoveryDesk 0.1 es una alpha. No hay una versión certificada para uso en equipos críticos.

## Reportar una vulnerabilidad

Utiliza el [canal privado de vulnerabilidades de GitHub](https://github.com/americoCB28/RecoveryDesk/security/advisories/new), habilitado para este repositorio. No publiques detalles de explotación, claves o archivos de usuarios en issues.

Incluye versión, entorno, pasos con datos sintéticos, impacto y resultado esperado. Redacta números de serie y rutas personales. Prioridad: selección incorrecta de discos, sobrescritura no autorizada, aceptación de copias corruptas, filtración de secretos e inyección de comandos o HTML.

## Modelo de confianza

- Se confía en el sistema donde se ejecuta la app, su administrador, el binario y las herramientas ADK instaladas.
- Los contenidos de discos y nombres de archivo son datos no confiables. No deben convertirse en comandos.
- Las guías son declarativas; se valida su origen oficial. No hay carga de plugins arbitrarios.
- La clave BitLocker se entrega al proveedor WMI local y se borra de los buffers propios al terminar. El proveedor y los controles de Windows administran sus copias internas; no se promete borrado físico de memoria o del pagefile.
- Un administrador local puede modificar la app y el sistema; RecoveryDesk no es una frontera de seguridad frente a ese administrador.
- El respaldo omite enlaces y utiliza archivos temporales, hash y confirmación antes de aceptar una copia. La alpha debe probarse frente a cambios concurrentes de rutas antes de su certificación.
- El consentimiento en la interfaz no demuestra propiedad ni sustituye la autorización del propietario.

## Publicaciones

La CI genera artefactos y checksums. El flujo de release añade inventario y atestaciones de procedencia de GitHub. Verifica un ZIP descargado con `gh attestation verify RecoveryDesk-windows-x64.zip --repo americoCB28/RecoveryDesk` y compara su SHA-256 con `SHA256SUMS.txt`. Authenticode no está configurado: la atestación de compilación no sustituye la firma del ejecutable para SmartScreen.
