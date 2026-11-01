# Contribuir a RecoveryDesk

1. Abre una propuesta con el problema observable y los casos de aceptación. No adjuntes claves, contraseñas, SAM, datos de clientes o imágenes Windows.
2. Compila y ejecuta las pruebas siguiendo README. Usa archivos sintéticos en carpetas temporales para las pruebas de respaldo.
3. Conserva la separación entre los modelos, la evaluación, los adaptadores y Win32 UI. Los errores deben incluir un código estable, una explicación y un siguiente paso.
4. Añade pruebas de comportamiento para cambios de almacenamiento, integridad, privacidad o medios. No ejecutes formateadores en CI.
5. Describe el cambio, la validación realizada y lo que requiere hardware. No declares una recuperación o un arranque como correctos sin evidencia.

## Convenciones

- C++20, cuatro espacios, RAII para recursos, Unicode en límites Win32 y UTF-8 en contratos/JSON.
- No registrar secretos; datos personales solo en la exportación técnica explícita.
- Dependencias fijadas y con licencia compatible. Actualiza `third_party/manifest.json`, el inventario y NOTICE.
- El catálogo requiere los seis escenarios, enlaces HTTPS de documentación Microsoft, idioma y fecha de revisión.
- SemVer: 0.x para alphas, 1.0 solo después de la matriz de validación física.
- Los cambios de esquema requieren actualizar versión y documentar compatibilidad. Los consumidores deben rechazar versiones desconocidas.

Las contribuciones se realizan bajo Apache-2.0. No se requiere CLA. No introduzcas datos reales en fixtures.
