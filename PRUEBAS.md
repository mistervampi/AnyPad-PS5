# Procedimiento de prueba en consola

AnyPad PS5 es **experimental** y **nunca ha corrido en una PS5**. Este procedimiento
sirve para probarlo por primera vez en la consola de forma gradual y reversible.
Al seguirlo, la prioridad es conservar el entorno que ya funciona. Una prueba
satisfactoria reduce la incertidumbre, pero no garantiza riesgo cero.

## Tu entorno (según lo indicado el 3 de octubre de 2026)

| Elemento | Versión |
|---|---|
| Consola | PS5 fat, firmware 10.01 |
| Cargador | [ps5-webkit-autoloader](https://github.com/itsPLK/ps5-webkit-autoloader) v0.5.2 (itsPLK), cadena Relapse/Poops en 10.01 |
| kstuff | [kstuff-lite](https://github.com/EchoStretch/kstuff-lite/releases) v1.11 Beta (EchoStretch) |
| ShadowMount | [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus/releases) (drakmor). La última estable es 1.7beta2; la 1.7beta3 es *pre-release* |
| HEN / lanzador de payloads | Payload Manager (del autoloader) u [onionHEN](https://github.com/aydencharles/onionHEN) v0.0.13 |

Lo que dice su documentación y afecta a AnyPad PS5:

- **El autoloader en 10.01** usa un elfldr que solo acepta conexiones desde la
  propia consola, así que `make send` desde el ordenador no funciona.
  AnyPad PS5 se lanza desde **Payload Manager** o desde la página **Payloads** de
  onionHEN.
- **onionHEN incluye su propio kstuff.** Si usas onionHEN, no cargues kstuff-lite
  además: tendrías dos. Para usar el 1.11 con onionHEN, su documentación indica
  ponerlo en `/data/OnionHEN/kstuff.elf`.
- **onionHEN se niega a arrancar si detecta etaHEN.** Usa uno de los dos.
- **ShadowMountPlus avisa** de que montar imágenes puede dar problemas de apagado
  y corrupción de datos en el disco interno. También pausa kstuff durante los
  juegos. Por eso, en las primeras fases **no debe haber ninguna imagen montada**.

## Cómo cargarlo

1. El ELF se llama `AnyPad-PS5-<versión>.elf` y va en la **raíz del USB**
   (`/mnt/usb0/`): Payload Manager solo escanea la raíz, no las carpetas.
   Está también en `dist/`, con su `.sha256` para comprobarlo.
2. En la web de Payload Manager, importa el payload desde el USB y lánzalo
   desde su lista. Importar puede mover el archivo del USB a la consola; si
   quieres conservarlo, elige copiar.

Cada versión nueva lleva otro número en el nombre, así que dos versiones no se
confunden. Si dejas una versión vieja en el USB, bórrala a mano.

## El menú, en la consola y desde otro dispositivo

Al arrancar, la consola muestra una notificación con la dirección del menú y la
combinación de botones. Tres formas de verlo:

1. **En la propia consola:** mantén en tu DualSense **L1 + R1 + panel táctil** (pulsar el
   panel como un botón) durante unos 1,5 s. Se abre el navegador de la consola con el menú.
   *No verificado:* depende de que AnyPad PS5 pueda leer tu mando mientras hay un juego en
   primer plano. La página y el log (`hotkey:`) dicen si lo lee ("Tu mando se lee bien" o el
   código del motivo). Para cambiar la combinación, `/data/anypad/config.ini`:
   `menu_combo = l2+r2+triangle` y `menu_hold_ms = 2000`.
2. **Desde el móvil o el PC** de la misma red: `http://<ip de la PS5>:8095/` (la IP sale en la
   notificación).
3. **Si no hay Bluetooth:** el menú sigue funcionando y muestra el motivo, el log y un botón
   *Reintentar Bluetooth*.

## Reglas durante todas las fases

1. **No toques** firmware, kstuff, ShadowMount, autoloader ni sus
   configuraciones para estas pruebas.
2. **Nada de juegos montados con ShadowMount** hasta la fase 4.
3. **Para AnyPad PS5** con el botón de la página o creando `/data/anypad/stop` por FTP. Se para solo,
   devuelve lo que cambió y muestra "AnyPad PS5: stopped".
4. **Si algo se cuelga**, no repitas la prueba. Guarda los logs y mándamelos.

## Qué recoger después de cada fase

Por FTP:
- `/data/anypad/anypad.log` (y `anypad.log.1` si existe)
- con onionHEN: `/data/OnionHEN/OnionHEN.log`
- con ShadowMountPlus: `/data/shadowmount/debug.log`

## Fase 1: arrancar y parar sin mandos

Objetivo: comprobar que AnyPad PS5 convive con el sistema sin hacer nada.

1. Desde Payload Manager (u onionHEN, Payloads), lanza `AnyPad PS5.elf`.
2. Debe aparecer la notificación `AnyPad PS5 0.3-experimental: 0 paired controller(s)`.
   Abre `http://<ip de la PS5>:8095/` desde el móvil: debe verse la página con
   "en marcha". Si el puerto estuviera ocupado, AnyPad PS5 sigue sin ella.
3. Usa la consola **5 minutos** con tu DualSense: menús, Ajustes > Accesorios y
   un juego instalado normal (no montado).
4. Para AnyPad PS5 con el botón de la página o creando `/data/anypad/stop`.
   Debe aparecer `AnyPad PS5: stopped`.
5. Revisa que el DualSense sigue funcionando y recoge los logs.

En el log de AnyPad PS5 hay que ver, por este orden:
`vpad: main user …`, `controller: … ACL buffers`, `controller address …`, y al
parar `stopping: stop requested` y `stopped`. Si se encendió el page scan, también
`page scan put back`.

**Detente aquí si:** la notificación dice "cannot create virtual pads" o
"Bluetooth not available" (envíame el log), o el DualSense se comporta distinto.

## Fase 2: un mando DualShock 4

1. Lanza AnyPad PS5.
2. Mantén **L1 + R1 + Cuadrado** 3 segundos en el DualSense (o crea
   `/data/anypad/pair`). Aparece "pairing for 60 s".
3. En el DS4, mantén **Share + PS** hasta que la luz parpadee deprisa.
4. Debe aparecer `AnyPad PS5: DualShock 4 connected`.
5. Prueba el mando en los menús. Luego apágalo (mantén PS 10 s) y vuelve a
   encenderlo: debe reconectarse solo.
6. Para AnyPad PS5 y recoge los logs.

Líneas clave: `paired, key stored`, `ready as DualShock 4`,
`device 0x… bound to user …`. Si sale `virtual pad not identified`, mándame el
log: es el punto que más depende del firmware.

## Fase 3: dentro de un juego instalado (sin ShadowMount)

1. Lanza AnyPad PS5, conecta el DS4 y abre un juego instalado normalmente.
2. Juega unos minutos, sal del juego y vuelve a entrar.
3. Para AnyPad PS5 y recoge los logs.

## Fase 4: con ShadowMount y kstuff en pausa

Solo si las fases 1 a 3 salen limpias.

1. Lanza AnyPad PS5 y conecta el DS4.
2. Abre un juego de ShadowMount y espera a que pase su tiempo de pausa de
   kstuff (15 a 25 s por defecto, según su configuración).
3. Comprueba que el DS4 sigue funcionando con kstuff en pausa.
4. Cierra el juego (ShadowMount reactiva kstuff), para AnyPad PS5 y recoge los
   logs, incluido `debug.log` de ShadowMount.

## Fase 5: reposo

1. Lanza AnyPad PS5 con el DS4 conectado y manda la consola a reposo.
2. AnyPad PS5 debe pararse **antes** de dormir: `stopping: rest mode` en el log.
3. Al despertar, AnyPad PS5 **no** sigue en marcha (es lo previsto). Lánzalo de
   nuevo a mano y comprueba que el DS4 se reconecta.

## Registro de pruebas en consola

Cada prueba hecha en una consola, con su resultado, para no perder lo aprendido.

### 3 de octubre de 2026: primera ejecución (0.3.0-experimental)

| | |
|---|---|
| Consola | PS5 fat, firmware 10.01 |
| Cargado desde | USB, con Payload Manager (el ELF estaba en el USB) |
| Fase | 1 (arrancar sin mandos) |
| Resultado | **No arrancó.** AnyPad PS5 se detuvo al empezar |

Lo único que quedó en `/data/padhost/` (así se llamaba entonces) fue el log:

```
[  186863] padhost 0.3-experimental
[  186877] vpad: scePadSetProcessPrivilege failed: 0x80920005
```

**Causa.** `0x80920005` es `SCE_PAD_ERROR_NOT_INITIALIZED` (tabla de errores de
libScePad). `scePadSetProcessPrivilege` se llamaba antes de
`scePadInit`, y el fallo se trataba como fatal. Fue un error de la reescritura de
estabilidad del 3 de octubre: se cambió el orden de las llamadas y se añadió una comprobación
que no hacía falta.

**Corrección (0.3.1).** `scePadInit` primero, comprobado. Luego
`scePadSetProcessPrivilege`, cuyo resultado distinto de 0 solo se registra como aviso.
La prueba `test_vpad` reproduce el comportamiento de la consola con bibliotecas
sustitutas y falla con el orden de 0.3.0.

**Lo que sí funcionó, y es lo importante.**
- Se paró limpiamente: no quedó `anypad.lock` ni ningún otro archivo; solo el log.
- No llegó a tocar el Bluetooth, tal como está diseñado: sin mandos virtuales no se
  abre el chip.
- La consola, el DualSense, kstuff y ShadowMount no se vieron afectados (según lo
  contado por el usuario).

**Sin confirmar.** La notificación en pantalla debía decir "AnyPad PS5: cannot create
virtual pads on this system - see the log. Stopped". Si dijo otra cosa, hay que
revisar el log, porque este es el único error registrado.

**Siguiente paso.** Repetir la fase 1 con 0.3.1 y enviar el log nuevo
(`/data/anypad/anypad.log`).

### 3 de octubre de 2026: segunda ejecución (0.3.1-experimental)

| | |
|---|---|
| Consola | PS5 fat, firmware 10.01 |
| Cargado desde | USB, con Payload Manager |
| Fase | 1 (arrancar sin mandos) |
| Resultado | **Avanzó más, pero el Bluetooth no respondió.** Se rindió y se detuvo; el menú nunca llegó a arrancar |

Log completo (`/data/anypad/anypad.log`):

```
[ 1032200] AnyPad PS5 0.3.1-experimental
[ 1032209] vpad: main user <user-id>
[ 1032212] usb: controller open beside the system: 40 event reads, 20 ACL reads
[ 1037235] command 0x1005: no reply
[ 1037418] Bluetooth not ready (try 1 of 5)
   ... (lo mismo en los intentos 2, 3 y 4) ...
[ 1070071] command 0x1005: no reply
[ 1070254] Bluetooth not ready (try 5 of 5)
```

**Qué funcionó.** El arreglo de la primera prueba: las comprobaciones de los mandos
virtuales pasaron (`main user <user-id>`). El chip se pudo abrir (`/dev/ugen0.2`, con
todas las lecturas). Al fallar, se detuvo limpiamente: no quedó `anypad.lock`.

**Qué falló.** `Read Buffer Size` (el primer comando HCI) no obtuvo respuesta en 5
aperturas × 3 reintentos. No se sabe todavía por qué (el log de 0.3.1 no decía más).
Hipótesis, ninguna confirmada:
1. Las credenciales elevadas cambian cómo el kernel trata el acceso a `ugen` (otras
   implementaciones no elevan).
2. El firmware 10.01 reparte las funciones o los endpoints del chip de otra forma
   (otras implementaciones están probadas en 11.60).
3. El driver del sistema se queda con las respuestas.

**Fallo de diseño propio.** La página web solo arrancaba después de que el Bluetooth
funcionara, así que, justo cuando algo fallaba, no había menú que lo explicara. Corregido
en 0.3.2: el menú arranca primero y el programa se queda activo en modo reducido.

**Qué hace la 0.3.2 para averiguarlo.**
- Vuelca en el log (`diag:`) qué es el dispositivo USB, cuántas lecturas terminaron y con
  qué estado, cuántos eventos y paquetes se vieron y los primeros eventos en hexadecimal.
- Prueba sola la hipótesis 1: dos intentos con credenciales elevadas y tres después de
  restaurar las originales. El log dice cuál funcionó.

**Siguiente paso.** Lanzar 0.3.2 y enviar `/data/anypad/anypad.log` (especialmente las
líneas `diag:`).

### 3 de octubre de 2026: tercera ejecución (0.3.2-experimental)

| | |
|---|---|
| Consola | PS5 fat, firmware 10.01 |
| Cargado desde | USB, con Payload Manager |
| Resultado | **El menú funciona; el atajo no; el Bluetooth sigue sin responder** |

**El menú.** `http://<console-ip>:8095/api/state` respondió desde el Mac. Lo que decía:
`"bluetooth":"failed"`, el motivo, la combinación `L1 + R1 + panel táctil`, y
`"physical":{"readable":false,"error":"80920008"}`. Funciona el modo degradado: aunque el
Bluetooth falle, AnyPad PS5 sigue activo y explica por qué.

**El atajo no hacía caso.** El log: `hotkey: your pad is not readable (code 80920008)`.
`0x80920008` es `SCE_PAD_ERROR_DEVICE_NO_HANDLE`: `scePadGetHandle` no devuelve ningún
handle del mando del usuario a un payload que no lo ha abierto. Corregido en 0.3.3: se abre
con `scePadOpen` (lo que hace cualquier aplicación; el sistema y el juego también lo
tienen abierto) y se cierra al salir. **Sigue sin verificarse** que esa lectura funcione con
un juego en primer plano.

**El Bluetooth.** Mismo síntoma: `command 0x1005: no reply`, 5 veces. Pero ahora con datos:

```
usb: /dev/ugen0.2 is 0e8d:3603
diag: device 0e8d:3603 class ef/02/01, USB 0201, 64 endpoint-0 bytes
diag: 1 reads finished (0 empty or failed); 0 events and 0 ACL packets seen
diag: 1 reads ended with USB status 0
diag: last command sent: 05 10 00
```

- Es el MediaTek `0e8d:3603` que describen otros proyectos. No es otro chip.
- **Hipótesis 1 descartada:** con las credenciales originales (intentos 3 a 5) pasa
  exactamente lo mismo. Las credenciales elevadas no son la causa.
- En 5 s **solo terminó una lectura**, y no contenía ningún evento. Las otras 59 lecturas
  siguen pendientes: no llega nada, ni la respuesta a nuestro comando ni tráfico del
  sistema. El comando (`05 10 00`, Read Buffer Size) se envía sin error USB.
- Quedan las hipótesis 2 y 3 (otra disposición de funciones o endpoints en el 10.01; el
  driver del sistema se queda con todo). La 0.3.3 vuelca la tabla completa de endpoints y
  qué driver del kernel tiene cada interfaz para decidirlo.

**Otro fallo propio hallado al investigar.** El archivo `/data/anypad/anypad.lock` quedó
con `95` tras reiniciar la consola. Los números de proceso empiezan de nuevo en cada
arranque, así que una copia nueva podía creer que ya había otra en marcha. Corregido en
0.3.3 (el bloqueo guarda también la hora de arranque).

**Cosas vistas en la consola (solo lectura), para el que continúe:**
- Con la consola recién arrancada: abiertos 2121 (ftpsrv) y 8084 (Payload Manager);
  cerrados 3232 (**no hay servidor de logs del kernel**, así que AnyPad PS5 tendrá que
  leer `/dev/klog` él mismo para los mandos virtuales), 9020, 9021 y 8095.
- `/data/pldmgr/autoload.txt` (autoload de Payload Manager del usuario): kstuff beta,
  shadowmountplus beta, OnionHEN, ftpsrv, nanodns.
- `/dev`: `ugen0.1`, `ugen0.2`, `ugen1.1`, `ugen2.1`, `ugen2.2`, y también `bt`
  (modo `crw-rw-rw-`, legible por cualquiera), `bluetooth_hid`, `wlanbt`, `usbc`, `usbctl`.
  `/dev/usb/` tiene nodos `0.2.0` a `0.2.15` (los endpoints del chip).
- `/system/common/lib/` tiene `libSceBluetoothHid.sprx`, `libSceHidControl.sprx` y
  `libSceMbus.sprx` (el sistema tiene su propia API Bluetooth HID; es una vía alternativa a
  estudiar si la USB directa no se consigue).

**Siguiente paso.** Lanzar la 0.3.3 y enviar `/data/anypad/anypad.log` (líneas `diag:`,
sobre todo `configuration descriptor`, `cfg[...]` e `interface N is held by driver`).

### 3 de octubre de 2026: cuarta ejecución (0.3.3-experimental)

| | |
|---|---|
| Consola | PS5 fat, firmware 10.01 |
| Resultado | **Se encontró por qué el Bluetooth "no respondía": sí respondía, y la respuesta se descartaba** |

**El hallazgo.** El diagnóstico nuevo (`diag:`) de la 0.3.3 dio, por primera vez, la
tabla de endpoints del chip y qué llegó:

```
diag: the first read to finish was transfer 40 (ACL in, 0x81), 13 bytes in 1 frame(s):
      0e 0b 01 05 10 00 fd 03 f0 08 00 08 00
diag: configuration descriptor, 474 bytes   (volcado completo en tests/fixtures/)
```

Esos 13 bytes son un evento HCI *Command Complete* (`0e`) válido del comando que se
envió (`Read Buffer Size`, `05 10`): estado `00` correcto, paquetes ACL de `fd 03` = 1021
bytes, `08 00` = 8 buffers. Llegó por el endpoint `0x81`, que el código trataba como la
tubería de datos ACL (suposición inicial: eventos `0x82`, ACL `0x81`)
y descartó. **El chip contestó a la primera, todas las veces.**

El descriptor (474 bytes, 5 interfaces) muestra la disposición estándar:

| Función HCI | Interfaz | Eventos (interrupción) | ACL entrante (bulk) | ACL saliente (bulk) |
|---|---|---|---|---|
| A (primera) | 0 | 0x81 (16 bytes) | 0x82 (512) | 0x01 (512); hay otro, 0x02, y una interrupción 0x8f de 2 bytes |
| B (segunda) | 3 | 0x8c | 0x8d | 0x0b; hay otro, 0x0d |

Las interfaces 1 y 4 son de voz (isócronas); la 2 es de otra cosa (interrupción 0x8a/0x0a,
64 bytes). Los `interface N is held by driver ""` vacíos indican que ningún driver del kernel
las reclama. **La función A no tuvo tráfico alguno en 5 s** (con el DualSense conectado),
así que el sistema lleva el DualSense por la función B, y la A está libre.

**Qué falló y qué se cambió en 0.3.4.**
1. Endpoints mal supuestos: ahora salen del descriptor del propio chip, con la regla del primer endpoint de cada tipo.
   *Sin verificar:* que `0x01` y no `0x02` sea el correcto para ACL saliente; solo se usará al
   haber una conexión, y el log lo dirá.
2. La web respondía `{"error":"busy"}` al pulsar el botón de Bluetooth: cada intento bloqueaba
   ~5 s sin atender la web. Se atiende ahora mientras se espera al chip.
3. El atajo seguía sin leer el mando: `scePadOpen` devolvió `0x809b0081`
   (`SCE_DEVICE_SERVICE_ERROR_USER_NOT_LOGIN`): el usuario elegido (el de primer plano) no
   tenía sesión iniciada. Ahora se elige de la lista de usuarios con sesión.

**Siguiente paso.** Lanzar la 0.3.4. Debería verse en el log:
`usb: HCI function 0: interface 0, events 0x81 …`, y `Bluetooth ready after 1 try`
(o `controller: 8 ACL buffers of 1021 bytes`). Si es así, probar el emparejado desde el menú
con un mando en modo emparejamiento.

## Más adelante

Solo después de varias sesiones limpias de las fases 1 a 5:
- probar otros mandos (Xbox, Switch Pro, 8BitDo);

Hasta entonces, se lanza a mano.


## Prueba 5 (0.3.4, 3 oct 2026)

- Bluetooth: **listo tras 1 intento** (`controller address xx:xx:xx:xx:xx:xx`, 8 buffers ACL, 8 LE). La detección de endpoints por descriptor funciona.
- Atajo de mando: sigue sin funcionar. `scePadOpen` devuelve `0x809b0081` (USER_NOT_LOGIN) incluso para el único usuario (`<user-id>`): un payload sin contexto de aplicación no puede leer el DualSense. Alternativa: acceso en Contenido multimedia (0.4.0).
- Emparejar con un DS4 en modo emparejamiento: el log solo muestra `pairing: searching for 60 s` y nada más: ni inquiry ni resultados. Sin datos para saber en qué fase se queda; la 0.4.0 añade trazas de la búsqueda.

## Prueba 6 (0.4.0)

Mirar tras ejecutar: `launcher: installed ANYP00001 ... -> 0x0`, si aparece el icono (puede requerir volver al menú de inicio o reiniciar la sesión), y las líneas `inquiry:` al pulsar Emparejar con el DS4 (Share+PS).

Resultado: el icono se instaló. Emparejar: `inquiry` encuentra el DS4 (xx:xx:xx:xx:xx:xx, clase 002508), conecta, "paired, key stored", "link encrypted"; a continuación `usb: ACL out still busy` y "channel 0x1: no connection response" cada 2 s hasta "setup did not finish". Causa probable: el endpoint ACL-out 0x01 elegido por descriptor no es el que usa el chip (hay otro bulk OUT, 0x02).

## Prueba 7 (0.4.1)

Buscar en el log `usb: ACL out 0x1 never finished; switching to 0x2` y `first ACL packet written on endpoint`, y si el DS4 llega a "ready".

Resultado (3 oct 2026, DS4 `xx:xx:xx:xx:xx:xx`):
- `usb: ACL out 0x1 never finished; switching to 0x2` -> `first ACL packet written on endpoint 0x2` (el estado USB de la 0x1 fue 5). **El ACL-out correcto de este chip es 0x02**, no el primer bulk OUT del descriptor. Tras el cambio: canal 0x1 abierto, SDP (vendor 054c product 09cc), canales HID 0x11 y 0x13, `ready as DualShock 4 in slot 0`, `slot 0: device 0x5030d bound to user <user-id>: 0`. Funciona de punta a punta hasta el vínculo con el usuario.
- La página web de AnyPad muestra TODOS los botones del DS4 al pulsarlos (cruz, triángulo, L1, R1, L2/R2 analógicos, panel táctil, direcciones; grabado con sondeo de /api/state). El **sistema** (menús de la consola) recibe todos los botones.
- **FALLO ABIERTO (resuelto en la prueba 12):** ni los juegos ni un emulador detectan el mando virtual cuando está asignado al usuario del DualSense.
- Hipótesis de entonces (descartadas por la prueba 12): los juegos leen otro dispositivo; faltaba un paso del vínculo; campos de `ScePadData` sin rellenar.
- Diagnóstico propuesto entonces: registrar el valor devuelto por cada `scePadVirtualDeviceInsertData` y volcar un `ScePadData`. Ya no hace falta.

## Prueba 8 (0.4.2): pendiente

Mirar: ¿el DS4 se encuentra siempre? (líneas `inquiry:` / `found gamepad` / `paging it again`), ¿la luz azul llega antes?, y si los juegos ven el mando con el DualSense original apagado (fallo abierto de la prueba 7). El usuario observó que al conectar el mando su MAC sale en el menú pero la luz azul tarda en aparecer.

## Prueba 9 (0.4.1, DualSense apagado): el DS4 controla todo el sistema pero NO los juegos

El usuario apagó el DualSense; con solo el DS4 se mueve por todo el sistema (menús) como con el DualSense, asignado al usuario principal. En un emulador y en juegos el mando **no hace caso, salvo el botón PS** (que lo atiende el propio sistema).
Conclusión: el mando virtual existe y está vinculado en el lado del shell, pero el juego no lo veía con ese usuario. La prueba 12 lo resolvió usando un usuario distinto del del DualSense.
Alternativas consideradas (innecesarias tras la prueba 12): vincular a otro usuario (`bind_user`), repetir el vínculo con el juego en marcha, o hacerlo desde el proceso del menú de la consola (invasivo, descartado por la regla de estabilidad).
Regresión: la 0.4.2/0.4.3 dejaron de encontrar el DS4 (ver prueba 8); la 0.4.1 sí lo encuentra y es la versión de referencia (tag git `baseline-0.4.1-funciona`).

## Prueba 10 (0.4.4): usuario distinto para el mando (opción 1)

Cómo probar: 1) crear un segundo usuario en la consola (Ajustes > Usuarios) e iniciar sesión con los dos; 2) crear por FTP `/data/anypad/bind_user` con el texto `other` (o el id del segundo usuario en hex); 3) ejecutar la 0.4.4; el log debe decir `users: ... bind_user=other` y `bound to user <id del segundo>`; 4) con el diálogo de la consola asignar el DS4 a ese usuario y **lanzar el juego con ese usuario**; 5) ver si el juego recibe el mando. Resultado: pendiente.

