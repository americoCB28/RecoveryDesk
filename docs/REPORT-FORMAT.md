# Informes JSON v1

El JSON incluye `schema_version`, `app_version`, `session_id`, `started_at`, `technical_details`, `environment`, recuentos de discos/instalaciones, `assessment` y eventos de la aplicación. `backup` es opcional y contiene resultado, bytes y omisiones.

La opción técnica añade `machine`, `selected_installation`, discos, volúmenes y problemas detectados. El respaldo técnico añade rutas y entradas por archivo. `locked` y `encrypted` pueden ser `null`: no se pudo determinar.

`assessment.outcome` es una etiqueta para humanos. El acceso solo se marca confirmado a partir de una selección del usuario. Una sesión sin comprobación mantiene el estado pendiente. Un desbloqueo BitLocker no confirma el inicio de sesión.

Los estados de respaldo son `pending`, `running`, `complete`, `partial`, `failed` y `cancelled`. Una entrada `verified` incluye SHA-256; una omisión incluye un código. Los archivos omitidos nunca se cuentan como verificados.

`secure_boot` es un campo opcional de diagnóstico: `true` o `false` proceden de la variable UEFI leída por el proceso; `null` significa que no se pudo consultar. El informe no certifica otro equipo, un arranque anterior ni la matriz de revocaciones del firmware.

No hay campos para secretos. Los consumidores deben rechazar versiones de esquema desconocidas y tratar todos los textos como datos no confiables.
