; Installer for the Resistance 2 native PC port.
; Build it with:  powershell -ExecutionPolicy Bypass -File tools\installer\build_installer.ps1
; The installer holds only the port's own files; the player's game copy is
; located on first run and prepared by Setup.bat.

#define PortName      "Resistance 2 PC Port"
#define PortShort     "Resistance2Port"
#define PortAppId     "{{6C1E6A0B-2F4D-4C55-9A4E-1F0A9B3D7E02}"
#define PortUrl       "https://github.com/miroslavdanutalexandru-alt/Resistance2Port"
#define GameTitle     "Resistance 2 (EU disc, BCES00226)"
; UserDir: the folder under %USERPROFILE%\Saved Games for the settings backup
#define UserDir       "Resistance 2"

#include "port_common.iss"
