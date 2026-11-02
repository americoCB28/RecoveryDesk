# Validación en equipos físicos

Una VM aprobada demuestra ese arranque virtual. No sustituye las pruebas en laptops físicas ni la matriz de revocaciones dbx.

## Preparación

- Utiliza una compilación MSVC cuyo `pe-dependencies.json` indique `static_runtime_check_passed: true`.
- Prepara el medio con el ADK oficial instalado y el asistente de RecoveryDesk, o genera una ISO local mediante `boot/Export-ISO.ps1`. Las imágenes Microsoft no se distribuyen en GitHub.
- Selecciona un USB dedicado y revisa la confirmación de borrado. Conserva los datos del equipo y su clave legítima BitLocker antes de cambiar su medio de arranque; un cambio puede provocar una solicitud de recuperación al volver a Windows.
- Mantén Secure Boot activado. Utiliza el menú de arranque temporal del fabricante. El firmware debe confiar en Windows UEFI CA 2023; si no lo hace, registra el fallo y consulta las actualizaciones oficiales del fabricante.

## Recorrido por equipo

1. Arranca RecoveryDesk desde el medio propio con autorización del propietario.
2. Abre **Detectar equipo** y **Vista técnica**. Debe identificar el entorno WinPE y mostrar el estado de Secure Boot. Un estado desconocido no es una aprobación.
3. Comprueba discos y volúmenes esperados. Una instalación cifrada y bloqueada puede no aparecer como accesible: no lo interpretes como pérdida de Windows.
4. Cuando corresponda, el propietario introduce la clave BitLocker legítima y vuelve a detectar. No copies la clave al informe o al registro de laboratorio.
5. Prueba el respaldo con archivos sintéticos y otra unidad física. Comprueba una copia completa, cancelación/reanudación y rechazo del mismo disco. No pruebes desconexiones con documentos reales.
6. Abre **Resultado e informe** y exporta JSON + HTML anonimizados a una unidad elegida. En el JSON deben constar `environment: "WinPE"`, `discovery_complete: true` y `secure_boot: true` para aprobar ese arranque.
7. Solo marca acceso confirmado si lo comprobaste realmente al volver al Windows instalado. Un volumen desbloqueado no demuestra inicio de sesión.
8. Guarda el resultado de ese equipo y registra versión del binario, ADK y firmware, sin números de serie ni datos del propietario.

## Registro mínimo

| Equipo de laboratorio | Fabricante/modelo | ADK/firmware | Arranque Secure Boot | Diagnóstico/respaldo | Evidencia JSON |
|---|---|---|---|---|---|
| Físico A | Pendiente | Pendiente | Pendiente | Pendiente | Pendiente |
| Físico B | Pendiente | Pendiente | Pendiente | Pendiente | Pendiente |
| Físico C | Pendiente | Pendiente | Pendiente | Pendiente | Pendiente |

Ninguna fila se completa por disponibilidad de scripts, compilación exitosa o arranque de una VM. Los fallos de controladores, confianza de certificados, accesibilidad y revocaciones se registran como resultados del laboratorio.
