# Validación y criterios de publicación

Este documento separa pruebas automatizadas de aceptación física. No marques resultados pendientes como aprobados.

## Automatizado

`ctest --preset release`: guías oficiales, selección de rutas, resultados pendientes, informes anonimizados, HTML malicioso, SHA-256 conocido, claves sintéticas, almacenamiento no verificable, mismo disco, carpetas anidadas, Unicode, reanudación, copia manipulada, cancelación, archivos bloqueados/hardlinks, límites de tamaño, espacio insuficiente, conflicto de destino y cambio de identidad USB.

Los discos de las pruebas son **identidades simuladas** sobre archivos sintéticos en una carpeta temporal única. No se formatean discos, desbloquean volúmenes reales o modifican usuarios. Las pruebas no demuestran arranque WinPE ni compatibilidad de un controlador.

## Laboratorio obligatorio antes de beta

| Escenario | Criterio | Estado inicial |
|---|---|---|
| VM Windows 11 x64 + vTPM + Secure Boot | Ejecutable MSVC, detección e informe correctos | Pendiente |
| VM con USB/ISO WinPE generado | Inicio automático, almacenamiento y BitLocker disponibles | Pendiente |
| UEFI CA 2023 y estados de revocación | Medio compatible sin desactivar Secure Boot | Pendiente |
| Windows en W:, dos instalaciones, ninguna accesible | Inventario correcto; no confundir WinPE | Pendiente |
| BitLocker bloqueado, clave válida/incorrecta, WMI ausente | No leer contenido bloqueado ni afirmar estado desconocido | Pendiente |
| Sin administrador / UAC cancelado | Guías utilizables y permisos explicados | Pendiente |
| NVMe/SATA y controlador faltante | Ausencia informada sin certificar avería o salud | Pendiente |
| Dos discos físicos y copia grande | Hashes correctos y origen intacto | Pendiente |
| FAT32 con archivo >4 GB | Omisión explícita; copia no declarada completa | Pendiente |
| OneDrive no descargado y EFS | Sin hidratación ni promesa de recuperación | Pendiente |
| Extracción del destino y corte de energía | Progreso verificable, ninguna copia incompleta aceptada | Pendiente |
| USB cambiado tras confirmación | Escritura bloqueada | Pendiente |
| USB interno/sistema/múltiples particiones | Rechazo anterior al formateo | Pendiente |
| Teclado, Narrator, alto contraste, DPI 100/150/200% | Recorrido comprensible y controles accesibles | Pendiente |
| Tres laptops físicas de fabricantes distintos | Inicio USB y escenarios documentados por modelo | Pendiente |
| Cinco usuarios, incluyendo principiantes y técnicos | Cuatro completan el recorrido correcto sin el desarrollador | Pendiente |

## Antes de publicar

- Revisar catálogos y fechas contra fuentes oficiales.
- Construir MSVC Release con `/MT`, revisar importaciones y confirmar que no requiere DLL de desarrollo.
- Construir el medio con ADK actualizado y parches vigentes; revisar sus términos aplicables.
- Habilitar el canal privado de vulnerabilidades en el repositorio público.
- Publicar ZIP de la app, checksums, inventario y procedencia. No subir imágenes de Windows ni datos de pruebas reales.
- Registrar hardware, versión ADK, versión Windows, firmware, resultados y fallos conocidos. No incluir claves, nombres de clientes ni números de serie sin anonimizar.