## Prueba 11 (0.4.4): "se ha vuelto a corromper" — causa encontrada

Log de 0.4.4 (base 0.4.1): el DS4 aparece en el primer segundo (`found gamepad`) y cada intento de conectar termina con `connection refused (status 0x0b)` = "Connection Already Exists": **el chip todavía tiene abierto un enlace con ese mando de una ejecución anterior** que terminó sin cerrarlo (arranques con `stale lock, taking it over`: Payload Manager relanzó el ELF sobre el anterior o se cortó). Mientras el enlace huérfano exista, el mando no se puede volver a paginar y las búsquedas salen vacías; reiniciar la consola no siempre lo limpia (el chip USB sigue alimentado). Explica el patrón "a veces funciona, a veces no" de 0.4.1–0.4.3: las sesiones buenas venían de una parada limpia (botón Detener de la web).
Consejo inmediato al usuario: **parar siempre con el botón Detener de la web** antes de volver a lanzar el ELF.
Corrección en 0.4.5: ante `status 0x0b` al paginar un mando, el host pide cerrar (Disconnect, motivo 0x13) los handles 0x000–0x0FF de ESA controladora (la función 0, que el sistema no usa) como mucho una vez por minuto, y el mando se vuelve a encontrar a continuación. Test con simulador (`test_stale_link`).
Las regresiones atribuidas a 0.4.2/0.4.3 probablemente eran este mismo efecto (ese par de sesiones empezaban con `stale lock`). El código de 0.4.2/0.4.3 se ha retirado de HEAD (reintentos de paginación, `es.mps`, estadísticas) para volver a la base 0.4.1; quedan en el historial git.

