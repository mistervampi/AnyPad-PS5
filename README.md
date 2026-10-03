<p align="center"><img src="assets/logo.png" width="180" alt="AnyPad PS5"></p>

# AnyPad PS5 — Free your PS5 from the DualSense
# AnyPad PS5 — Libera tu PS5 del DualSense

**[English](#english) · [Español](#español)**

---

<a name="english"></a>
# 🇬🇧 English

> ## ⚠️ MANDATORY: create a NEW console user
> **AnyPad PS5 does not work if the controller is assigned to the same user as your DualSense.**
> You must **create a new user on the PS5** and assign the controller to that user when the console
> asks. Play with your main user and **press the PS button briefly**: the created user's pad is then
> the one used in the game. If the pad stays on the DualSense's user, it only works in the console
> menus and games and emulators will not see it. Details [below](#important-use-a-second-console-user).

> **ALPHA.** Verified on a real PS5 (fat, firmware 10.01) with a **DualShock 4**.
> Controllers from other consoles and brands (Xbox, Nintendo Switch, 8BitDo and more) are
> supported so you can use them on the PS5, but they have not been tested on a console yet. See
> [COMPATIBILITY.md](COMPATIBILITY.md).

## A "jailbreak" for the PS5 controller

A PS5 only accepts its own DualSense (and a few licensed pads). AnyPad PS5 is a
**controller jailbreak**: a resident payload for a jailbroken PS5 that connects other
Bluetooth controllers through the console's own Bluetooth chip and shows them to
the system and to games as **virtual DualSense controllers**. DualShock 4, Xbox, Switch Pro,
8BitDo and any Bluetooth HID gamepad, with no dongle.

It does not modify games, the firmware or the jailbreak. It needs a console that already runs
a payload loader (tested with the WebKit autoloader + Payload Manager, kstuff-lite and ShadowMountPlus).

## What works today

- Pairing from a web page (and from the console's *Media* row), reconnection by switching the pad on.
- **DualShock 4: buttons, sticks, triggers, in the console menus and in games and emulators** (verified).
- A menu in the console's *Media* row ("AnyPad", with the orange pad icon) that opens the web page.
- A log (`/data/anypad/anypad.log`) with the console's firmware and the payloads running beside it.

The full list of controllers, one by one, with their status, is in
[COMPATIBILITY.md](COMPATIBILITY.md).

## IMPORTANT: use a second console user

Games do not take the controller from the user who owns your DualSense, so the pad has to belong to
another user. So:

1. **Create a new user** on the console (Settings → Users).
2. Run AnyPad PS5 and pair the controller.
3. When the console detects the pad and asks which user it is for, **choose the new user**.
4. Play with your main user as usual and, when you want the DS4, **press the PS button briefly**: the
   pad of the created user is then the one used.

With the same user as the DualSense, the pad works in the console menus but games and
emulators do not see it.

## Install and run

1. Download `AnyPad-PS5-<version>-alpha.elf` from the
   [Releases](https://github.com/sinfiltros/AnyPad-PS5/releases) page, or build it (below).
2. Copy it to the root of a USB drive and import it into Payload Manager, or send it to your loader.
3. Launch it. A notification shows the menu address and the reminder about the second user.
4. Open the menu from the *Media* row ("AnyPad") or from any device on your network at
   `http://<console ip>:8095/`.
5. Press **Emparejar** and put the controller in pairing mode:

| Controller | Pairing mode |
|---|---|
| DualShock 4 | Share + PS, until the light bar double-flashes |
| DualSense | Create + PS, until the light flashes |
| Xbox | Turn it on, then hold the pairing button on top |
| Switch Pro | The small button on top, next to USB-C |
| 8BitDo | Its pairing button, or the start combination for the mode you want |
| Others | See the controller's manual |

After that, switching the controller on is enough: it reconnects by itself.

AnyPad PS5 can be added to your payload loader's autoload list like any other payload.

**Always stop AnyPad from the page ("Apagar AnyPad PS5 (avanzado)") before launching it again.**
A payload killed without closing its Bluetooth links can leave a stale link on the chip; this
version closes stale links when it finds one, but a clean stop is still best. The *Salir* button
only leaves the page and keeps AnyPad running.

## Mapping a controller with no profile

The log prints the ids and what the HID descriptor has. To change the mapping, write
`/data/anypad/maps/VVVV_PPPP.map` (see `maps/EXAMPLE.map`) and reconnect the pad.

## The web page

Port **8095**: connected pads live, paired pads, pair, forget, log, stop. There is **no PIN**, by
choice: anyone on the same local network can use it. It only accepts private-network peers, an IPv4
`Host`, a custom header on changes, at most 8 clients and small requests.

## Stability rules the code follows

- One instance (atomic lock); the thread is named `AnyPad PS5`.
- Minimal changes to the shared Bluetooth controller; no reset; page scan restored on exit.
- It only closes or deletes its own links and virtual pads.
- Raised credentials are checked and restored; it stops before touching Bluetooth if they fail.
- Everything bounded: tries, parsers, log rotation (1 MB), atomic storage.
- Stops by itself when the console starts going to rest mode.
- Tested off the console with simulators (including packet loss), 200 000 malformed inputs, and the
  address and undefined-behaviour sanitizers. That lowers the risk; it proves nothing about your console.

## Build and test

```
make test                                  # unit tests + simulation, on a Mac or Linux PC
make fuzz                                  # malformed input under the sanitizers
tools/build-elf-docker.sh /path/to/ps5-payload-sdk   # the ELF, without installing anything
```

The ELF is `dist/AnyPad-PS5-<version>-alpha.elf` (version in `src/version.h`).

| Path | Purpose |
|---|---|
| `src/hci_usb.c` | The Bluetooth chip, shared with the system's driver |
| `src/host.c` | Pairing, reconnection, L2CAP, SDP, HID, retries, stale-link cleanup |
| `src/le.c`, `src/smp_crypto.c` | Bluetooth LE: pairing, GATT, HID over GATT |
| `src/profiles.c`, `src/generic.c` | Controller profiles and the generic HID mapping |
| `src/ps5_vpad.c` | The virtual DualSense bound to a console user |
| `src/web.c`, `web/index.html` | The web page and its API |
| `src/launcher.c`, `src/ps5_apps.c` | The "AnyPad" entry in the *Media* row |
| `tests/` | Tests and the simulators |

Test log of the console trials: [PRUEBAS.md](PRUEBAS.md) (Spanish). Version history: [CHANGELOG.md](CHANGELOG.md).

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE).

## Contact

anypadps5@proton.me


---

<a name="español"></a>
# 🇪🇸 Español

> ## ⚠️ OBLIGATORIO: crea un usuario NUEVO en la consola
> **AnyPad PS5 no funciona si el mando se asigna al mismo usuario que tu DualSense.**
> Debes **crear un usuario nuevo en la PS5** y asignarle el mando cuando la consola lo pregunte. Juega
> con tu usuario principal y **pulsa el botón PS un momento**: entonces se usa el mando del usuario creado.
> Si el mando se queda en el usuario del DualSense, solo funciona en los menús de la consola y los juegos
> y emuladores no lo ven. Detalles [más abajo](#importante-usa-un-segundo-usuario-de-la-consola).

> **ALPHA.** Verificado en una PS5 real (fat, firmware 10.01) con un **DualShock 4**.
> Los mandos de otras consolas y marcas (Xbox, Nintendo Switch, 8BitDo y más) están soportados
> para que puedas usarlos en la PS5, pero aún no se han probado en consola. Ver
> [COMPATIBILITY.md](COMPATIBILITY.md).

## Un «jailbreak» para el mando de la PS5

Una PS5 solo acepta su DualSense (y algunos mandos con licencia). AnyPad PS5 es un
**jailbreak de mandos**: un payload residente para una PS5 con jailbreak que conecta otros
mandos Bluetooth a través del chip Bluetooth de la propia consola y se los presenta al
sistema y a los juegos como **DualSense virtuales**. DualShock 4, Xbox, Switch Pro, 8BitDo y
cualquier mando HID Bluetooth, sin adaptadores.

No modifica juegos, ni el firmware, ni el jailbreak. Necesita una consola que ya ejecute un
cargador de payloads (probado con el WebKit autoloader + Payload Manager, kstuff-lite y ShadowMountPlus).

## Qué funciona hoy

- Emparejado desde una página web (y desde la fila *Contenido multimedia* de la consola) y reconexión al encender el mando.
- **DualShock 4: botones, sticks y gatillos, en los menús de la consola y en juegos y emuladores** (verificado).
- Una entrada en *Contenido multimedia* («AnyPad», con el icono del mando naranja) que abre la página web.
- Un log (`/data/anypad/anypad.log`) con el firmware de la consola y los payloads que corren a su lado.

La lista completa de mandos, uno por uno y con su estado, está en [COMPATIBILITY.md](COMPATIBILITY.md).

## IMPORTANTE: usa un segundo usuario de la consola

Los juegos no toman el mando del usuario al que pertenece tu DualSense, así que el mando tiene que
pertenecer a otro usuario. Por eso:

1. **Crea un usuario nuevo** en la consola (Ajustes → Usuarios).
2. Ejecuta AnyPad PS5 y empareja el mando.
3. Cuando la consola detecte el mando y pregunte para qué usuario es, **elige el usuario nuevo**.
4. Juega con tu usuario principal como siempre y, cuando quieras usar el DS4, **pulsa el botón PS un
   momento**: se usa el mando del usuario creado.

Con el mismo usuario del DualSense, el mando funciona en los menús de la consola pero los
juegos y emuladores no lo ven.

## Instalar y ejecutar

1. Descarga `AnyPad-PS5-<versión>-alpha.elf` desde
   [Releases](https://github.com/sinfiltros/AnyPad-PS5/releases), o compílalo (abajo).
2. Cópialo a la raíz de un USB e impórtalo en Payload Manager, o envíalo a tu cargador.
3. Lánzalo. Una notificación muestra la dirección del menú y el aviso del segundo usuario.
4. Abre el menú desde *Contenido multimedia* («AnyPad») o desde cualquier dispositivo de tu red en
   `http://<ip de la consola>:8095/`.
5. Pulsa **Emparejar** y pon el mando en modo emparejamiento:

| Mando | Modo emparejamiento |
|---|---|
| DualShock 4 | Share + PS, hasta que la barra de luz parpadee doble |
| DualSense | Create + PS, hasta que la luz parpadee |
| Xbox | Enciéndelo y mantén el botón de emparejado de arriba |
| Switch Pro | El botón pequeño de arriba, junto al USB-C |
| 8BitDo | Su botón de emparejado o la combinación de inicio del modo que quieras |
| Otros | Mira el manual del mando |

Después basta con encender el mando: se reconecta solo.

AnyPad PS5 se puede añadir a la lista de autoload de tu cargador de payloads, como cualquier otro.

**Para AnyPad siempre desde la página («Apagar AnyPad PS5 (avanzado)») antes de volver a lanzarlo.**
Un payload cortado sin cerrar sus enlaces Bluetooth puede dejar un enlace huérfano en el chip; esta
versión los cierra cuando los encuentra, pero lo mejor es parar limpio. El botón *Salir* solo
abandona la página y deja AnyPad funcionando.

## Mapear un mando sin perfil

El log imprime los ids y lo que trae el descriptor HID. Para cambiar el mapeo, escribe
`/data/anypad/maps/VVVV_PPPP.map` (ver `maps/EXAMPLE.map`) y reconecta el mando.

## La página web

Puerto **8095**: mandos conectados en vivo, emparejados, emparejar, olvidar, log, parar. **No hay PIN**,
por decisión propia: cualquiera de tu red local puede usarla. Solo admite equipos de red privada, un
`Host` IPv4, una cabecera propia en los cambios, como mucho 8 clientes y peticiones pequeñas.

## Reglas de estabilidad que sigue el código

- Una sola instancia (bloqueo atómico); el hilo se llama `AnyPad PS5`.
- Cambios mínimos en el controlador Bluetooth compartido; sin reset; el page scan se restaura al salir.
- Solo cierra o borra sus propios enlaces y mandos virtuales.
- Las credenciales elevadas se comprueban y se restauran; si fallan, se detiene antes de tocar el Bluetooth.
- Todo acotado: intentos, analizadores, rotación del log (1 MB), almacenamiento atómico.
- Se detiene solo cuando la consola empieza a entrar en reposo.
- Probado fuera de la consola con simuladores (con pérdida de paquetes), 200 000 entradas malformadas y los
  sanitizadores de memoria y comportamiento indefinido. Reduce el riesgo; no demuestra nada sobre tu consola.

## Compilar y probar

```
make test                                  # pruebas unitarias + simulación, en Mac o Linux
make fuzz                                  # entradas malformadas bajo los sanitizadores
tools/build-elf-docker.sh /ruta/al/ps5-payload-sdk   # el ELF, sin instalar nada
```

El ELF es `dist/AnyPad-PS5-<versión>-alpha.elf` (versión en `src/version.h`).

Registro de pruebas en consola: [PRUEBAS.md](PRUEBAS.md). Historial de versiones: [CHANGELOG.md](CHANGELOG.md).

## Licencia

GPL-3.0-or-later. Ver [LICENSE](LICENSE).

## Contacto

anypadps5@proton.me
