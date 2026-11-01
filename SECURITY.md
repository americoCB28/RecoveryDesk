# Política de seguridad

RecoveryDesk 0.1 es una alpha. No hay una versión certificada para uso en equipos críticos.

## Reportar una vulnerabilidad

Cuando el proyecto esté publicado en GitHub, utiliza **Security > Advisories > Report a vulnerability** si está habilitado. No publiques detalles de explotación, claves o archivos de usuarios en issues. Si el canal privado todavía no está configurado, contacta primero al mantenedor a través del perfil del repositorio y solicita un canal privado; este repositorio no inventa una dirección de contacto.

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

La CI genera artefactos y checksums. El flujo de release añade inventario y attestations de procedencia cuando el repositorio y los permisos de GitHub lo permiten. Authenticode no está configurado: no se incluye ni inventa un certificado de firma.
