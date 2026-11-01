# Arquitectura

## Decisiones

**ADR-001 — C++20/Win32 compartido.** Un ejecutable x64 con CRT estático y controles nativos. Windows y WinPE usan el mismo flujo; el constructor de medios solo funciona en Windows completo. No se asume un navegador o runtime web en WinPE.

**ADR-002 — Monolito modular local.** `DeviceSnapshot`, `RecoveryAssessment`, `BackupJob` y `SessionReport` separan evidencia, recomendación, copia y resultado. El núcleo no recibe HWND. Los adaptadores Windows consultan APIs de almacenamiento y WMI. El adaptador de medios utiliza scripts propios con parámetros estructurados y herramientas ADK.

**ADR-003 — Incertidumbre explícita.** `locked` y `encrypted` son opcionales. Un proveedor ausente no equivale a disco descifrado. El tipo de cuenta lo selecciona el usuario según la pantalla del equipo afectado; no se infiere de perfiles offline.

**ADR-004 — Escritura acotada.** RecoveryDesk no edita cuentas ni el Windows examinado. El respaldo escribe en un destino independiente, el informe en una carpeta elegida y el constructor en su área de trabajo/USB confirmado. El desbloqueo oficial de BitLocker cambia el estado de acceso de la sesión sin desactivar su protección.

**ADR-005 — Privacidad por lista de campos permitidos.** La exportación personal incluye resultado y recuentos. La técnica añade rutas y evidencia solo tras elección explícita. El HTML escapa contenido y bloquea scripts mediante CSP. Ningún contrato tiene campos para contraseñas o claves.

## Flujo de operaciones

UI -> worker -> adaptador/núcleo -> mensajes de progreso -> UI

Solo el hilo principal modifica controles y la sesión visible. La ventana no se destruye mientras haya una operación. El worker comunica resultados estructurados; no escribe en los controles. Descubrimiento y respaldo admiten cancelación; ADK termina su etapa antes de cerrar.

## Respaldo y reanudación

Se comprueba que las dos carpetas existan, no atraviesen reparse points, no estén anidadas y pertenezcan a discos físicos distintos identificados. Red, RAID y múltiples extents quedan fuera del MVP.

La enumeración omite EFS, placeholders y enlaces. El archivo origen se abre sin compartir escritura; se verifica tamaño/fecha/identidad y se calcula SHA-256. Se escribe un temporal independiente, se vacían buffers y se vuelve a calcular SHA-256. La copia se confirma únicamente al coincidir los hashes. El manifiesto se reemplaza después de cada archivo, manteniendo entradas verificadas no revisitadas al reanudar.

Se rechazan archivos ajenos o modificados en destino. Un archivo verificado que cambió en origen puede reemplazarse si la copia previa todavía coincide con su manifiesto. No se sincronizan borrados ni metadatos completos de Windows.

## Contratos

`schema_version = 1`. JSON UTF-8; GUID de volumen como identidad de volumen e identidad de disco obtenida de almacenamiento. No se usa la letra de unidad como identidad persistente. La UI puede mostrar una letra como etiqueta.

El JSON de informes es de exportación; no existe importación de informes ni API de red. El manifiesto local de respaldo se valida para versión y origen; sus rutas no se utilizan para enumerar archivos, que siempre provienen del origen seleccionado.

## Escalabilidad

Añadir módulos compilados para nuevos diagnósticos y formatos mediante los contratos existentes. Ampliar idiomas con catálogos revisados y pruebas. La gestión de casos, sincronización, ARM64 y extensiones de terceros requieren una propuesta posterior; no hay infraestructura cloud preventiva.

## Dependencias y distribución

Biblioteca vendorizada: nlohmann/json 3.12.0 (MIT). APIs de Windows: almacenamiento, CNG SHA-256, COM/WMI, Shell y controles comunes. Los componentes PE se obtienen del ADK local y deben tener la misma versión y arquitectura. El ADK no está incluido ni relicenciado.