## Prueba 12 (0.4.1, usuario nuevo): **FUNCIONA en juegos**

El usuario creó un usuario nuevo en la consola, al detectar el DS4 la consola preguntó qué usuario lo usa y se lo asignó a ese usuario nuevo: **el mando funciona** en los juegos (el usuario juega con su usuario principal y pulsa PS un momento: se usa el mando del usuario creado). Con el usuario principal (el del DualSense) solo funcionaba en el sistema, no en juegos ni emuladores. Es decir, la causa del "los juegos no lo ven" era el reparto de mandos por usuario, no un fallo de AnyPad: no hace falta inyección ni la opción `bind_user`. Requisito de uso, ahora en el aviso de arranque, la web y el README: crear un usuario nuevo y asignarle el mando.

## Prueba 13 (0.5.0): "vuelve a pasar": no detecta el mando

Log: tras `Bluetooth ready`, al pulsar Emparejar el chip contestó `status 0x0c` (comando no permitido: ya había una búsqueda en marcha, dejada por una ejecución anterior cortada; arranque con `stale lock, taking it over`) a ~25 órdenes de búsqueda seguidas; después `finished (status 0x0a/0xe3/0xee)` (eventos dañados) y ningún resultado hasta 45 s más tarde. Causa: estado que una ejecución cortada deja en el chip (búsqueda y/o enlaces) más la pérdida del aviso de fin de búsqueda, que dejaba `inquiring=1` sin límite. No es un fallo de una versión concreta: les pasa a todas cuando la anterior no se paró limpia.
Arreglo en 0.5.1: cancelar al arrancar, no repetir ante 0x0c, vigilante de 14 s, validar la longitud del evento de fin de búsqueda, cancelar al parar.
