// The PC wording of the console's texts: every line of the language files that talks about the PlayStation 2 (its memory cards and
// their slots, its controller ports, the DUALSHOCK 2, the console) replaced with wording for a computer, in each of the game's five
// languages, when the game splits the file into its lines (ReadTextFile, under TWIN_NATIVE). native/PC_TEXT.md lists them all with
// what each is for; native/tools/dump_texts.py dumps the disc's lines. Also the native texts the resolution setting's items use,
// past the code file's own lines
#include "ui/pctext.h"

#include "native.h"

#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NativeUi
{
// The lines replaced: the file, the language (its Language folder's name), the game's text number, the disc's line (only a line that
// is this one is replaced: another disc's is left alone and logged) and the PC's. UTF-8 here; the game's files and font are
// Windows-1252, and every character used is one the language's own files have. Glyph codes (\ ^ { } ¦ ¬ < > [ ]) and the save
// code's marks ((xxx), (x)) are kept
const TextOverride g_TextOverrides[] = {
    {CodeTexts, "English", 0x20,
     "please insert an~analog controller~(dualshock@2)~into controller~port 1.",
     "please connect~a controller~to the computer."},
    {CodeTexts, "English", 0x26,
     "use the directional buttons~or left analog stick~on the analog controller~(dualshock@2)~in controller port 1~to position the screen.",
     "use the directional buttons~or left stick~on the controller~to position the screen."},
    {CodeTexts, "English", 0x27,
     "checking memory card (ps2)~in memory card slot 1.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "checking save data.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x28,
     "formatting memory card (ps2)~in memory card slot 1.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "preparing save storage.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x29,
     "checking memory card (ps2)~in memory card slot 1.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "checking save data.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x2A,
     "saving data.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "saving game.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x2B,
     "saving data.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "saving game.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x2C,
     "loading data.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "loading game.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x2D,
     "checking memory card (ps2)~in memory card slot 1.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "checking save data.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x2E,
     "saving data.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "saving game.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x2F,
     "loading data.~~do not remove~memory card (ps2),~controller, reset, or~switch-off the console.",
     "loading game.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x30,
     "no save file on the~memory card (ps2)~in memory card slot 1.~~Would you like to create a~(xxx)~save file?",
     "no saved games found~on this computer.~~Would you like to create a~(xxx)~save game?"},
    {CodeTexts, "English", 0x31,
     "no memory card (ps2)~in memory card slot 1.",
     "save storage is~not available~on this computer."},
    {CodeTexts, "English", 0x32,
     "memory card (ps2)~in memory card slot 1~is unformatted.~~format memory card (ps2)?",
     "save storage~on this computer~is not ready.~~prepare save storage?"},
    {CodeTexts, "English", 0x33,
     "no (xxx)~save data present~on memory card (ps2)~in memory card slot 1.",
     "no (xxx)~saved games found~on this computer."},
    {CodeTexts, "English", 0x34,
     "insufficient free space~on memory card (ps2)~in memory card slot 1.~~(xxx)~requires (x)kb of free space~to save data.",
     "your computer doesn't have~enough free space~to save games.~~(xxx)~requires (x)kb of free space."},
    {CodeTexts, "English", 0x38,
     "format failed!~~check memory card (ps2)~in memory card slot 1,~and please try again.",
     "preparation failed!~~check your computer's~storage and please~try again."},
    {CodeTexts, "English", 0x39,
     "save failed!~~check memory card (ps2)~in memory card slot 1,~and please try again.",
     "save failed!~~check your computer's~storage and please~try again."},
    {CodeTexts, "English", 0x3A,
     "load failed!~~check memory card (ps2)~in memory card slot 1,~and please try again.",
     "the saved game is damaged~and cannot be used."},
    {CodeTexts, "English", 0x3E,
     "format",
     "prepare"},
    {CodeTexts, "English", 0x42,
     "do not remove~memory card (ps2),~in memory card slot 1~controller, reset, or~switch-off the console~when the autosave indicator~(below) is present.",
     "please don't turn~off your computer~when the autosave indicator~(below) is present."},
    {CodeTexts, "English", 0x43,
     "autosaving data.~~do not remove~memory card (ps2),~in memory card slot 1~controller, reset, or~switch-off the console.",
     "autosaving data.~~please don't turn~off your computer."},
    {CodeTexts, "English", 0x5F,
     "do you really wish to~format memory card (ps2)~in memory card slot 1?",
     "do you really wish to~prepare save storage~on this computer?"},
    {CodeTexts, "English", 0xB4,
     "format successful",
     "preparation successful"},
    {CodeTexts, "English", 0xB7,
     "no memory card (ps2)~in memory card slot 1.~~(xxx)~requires (x)kb of free space~to save data.",
     "save storage is~not available~on this computer.~~(xxx)~requires (x)kb of free space~to save data."},
    {AgentLabTexts, "English", 0x2C,
     "use the left analog stick to drive the ufo",
     "use the left stick to drive the ufo"},
    {AgentLabTexts, "English", 0x2E,
     "use the left analog stick to steer the hoverboard~hold the } button to hover higher and slower",
     "use the left stick to steer the hoverboard~hold the } button to hover higher and slower"},
    {AgentLabTexts, "English", 0x41,
     "use the left analog stick to control the hover scooter.",
     "use the left stick to control the hover scooter."},
    {CodeTexts, "French", 0x20,
     "branchez une manette~analogique (dualshock@2) ~dans le port de~manette n°1.",
     "branchez une manette~sur l'ordinateur."},
    {CodeTexts, "French", 0x26,
     "utiliser les touches~directionnelles ou le joystick~analogique gauche sur la~manette analogique~(dualshock@2) branchée sur~le port de manette n°1~pour centrer l'écran.",
     "utiliser les touches~directionnelles ou le joystick~gauche de la manette~pour centrer l'écran."},
    {CodeTexts, "French", 0x27,
     "verification de la memory card (ps2)~dans la fente pour memory card n°1.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "vérification des données~de sauvegarde.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x28,
     "formatage de la memory card (ps2)~dans la fente pour memory card n°1.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "préparation de l'espace~de sauvegarde.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x29,
     "verification de la memory card (ps2)~dans la fente pour memory card n°1.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "vérification des données~de sauvegarde.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x2A,
     "sauvegarde en cours.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "sauvegarde en cours.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x2B,
     "sauvegarde en cours.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "sauvegarde en cours.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x2C,
     "chargement en cours.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "chargement en cours.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x2D,
     "verification de la memory card (ps2)~dans la fente pour memory card n°1.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "vérification des données~de sauvegarde.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x2E,
     "sauvegarde en cours.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "sauvegarde en cours.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x2F,
     "chargement en cours.~~ne pas retirer la~memory card (ps2)~ou la manette, ni redemarrer ou~eteindre la console.",
     "chargement en cours.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x30,
     "pas de sauvegarde sur~la memory card (ps2)~dans la fente pour~memory card n°1.~~Voulez-vous créer un~fichier de sauvegarde~(xxx) ?",
     "pas de sauvegarde~sur l'ordinateur.~~Voulez-vous créer un~fichier de sauvegarde~(xxx) ?"},
    {CodeTexts, "French", 0x31,
     "pas de memory card (ps2)~dans la fente pour memory card n°1.",
     "l'espace de sauvegarde~de l'ordinateur~est inaccessible."},
    {CodeTexts, "French", 0x32,
     "la memory card (ps2)~dans la fente pour memory card n°1~n'est pas formatee.~~formater la memory card (ps2) ?",
     "l'espace de sauvegarde~de l'ordinateur~n'est pas prêt.~~le préparer ?"},
    {CodeTexts, "French", 0x33,
     "pas de donnees (xxx)~presentes sur~la memory card (ps2)~dans la fente pour memory card n°1.",
     "pas de données (xxx)~présentes~sur l'ordinateur."},
    {CodeTexts, "French", 0x34,
     "espace insuffisant~sur la memory card (ps2)~dans la fente pour memory card n°1.~~(xxx)~necessite (x)kb d'espace libre~pour sauvegarder les donnees.",
     "espace insuffisant~sur l'ordinateur.~~(xxx)~nécessite (x)kb d'espace libre~pour sauvegarder les données."},
    {CodeTexts, "French", 0x38,
     "echec du formatage !~~verifier la memory card (ps2)~dans la fente pour memory card n°1~et reessayer.",
     "échec de la préparation !~~vérifier le stockage~de l'ordinateur~et réessayer."},
    {CodeTexts, "French", 0x39,
     "echec de la sauvegarde !~~verifier la memory card (ps2)~dans la fente pour memory card n°1~et reessayer.",
     "échec de la sauvegarde !~~vérifier le stockage~de l'ordinateur~et réessayer."},
    {CodeTexts, "French", 0x3A,
     "echec du chargement !~~verifier la memory card (ps2)~dans la fente pour memory card n°1~et reessayer.",
     "échec du chargement !~~vérifier le stockage~de l'ordinateur~et réessayer."},
    {CodeTexts, "French", 0x3E,
     "formater",
     "préparer"},
    {CodeTexts, "French", 0x42,
     "ne pas retirer la memory~card (ps2) dans la fente~pour memory card n°1,~ou la manette, ni redemarrer~ou eteindre la console quand~l'indicateur d'auto-sauvegarde~(ci-dessous) est present.",
     "ne pas quitter le jeu~ni éteindre l'ordinateur~quand l'indicateur~d'auto-sauvegarde~(ci-dessous) est présent."},
    {CodeTexts, "French", 0x43,
     "auto-sauvegarde en cours.~~ne pas retirer la~memory card (ps2)~dans la fente pour~memory card n°1,~ou la manette, ni redemarrer~ou eteindre la console.",
     "auto-sauvegarde en cours.~~ne pas quitter le jeu~ni éteindre l'ordinateur."},
    {CodeTexts, "French", 0x5F,
     "formater la memory~card (ps2) dans la fente~pour memory card n°1 ?",
     "préparer l'espace~de sauvegarde~de l'ordinateur ?"},
    {CodeTexts, "French", 0xB4,
     "formatage réussi",
     "préparation réussie"},
    {CodeTexts, "French", 0xB7,
     "pas de memory card (ps2)~dans la fente pour~memory card n°1.~~(xxx)~necessite (x)kb d'espace~libre pour sauvegarder~les donnees.",
     "l'espace de sauvegarde~de l'ordinateur~est inaccessible.~~(xxx)~nécessite (x)kb d'espace~libre pour sauvegarder~les données."},
    {AgentLabTexts, "French", 0x2C,
     "Utilise le joystick analogique gauche pour piloter l'ovni",
     "Utilise le joystick gauche pour piloter l'ovni"},
    {AgentLabTexts, "French", 0x2E,
     "utilise le joystick analogique gauche pour diriger l'aéroplanche~maintiens la touche } enfoncée pour monter plus haut et aller moins vite",
     "utilise le joystick gauche pour diriger l'aéroplanche~maintiens la touche } enfoncée pour monter plus haut et aller moins vite"},
    {AgentLabTexts, "French", 0x41,
     "utilise le joystick analogique gauche pour piloter le scooter.",
     "utilise le joystick gauche pour piloter le scooter."},
    {CodeTexts, "German", 0x20,
     "bitte einen analog~controller (dualshock@2) an~controller-anschluss 1~anschließen.",
     "bitte einen~controller an den~computer anschließen."},
    {CodeTexts, "German", 0x26,
     "benutz die richtungstasten ~oder den linken analog-stick ~des analog controllers ~(dualshock@2) in ~controller-anschluss 1, um ~den bildschirm auszurichten.",
     "benutz die richtungstasten ~oder den linken stick ~des controllers, um ~den bildschirm auszurichten."},
    {CodeTexts, "German", 0x27,
     "memory card (ps2)~in memory card-steckplatz 1~wird überprüft.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speicherdaten~werden überprüft.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x28,
     "formatiert memory card (ps2)~in memory card-steckplatz 1.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speicherplatz wird~vorbereitet.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x29,
     "memory card (ps2)~in memory card-steckplatz 1~wird überprüft.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speicherdaten~werden überprüft.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x2A,
     "speichert daten.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speichert daten.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x2B,
     "speichert daten.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speichert daten.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x2C,
     "lädt daten.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht ~zurückstellen oder abschalten.",
     "lädt daten.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x2D,
     "memory card (ps2)~in memory card-steckplatz 1~wird überprüft.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speicherdaten~werden überprüft.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x2E,
     "speichert daten.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "speichert daten.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x2F,
     "lädt daten.~~memory card (ps2) und~controller nicht entfernen~und die konsole nicht ~zurückstellen oder abschalten.",
     "lädt daten.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x30,
     "keine datei auf~der memory card (ps2)~in memory card-steckplatz 1.~~möchtest du eine~(xxx)~-datei erstellen?",
     "keine datei auf~diesem computer.~~möchtest du eine~(xxx)~-datei erstellen?"},
    {CodeTexts, "German", 0x31,
     "keine memory card (ps2)~in memory card-steckplatz 1.",
     "kein speicherplatz~auf diesem computer~verfügbar."},
    {CodeTexts, "German", 0x32,
     "die memory card (ps2)~in memory card-steckplatz 1~ist nicht formatiert.~~möchtest du die~memory card (ps2)~formatieren?",
     "der speicherplatz~auf diesem computer~ist nicht bereit.~~möchtest du ihn~vorbereiten?"},
    {CodeTexts, "German", 0x33,
     "keine gespeicherten~(xxx)-daten auf der~memory card (ps2)~in memory card-steckplatz 1~vorhanden.",
     "keine gespeicherten~(xxx)-daten auf~diesem computer~vorhanden."},
    {CodeTexts, "German", 0x34,
     "ungenügend speicherplatz auf~der memory card (ps2)~in memory card-steckplatz 1.~~(xxx)~benötigt (x) kb freien~Speicherplatz, um Daten zu~speichern.",
     "ungenügend speicherplatz~auf diesem computer.~~(xxx)~benötigt (x) kb freien~Speicherplatz, um Daten zu~speichern."},
    {CodeTexts, "German", 0x38,
     "formatieren gescheitert!~~bitte überprüf die~memory card (ps2)~in memory card-steckplatz 1~und versuch es noch einmal.",
     "vorbereiten gescheitert!~~bitte überprüf den~speicher des computers~und versuch es noch einmal."},
    {CodeTexts, "German", 0x39,
     "speichern gescheitert!~~bitte überprüf die~memory card (ps2)~in memory card-steckplatz 1~und versuch es noch einmal.",
     "speichern gescheitert!~~bitte überprüf den~speicher des computers~und versuch es noch einmal."},
    {CodeTexts, "German", 0x3A,
     "laden gescheitert!~~bitte überprüf die~memory card (ps2)~in memory card-steckplatz 1~und versuch es noch einmal.",
     "laden gescheitert!~~bitte überprüf den~speicher des computers~und versuch es noch einmal."},
    {CodeTexts, "German", 0x3E,
     "formatieren",
     "vorbereiten"},
    {CodeTexts, "German", 0x42,
     "memory card (ps2) in~memory card-steckplatz 1 und~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten,~solange die automatische~speicher-anzeige~(unten) angezeigt wird.",
     "das spiel nicht beenden~und den computer~nicht ausschalten,~solange die automatische~speicher-anzeige~(unten) angezeigt wird."},
    {CodeTexts, "German", 0x43,
     "automatische datenspeicherung.~~memory card (ps2) in~memory card-steckplatz 1 und ~controller nicht entfernen~und die konsole nicht~zurückstellen oder abschalten.",
     "automatische datenspeicherung.~~das spiel nicht beenden~und den computer~nicht ausschalten."},
    {CodeTexts, "German", 0x5F,
     "bist du sicher, dass du die~memory card (ps2) in~memory card-steckplatz 1~formatieren möchtest?",
     "bist du sicher, dass du den~speicherplatz auf~diesem computer~vorbereiten möchtest?"},
    {CodeTexts, "German", 0xB4,
     "formatieren erfolgreich.",
     "vorbereiten erfolgreich."},
    {CodeTexts, "German", 0xB7,
     "keine memory card (ps2)~in memory card-steckplatz 1.~~(xxx)~benötigt (x) kb freien ~speicherplatz, um daten~zu speichern.",
     "kein speicherplatz~auf diesem computer~verfügbar.~~(xxx)~benötigt (x) kb freien ~speicherplatz, um daten~zu speichern."},
    {AgentLabTexts, "German", 0x2C,
     "benutz den linken analog-stick, um das ufo zu steuern.",
     "benutz den linken stick, um das ufo zu steuern."},
    {AgentLabTexts, "German", 0x2E,
     "benutz den linken analog-stick, um das hoverboard zu steuern.~drück die r1-taste, um höher und langsamer zu schweben.",
     "benutz den linken stick, um das hoverboard zu steuern.~drück die }-taste, um höher und langsamer zu schweben."},
    {AgentLabTexts, "German", 0x41,
     "benutz den linken analog-stick, um den hover-roller zu steuern.",
     "benutz den linken stick, um den hover-roller zu steuern."},
    {CodeTexts, "Spanish", 0x20,
     "inserta un ~mando analógico (dualshock@2)~en el puerto de mando 1.",
     "conecta un ~mando al ordenador."},
    {CodeTexts, "Spanish", 0x26,
     "usa los botones de dirección~o el joystick analógico izquierdo~del mando analógico (dualshock@2)~conectado al puerto de mando 1~para centrar la pantalla.",
     "usa los botones de dirección~o el joystick izquierdo~del mando~para centrar la pantalla."},
    {CodeTexts, "Spanish", 0x27,
     "comprobando la memory card (ps2)~de la ranura para memory card 1.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "comprobando los~datos guardados.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x28,
     "formateando la memory card (ps2)~de la ranura para memory card 1.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "preparando el espacio~de guardado.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x29,
     "comprobando la memory card (ps2)~de la ranura para memory card 1.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "comprobando los~datos guardados.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x2A,
     "guardando datos.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "guardando datos.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x2B,
     "guardando datos.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "guardando datos.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x2C,
     "cargando datos.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "cargando datos.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x2D,
     "comprobando la memory card (ps2)~de la ranura para memory card 1.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "comprobando los~datos guardados.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x2E,
     "guardando datos.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "guardando datos.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x2F,
     "cargando datos.~~no extraigas~la memory card (ps2)~ni el mando.~~no reinicies ni~apagues la consola.",
     "cargando datos.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x30,
     "no hay ningún archivo guardado en la~memory card (ps2)~de la ranura para memory card 1.~~¿Quieres crear una~partida~guardada de (xxx)?",
     "no hay ningún archivo guardado~en el ordenador.~~¿Quieres crear una~partida~guardada de (xxx)?"},
    {CodeTexts, "Spanish", 0x31,
     "no hay memory card (ps2)~en la ranura para memory card 1.",
     "no se puede acceder~al espacio de guardado~del ordenador."},
    {CodeTexts, "Spanish", 0x32,
     "la memory card (ps2)~de la ranura para memory card 1~no está formateada.~~¿Deseas formatear~la memory card (ps2)?",
     "el espacio de guardado~del ordenador~no está preparado.~~¿Deseas prepararlo?"},
    {CodeTexts, "Spanish", 0x33,
     "no hay~datos guardados de (xxx)~en la memory card (ps2)~de la ranura para memory card 1.",
     "no hay~datos guardados de (xxx)~en el ordenador."},
    {CodeTexts, "Spanish", 0x34,
     "no hay espacio suficiente libre~en la memory card (ps2)~de la ranura para memory card 1.~~(xxx)~necesita (x) kb de espacio libre~para guardar datos.",
     "no hay espacio suficiente libre~en el ordenador.~~(xxx)~necesita (x) kb de espacio libre~para guardar datos."},
    {CodeTexts, "Spanish", 0x38,
     "¡error al formatear!~~comprueba la memory card (ps2)~de la ranura para memory card 1~e inténtalo de nuevo.",
     "¡error al preparar!~~comprueba el almacenamiento~del ordenador~e inténtalo de nuevo."},
    {CodeTexts, "Spanish", 0x39,
     "¡error al guardar!~~comprueba la memory card (ps2)~de la ranura para memory card 1~e inténtalo de nuevo.",
     "¡error al guardar!~~comprueba el almacenamiento~del ordenador~e inténtalo de nuevo."},
    {CodeTexts, "Spanish", 0x3A,
     "¡error al cargar!~~comprueba la memory card (ps2)~de la ranura para memory card 1~e inténtalo de nuevo.",
     "¡error al cargar!~~comprueba el almacenamiento~del ordenador~e inténtalo de nuevo."},
    {CodeTexts, "Spanish", 0x3E,
     "formatear",
     "preparar"},
    {CodeTexts, "Spanish", 0x42,
     "cuando el indicador de autoguardado~(abajo) esté presente, no reinicies ni~apagues la consola, no extraigas~la memory card (ps2)~de la ranura para memory card 1~ni el mando.",
     "cuando el indicador de autoguardado~(abajo) esté presente, no salgas~del juego ni apagues~el ordenador."},
    {CodeTexts, "Spanish", 0x43,
     "autoguardando datos.~~no extraigas la~memory card (ps2)~de la ranura para~memory card 1~ni el mando.~~no reinicies ni~apagues la consola.",
     "autoguardando datos.~~no salgas del juego~ni apagues el ordenador."},
    {CodeTexts, "Spanish", 0x5F,
     "¿seguro que quieres~formatear la memory card (ps2)~de la ranura para memory card 1?",
     "¿seguro que quieres~preparar el espacio~de guardado del ordenador?"},
    {CodeTexts, "Spanish", 0xB4,
     "éxito al formatear",
     "éxito al preparar"},
    {CodeTexts, "Spanish", 0xB7,
     "no hay memory card (ps2)~en la ranura para memory card 1.~~(xxx)~necesita (x) kb de espacio libre~para guardar datos.",
     "no se puede acceder~al espacio de guardado~del ordenador.~~(xxx)~necesita (x) kb de espacio libre~para guardar datos."},
    {AgentLabTexts, "Spanish", 0x2C,
     "utiliza el joystick analógico izquierdo para pilotar el ovni",
     "utiliza el joystick izquierdo para pilotar el ovni"},
    {AgentLabTexts, "Spanish", 0x2E,
     "usa el joystick analógico izquierdo para conducir la nave~mantén pulsado el botón } para volar más alto y más despacio",
     "usa el joystick izquierdo para conducir la nave~mantén pulsado el botón } para volar más alto y más despacio"},
    {AgentLabTexts, "Spanish", 0x41,
     "utiliza el joystick analógico izquierdo para controlar el patinete aéreo.",
     "utiliza el joystick izquierdo para controlar el patinete aéreo."},
    {CodeTexts, "Italian", 0x20,
     "inserisci un controller~analogico (dualshock@2)~nell'ingresso~controller 1.",
     "collega un controller~al computer."},
    {CodeTexts, "Italian", 0x26,
     "usa i tasti direzionali~o la levetta analogica sinistra~del controller~analogico (dualshock@2)~inserito nell'ingresso controller 1~per posizionare la schermata.",
     "usa i tasti direzionali~o la levetta sinistra~del controller~per posizionare la schermata."},
    {CodeTexts, "Italian", 0x27,
     "controllo della memory card (ps2)~inserita nell’ingresso~memory card 1 in corso...~~non rimuovere la~memory card (ps2)~né il controller e non~resettare/spegnere la console!",
     "controllo dei dati~salvati in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x28,
     "formattazione della memory card (ps2)~inserita nell’ingresso~memory card 1 in corso...~non rimuovere la memory card (ps2)~né il controller e non~resettare/spegnere la console!",
     "preparazione dello spazio~di salvataggio in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x29,
     "controllo della memory card (ps2)~inserita nell’ingresso~memory card 1 in corso...~~non rimuovere la~memory card (ps2)~né il controller e non~resettare/spegnere la console!",
     "controllo dei dati~salvati in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x2A,
     "salvataggio dati~in corso...~~non rimuovere la~memory card (ps2) né~il controller e non~resettare/spegnere la console!",
     "salvataggio dati~in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x2B,
     "salvataggio dati~in corso...~~non rimuovere la~memory card (ps2) né~il controller e non~resettare/spegnere la console!",
     "salvataggio dati~in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x2C,
     "caricamento dati in corso...~~non rimuovere~la memory card (ps2)~né il controller e non~resettare/spegnere la console!",
     "caricamento dati in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x2D,
     "controllo della memory card (ps2)~inserita nell’ingresso~memory card 1 in corso...~~non rimuovere la~memory card (ps2)~né il controller e non~resettare/spegnere la console!",
     "controllo dei dati~salvati in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x2E,
     "salvataggio dati~in corso...~~non rimuovere la~memory card (ps2) né~il controller e non~resettare/spegnere la console!",
     "salvataggio dati~in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x2F,
     "caricamento dati in corso...~~non rimuovere~la memory card (ps2)~né il controller e non~resettare/spegnere la console!",
     "caricamento dati in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x30,
     "nessun file salvato sulla~memory card (ps2)~inserita nell'ingresso~memory card 1.~~creare un file di~salvataggio~(xxx)?",
     "nessun file salvato~sul computer.~~creare un file di~salvataggio~(xxx)?"},
    {CodeTexts, "Italian", 0x31,
     "nessuna memory card (ps2)~inserita nell'ingresso memory card 1.",
     "spazio di salvataggio~del computer~non disponibile."},
    {CodeTexts, "Italian", 0x32,
     "la memory card (ps2)~inserita nell'ingresso memory card 1~non è formattata.~~formattare la memory card (ps2)?",
     "lo spazio di salvataggio~del computer~non è pronto.~~prepararlo?"},
    {CodeTexts, "Italian", 0x33,
     "nessun dato salvato di~(xxx)~sulla memory card (ps2) inserita~nell'ingressso memory card 1.",
     "nessun dato salvato di~(xxx)~sul computer."},
    {CodeTexts, "Italian", 0x34,
     "spazio libero insufficiente~sulla memory card (ps2)~inserita nell'ingresso memory card 1~~(xxx)~richiede (x) kb di spazio libero~per salvare i dati.",
     "spazio libero insufficiente~sul computer.~~(xxx)~richiede (x) kb di spazio libero~per salvare i dati."},
    {CodeTexts, "Italian", 0x38,
     "formattazione fallita!~~controllare la memory card (ps2)~inserita nell’ingresso memory card 1~e riprovare.",
     "preparazione fallita!~~controllare lo spazio~di archiviazione del computer~e riprovare."},
    {CodeTexts, "Italian", 0x39,
     "salvataggio fallito!~~controllare la memory card (ps2)~inserita nell’ingresso memory card 1~e riprovare.",
     "salvataggio fallito!~~controllare lo spazio~di archiviazione del computer~e riprovare."},
    {CodeTexts, "Italian", 0x3A,
     "caricamento fallito!~~controllare la memory card (ps2)~inserita nell’ingresso memory card 1~e riprovare.",
     "caricamento fallito!~~controllare lo spazio~di archiviazione del computer~e riprovare."},
    {CodeTexts, "Italian", 0x3E,
     "formatta",
     "prepara"},
    {CodeTexts, "Italian", 0x42,
     "non rimuovere la memory~card (ps2), inserita nell'ingresso~memory card 1 né il controller e~non resettare/spegnere la~console quando l'indicatore~dell'autosalvataggio (sotto)~è presente.",
     "non uscire dal gioco~e non spegnere il computer~quando l'indicatore~dell'autosalvataggio (sotto)~è presente."},
    {CodeTexts, "Italian", 0x43,
     "autosalvataggio dati~in corso...~~non rimuovere la memory~card (ps2) inserita~nell'ingresso memory~card 1 né il controller e~non resettare/spegnere~la console!",
     "autosalvataggio dati~in corso...~~non uscire dal gioco~e non spegnere~il computer!"},
    {CodeTexts, "Italian", 0x5F,
     "~formattare la memory card (ps2)~inserita nell'ingresso memory card 1?",
     "~preparare lo spazio~di salvataggio del computer?"},
    {CodeTexts, "Italian", 0xB4,
     "formattazione completata",
     "preparazione completata"},
    {CodeTexts, "Italian", 0xB7,
     "nessuna memory card (ps2)~inserita nell'ingresso memory card 1~~(xxx)~richiede (x) kb di spazio libero~per salvare i dati.",
     "spazio di salvataggio~del computer~non disponibile.~~(xxx)~richiede (x) kb di spazio libero~per salvare i dati."},
    {AgentLabTexts, "Italian", 0x2C,
     "usa la levetta analogica sinistra per guidare l'ufo",
     "usa la levetta sinistra per guidare l'ufo"},
    {AgentLabTexts, "Italian", 0x2E,
     "usa la levetta analogica sinistra per manovrare l' hoverboard~tieni premuto il tasto } per volare più in alto e più lentamente",
     "usa la levetta sinistra per manovrare l' hoverboard~tieni premuto il tasto } per volare più in alto e più lentamente"},
    {AgentLabTexts, "Italian", 0x41,
     "usa la levetta analogica sinistra per controllare lo scooter volante.",
     "usa la levetta sinistra per controllare lo scooter volante."},
    // Not the console's wording: the cutscene skip's prompt (the community mod's mod/text.txt, made "hold any button": any key, mouse
    // button or pad button held skips, ui/features.cpp), in the AgentLab texts' unused
    // "zzz" line, which the level recipes' BottomTextDisplay shows while a scene can be skipped (src/platform/native/levelrecipes.cpp)
    {AgentLabTexts, "English", 0x18, "zzz", "press any button to skip"},
    {AgentLabTexts, "French", 0x18, "zzz", "maintiens une touche pour passer"},
    {AgentLabTexts, "German", 0x18, "zzz", "beliebige taste halten zum überspringen"},
    {AgentLabTexts, "Spanish", 0x18, "zzz", "mantén cualquier botón para saltar"},
    {AgentLabTexts, "Italian", 0x18, "zzz", "tieni premuto un tasto per saltare"},
};
const u32 g_TextOverrideCount = sizeof(g_TextOverrides) / sizeof(g_TextOverrides[0]);

// The native texts by language (English, French, German, Spanish, Italian)
const NativeTextLine g_NativeTexts[] = {
    {TextResolution, {"resolution", "résolution", "auflösung", "resolución", "risoluzione"}},
    {TextDisplay, {"display", "affichage", "anzeige", "pantalla", "schermo"}},
    {TextScale1, {"1x", "1x", "1x", "1x", "1x"}},
    {TextScale2, {"2x", "2x", "2x", "2x", "2x"}},
    {TextScale3, {"3x", "3x", "3x", "3x", "3x"}},
    {TextScale4, {"4x", "4x", "4x", "4x", "4x"}},
    {TextWindow960, {"960x720", "960x720", "960x720", "960x720", "960x720"}},
    {TextWindow1280, {"1280x960", "1280x960", "1280x960", "1280x960", "1280x960"}},
    {TextWindow1600, {"1600x1200", "1600x1200", "1600x1200", "1600x1200", "1600x1200"}},
    {TextWindow1920, {"1920x1440", "1920x1440", "1920x1440", "1920x1440", "1920x1440"}},
    {TextWindow1280Wide, {"1280x720", "1280x720", "1280x720", "1280x720", "1280x720"}},
    {TextWindow1920Wide, {"1920x1080", "1920x1080", "1920x1080", "1920x1080", "1920x1080"}},
    {TextFullscreen, {"fullscreen", "plein écran", "vollbild", "completa", "intero"}},
    {TextFiltering, {"filtering", "filtrage", "filter", "filtrado", "filtro"}},
    {TextSmooth, {"smooth", "lisse", "weich", "suave", "morbido"}},
    {TextSharp, {"sharp", "net", "scharf", "nítido", "nitido"}},
    {TextCrt, {"scanlines", "lignes crt", "crt-zeilen", "líneas crt", "linee crt"}},
    {TextVoiceVolume, {"voice volume", "volume des voix", "stimmen-lautstärke", "volumen de las voces", "volume voci"}},
    {TextSkipCutscenes, {"skip cutscenes", "passer les scènes", "szenen überspringen", "saltar escenas", "salta scene"}},
    {TextFastLoading, {"fast loading", "chargement rapide", "schnelles laden", "carga rápida", "caricamento rapido"}},
    {TextPauseInactive, {"pause in background", "pause en arrière-plan", "pause im hintergrund", "pausa en segundo plano", "pausa in secondo piano"}},
    {TextDetailedLog, {"detailed log", "journal détaillé", "ausführliches protokoll", "registro detallado", "log dettagliato"}},
    {TextRefreshRate, {"refresh rate", "fréquence", "bildfrequenz", "frecuencia", "frequenza"}},
    {Text50Hz, {"50 hz", "50 hz", "50 hz", "50 hz", "50 hz"}},
    {Text60Hz, {"60 hz", "60 hz", "60 hz", "60 hz", "60 hz"}},
    {Text120Hz, {"120 hz", "120 hz", "120 hz", "120 hz", "120 hz"}},
    {Text144Hz, {"144 hz", "144 hz", "144 hz", "144 hz", "144 hz"}},
    {TextUnlimited, {"unlimited", "illimitée", "unbegrenzt", "ilimitada", "illimitata"}},
    {TextGameLanguage, {"text language", "langue des textes", "textsprache", "idioma de los textos", "lingua dei testi"}},
    {TextLanguageWindows, {"as windows", "comme windows", "wie windows", "como windows", "come windows"}},
    {TextLanguageEnglish, {"english", "english", "english", "english", "english"}},
    {TextLanguageFrench, {"français", "français", "français", "français", "français"}},
    {TextLanguageGerman, {"deutsch", "deutsch", "deutsch", "deutsch", "deutsch"}},
    {TextLanguageItalian, {"italiano", "italiano", "italiano", "italiano", "italiano"}},
    {TextLanguageSpanish, {"español", "español", "español", "español", "español"}},
    {TextBugFixes, {"bug fixes", "corrections", "fehlerkorrekturen", "correcciones", "correzioni"}},
    {TextControls, {"controls", "commandes", "steuerung", "controles", "comandi"}},
    {TextResetControls, {"reset controls", "réinitialiser", "zurücksetzen", "restablecer", "ripristina"}},
    {TextPressKey, {"press a key or button (esc to cancel)", "appuie sur une touche (échap pour annuler)", "taste drücken (esc bricht ab)", "pulsa una tecla (esc para cancelar)", "premi un tasto (esc per annullare)"}},
    {TextL3, {"l3", "l3", "l3", "l3", "l3"}},
    {TextR3, {"r3", "r3", "r3", "r3", "r3"}},
    {TextStart, {"start", "start", "start", "start", "start"}},
    {TextSelect, {"select", "select", "select", "select", "select"}},
    {TextUp, {"up", "haut", "oben", "arriba", "su"}},
    {TextDown, {"down", "bas", "unten", "abajo", "giù"}},
    {TextLeft, {"left", "gauche", "links", "izquierda", "sinistra"}},
    {TextRight, {"right", "droite", "rechts", "derecha", "destra"}},
    {TextLeftStick, {"stick", "joystick", "stick", "joystick", "levetta"}},
    {TextJump, {"jump", "sauter", "springen", "saltar", "saltare"}},
    {TextCrouch, {"crouch / slide", "accroupi / glisser", "ducken / rutschen", "agacharse / deslizar", "abbassarsi / scivolare"}},
    {TextStatus, {"status", "statut", "status", "estado", "stato"}},
    {TextSpin, {"spin", "toupie", "drehen", "girar", "girare"}},
    {TextNintendoLayout, {"layout", "boutons", "belegung", "botones", "tasti"}},
    {TextNintendo, {"nintendo", "nintendo", "nintendo", "nintendo", "nintendo"}},
    {TextController, {"controller", "manette", "controller", "mando", "controller"}},
    {TextAutomatic, {"auto", "auto", "auto", "auto", "auto"}},
    {TextKeyboard, {"keyboard", "clavier", "tastatur", "teclado", "tastiera"}},
    {TextXbox, {"xbox", "xbox", "xbox", "xbox", "xbox"}},
    {TextPlayStation, {"playstation", "playstation", "playstation", "playstation", "playstation"}},
    {TextSwitch, {"switch", "switch", "switch", "switch", "switch"}},
    {TextMouse, {"mouse", "souris", "maus", "ratón", "mouse"}},
    {TextTabDisplay, {"display", "affichage", "anzeige", "pantalla", "schermo"}},
    {TextTabAudio, {"audio", "audio", "audio", "audio", "audio"}},
    {TextTabGameplay, {"gameplay", "jeu", "spiel", "juego", "gioco"}},
    {TextDisplayMode, {"display mode", "mode d'affichage", "anzeigemodus", "modo de pantalla", "modalità schermo"}},
    {TextUpscale, {"texture upscale", "textures agrandies", "texturen skalieren", "escalar texturas", "scala texture"}},
    {TextTexturePack, {"texture pack", "pack de textures", "texturpaket", "pack de texturas", "pacchetto texture"}},
    {TextAntiAliasing, {"anti-aliasing", "anticrénelage", "kantenglättung", "antialiasing", "antialiasing"}},
    {TextVSync, {"v-sync", "v-sync", "v-sync", "v-sync", "v-sync"}},
    {TextColumnAction, {"action", "action", "aktion", "acción", "azione"}},
    {TextColumnKey, {"key", "touche", "taste", "tecla", "tasto"}},
    {TextColumnKey2, {"alt key", "autre", "alternativ", "alternativa", "alternativo"}},
    {TextColumnPad, {"controller", "manette", "controller", "mando", "controller"}},
    {TextMoveUp, {"move up", "avancer", "vorwärts", "avanzar", "avanti"}},
    {TextMoveDown, {"move down", "reculer", "rückwärts", "retroceder", "indietro"}},
    {TextMoveLeft, {"move left", "gauche", "links", "izquierda", "sinistra"}},
    {TextMoveRight, {"move right", "droite", "rechts", "derecha", "destra"}},
    {TextCameraLeft, {"camera left", "caméra gauche", "kamera links", "cámara izquierda", "telecamera sin."}},
    {TextCameraRight, {"camera right", "caméra droite", "kamera rechts", "cámara derecha", "telecamera des."}},
    {TextStrafeLeft, {"strafe left", "pas latéral g.", "seitlich links", "lateral izq.", "laterale sin."}},
    {TextStrafeRight, {"strafe right", "pas latéral d.", "seitlich rechts", "lateral der.", "laterale des."}},
    {TextPause, {"pause", "pause", "pause", "pausa", "pausa"}},
    {TextResetKeyboard, {"reset keyboard", "clavier par défaut", "tastatur zurücksetzen", "restablecer teclado", "ripristina tastiera"}},
    {TextResetController, {"reset controller", "manette par défaut", "controller zurücksetzen", "restablecer mando", "ripristina controller"}},
    {TextResetDefaults, {"reset to defaults", "valeurs par défaut", "standard", "valores por defecto", "valori predefiniti"}},
    {TextDescDisplayMode, {"a window of a set size, or fullscreen.", "une fenêtre d'une taille donnée, ou plein écran.", "ein fenster fester größe oder vollbild.", "una ventana de tamaño fijo o pantalla completa.", "una finestra di dimensione fissa o schermo intero."}},
    {TextDescResolution, {"sharper 3d, smoother edges. 3x and up are slower: 6x and 8x need a strong graphics card.", "3d plus net, bords lissés. 3x et plus sont plus lents : 6x et 8x demandent une carte graphique puissante.", "schärferes 3d, glatte kanten. ab 3x langsamer: 6x und 8x brauchen eine starke grafikkarte.", "3d más nítido, bordes suaves. desde 3x es más lento: 6x y 8x piden una gráfica potente.", "3d più nitido, bordi lisci. da 3x è più lento: 6x e 8x chiedono una scheda video potente."}},
    {TextDescWidescreen, {"draw the game for a 16:9 screen.", "jeu adapté aux écrans 16:9.", "das spiel für 16:9-bildschirme.", "el juego para pantallas 16:9.", "il gioco per schermi 16:9."}},
    {TextDescFiltering, {"smooth or sharp pixels when the picture is scaled.", "pixels lissés ou nets à l'agrandissement.", "weiche oder scharfe pixel beim skalieren.", "píxeles suaves o nítidos al escalar.", "pixel morbidi o nitidi nel ridimensionamento."}},
    {TextDescUpscale, {"smooth the game's textures at a larger size.", "textures lissées en plus grand.", "texturen größer und glatter.", "texturas suavizadas a mayor tamaño.", "texture levigate più grandi."}},
    {TextDescTexturePack, {"use the textures of a texture pack.", "utiliser un pack de textures.", "texturen eines texturpakets nutzen.", "usar un pack de texturas.", "usa un pacchetto di texture."}},
    {TextDescAntiAliasing, {"fxaa and smaa smooth the edges, smaa better. ssaa draws the picture 2 or 4 times larger: slower.", "fxaa et smaa lissent les bords, smaa mieux. ssaa dessine l'image 2 ou 4 fois plus grande : plus lent.", "fxaa und smaa glätten kanten, smaa besser. ssaa zeichnet das bild 2- oder 4-mal größer: langsamer.", "fxaa y smaa suavizan bordes, smaa mejor. ssaa dibuja la imagen 2 o 4 veces mayor: más lento.", "fxaa e smaa levigano i bordi, smaa meglio. ssaa disegna l'immagine 2 o 4 volte più grande: più lento."}},
    {TextDescVSync, {"wait for the screen's refresh: no tearing.", "synchronisé à l'écran: pas de déchirure.", "auf den bildschirm warten: kein tearing.", "sincronizar con la pantalla: sin cortes.", "sincronizza con lo schermo: niente tearing."}},
    {TextDescCrt, {"the look of an old crt television.", "l'aspect d'une vieille télévision.", "wie ein alter röhrenfernseher.", "el aspecto de una tele antigua.", "l'aspetto di un vecchio televisore."}},
    {TextDescGameLanguage, {"the texts and title pictures, and the voices unless chosen below. from the next start.", "les textes et les titres, et les voix sauf choix plus bas. au prochain lancement.", "texte und titelbilder, die stimmen auch, außer unten gewählt. ab dem nächsten start.", "los textos y los títulos, y las voces salvo que elijas abajo. al volver a abrir el juego.", "i testi e i titoli, e le voci se non scelte sotto. dal prossimo avvio del gioco."}},
    {TextDescRefreshRate, {"50 hz is the original timing. above 60 needs a fast pc.", "50 hz est la cadence d'origine. au-delà de 60, un pc rapide.", "50 hz ist das original. über 60 braucht einen schnellen pc.", "50 hz es el ritmo original. más de 60 pide un pc rápido.", "50 hz è la frequenza originale. oltre 60 serve un pc veloce."}},
    {TextDescScreenPosition, {"move the picture on the screen.", "déplacer l'image à l'écran.", "das bild verschieben.", "mover la imagen en pantalla.", "sposta l'immagine sullo schermo."}},
    {TextDescMusic, {"the music's volume.", "le volume de la musique.", "die lautstärke der musik.", "el volumen de la música.", "il volume della musica."}},
    {TextDescEffects, {"the sound effects' volume.", "le volume des sons.", "die lautstärke der effekte.", "el volumen de los efectos.", "il volume degli effetti."}},
    {TextDescVoices, {"the characters' voices' volume.", "le volume des voix.", "die lautstärke der stimmen.", "el volumen de las voces.", "il volume delle voci."}},
    {TextDescOutput, {"mono, stereo or dolby pro logic ii.", "mono, stéréo ou dolby pro logic ii.", "mono, stereo oder dolby pro logic ii.", "mono, estéreo o dolby pro logic ii.", "mono, stereo o dolby pro logic ii."}},
    {TextDescController, {"the button prompts: auto follows what you use.", "les boutons affichés: auto suit ce que tu utilises.", "die tastensymbole: auto folgt dem gerät.", "los botones: auto sigue lo que usas.", "i tasti mostrati: auto segue ciò che usi."}},
    {TextDescLayout, {"nintendo: a selects and b goes back in menus.", "nintendo: a valide et b revient dans les menus.", "nintendo: a wählt, b zurück in menüs.", "nintendo: a elige y b vuelve en los menús.", "nintendo: a seleziona e b torna nei menu."}},
    {TextDescVibration, {"the controller's vibration.", "les vibrations de la manette.", "die vibration des controllers.", "la vibración del mando.", "la vibrazione del controller."}},
    {TextDescMouse, {"use the mouse in menus.", "utiliser la souris dans les menus.", "die maus in menüs nutzen.", "usar el ratón en los menús.", "usa il mouse nei menu."}},
    {TextDescBinding, {"select a key or button to change it.", "choisis une touche pour la changer.", "taste wählen, um sie zu ändern.", "elige una tecla para cambiarla.", "scegli un tasto per cambiarlo."}},
    {TextDescResetKeyboard, {"the default keys again.", "les touches par défaut.", "die standardtasten.", "las teclas por defecto.", "i tasti predefiniti."}},
    {TextDescResetController, {"the default buttons again.", "les boutons par défaut.", "die standardknöpfe.", "los botones por defecto.", "i pulsanti predefiniti."}},
    {TextDescSkip, {"hold any button to skip cutscenes and movies.", "maintiens une touche pour passer les scènes.", "beliebige taste halten, um szenen zu überspringen.", "mantén cualquier botón para saltar escenas.", "tieni premuto un tasto per saltare le scene."}},
    {TextDescFastLoading, {"load levels faster.", "chargement plus rapide des niveaux.", "level schneller laden.", "cargar niveles más rápido.", "caricamento più rapido dei livelli."}},
    {TextDescPauseInactive, {"pause when the window is in the background.", "pause quand la fenêtre est en arrière-plan.", "pause im hintergrund.", "pausa en segundo plano.", "pausa quando la finestra è in secondo piano."}},
    {TextDescDetailedLog, {"write more of what happens to log.txt, for bug reports.", "plus de détails dans log.txt, pour signaler un bug.", "mehr details in log.txt, für fehlerberichte.", "más detalles en log.txt, para informar de errores.", "più dettagli in log.txt, per segnalare i bug."}},
    {TextDescBugFixes, {"fixes from the community patch and the decomp's bug list. off plays exactly like the original.", "corrections du patch de la communauté et de la liste de la décompilation. désactivé : comme l'original.", "korrekturen aus dem community-patch und der fehlerliste der dekompilierung. aus: wie das original.", "arreglos del parche de la comunidad y de la lista de la descompilación. desactivado: como el original.", "correzioni della patch della comunità e dell'elenco della decompilazione. disattivato: come l'originale."}},
    {TextDescResetDefaults, {"this tab's settings as they were at first.", "les réglages de cet onglet par défaut.", "die einstellungen dieser seite zurücksetzen.", "los ajustes de esta pestaña por defecto.", "le impostazioni di questa scheda predefinite."}},
    {TextTabs, {"tabs", "onglets", "reiter", "pestañas", "schede"}},
    {TextTabAccessibility, {"accessibility", "accessibilité", "barrierefreiheit", "accesibilidad", "accessibilità"}},
    {TextWindowSize, {"window size", "taille de fenêtre", "fenstergröße", "tamaño de ventana", "dimensione finestra"}},
    {TextWindowed, {"window", "fenêtre", "fenster", "ventana", "finestra"}},
    {TextKeyboardBindings, {"keyboard", "clavier", "tastatur", "teclado", "tastiera"}},
    {TextControllerBindings, {"controller", "manette", "controller", "mando", "controller"}},
    {TextColumnButton, {"button", "bouton", "knopf", "botón", "pulsante"}},
    {TextColumnButton2, {"alt button", "autre", "alternativ", "alternativo", "alternativo"}},
    {TextCameraShake, {"camera shake", "tremblement caméra", "kamerawackeln", "vibración de cámara", "tremolio telecamera"}},
    {TextInvertX, {"invert camera x", "inverser caméra x", "kamera x umkehren", "invertir cámara x", "inverti telecamera x"}},
    {TextInvertY, {"invert camera y", "inverser caméra y", "kamera y umkehren", "invertir cámara y", "inverti telecamera y"}},
    {TextCameraSpeed, {"camera speed", "vitesse caméra", "kamerageschwindigkeit", "velocidad de cámara", "velocità telecamera"}},
    {TextDeadZone, {"stick dead zone", "zone morte", "stick-totzone", "zona muerta", "zona morta"}},
    {TextMuteInactive, {"mute in background", "muet en arrière-plan", "stumm im hintergrund", "silencio en segundo plano", "muto in secondo piano"}},
    {TextShoulderLeft, {"shoulder left", "gâchette gauche", "schulter links", "gatillo izquierdo", "dorsale sinistro"}},
    {TextShoulderRight, {"shoulder right", "gâchette droite", "schulter rechts", "gatillo derecho", "dorsale destro"}},
    {TextWalk, {"walk (toggle)", "marcher (bascule)", "gehen (umschalten)", "caminar (alternar)", "cammina (attiva)"}},
    {TextCameraUp, {"camera up", "caméra haut", "kamera oben", "cámara arriba", "telecamera su"}},
    {TextCameraDown, {"camera down", "caméra bas", "kamera unten", "cámara abajo", "telecamera giù"}},
    {TextMouseLeft, {"mouse left", "souris gauche", "maus links", "ratón izq.", "mouse sin."}},
    {TextMouseRight, {"mouse right", "souris droite", "maus rechts", "ratón der.", "mouse des."}},
    {TextKeepDisplay, {"keep these display settings?", "garder cet affichage ?", "diese anzeige behalten?", "¿mantener esta pantalla?", "mantenere questo schermo?"}},
    {TextClear, {"clear", "effacer", "löschen", "borrar", "cancella"}},
    {TextDescWindowSize, {"the window's size.", "la taille de la fenêtre.", "die größe des fensters.", "el tamaño de la ventana.", "la dimensione della finestra."}},
    {TextDescKeyboardBindings, {"the keys for each action.", "les touches de chaque action.", "die tasten jeder aktion.", "las teclas de cada acción.", "i tasti di ogni azione."}},
    {TextDescControllerBindings, {"the controller's buttons for each action.", "les boutons de chaque action.", "die knöpfe jeder aktion.", "los botones de cada acción.", "i pulsanti di ogni azione."}},
    {TextDescCameraShake, {"the camera shakes with explosions and impacts.", "la caméra tremble aux explosions.", "die kamera wackelt bei explosionen.", "la cámara tiembla con explosiones.", "la telecamera trema con le esplosioni."}},
    {TextDescInvertX, {"turn the camera the other way left and right.", "caméra inversée à gauche et à droite.", "kamera links und rechts umgekehrt.", "cámara invertida a izquierda y derecha.", "telecamera invertita a sinistra e destra."}},
    {TextDescInvertY, {"turn the camera the other way up and down.", "caméra inversée en haut et en bas.", "kamera oben und unten umgekehrt.", "cámara invertida arriba y abajo.", "telecamera invertita su e giù."}},
    {TextDescCameraSpeed, {"how fast the camera turns.", "la vitesse de la caméra.", "wie schnell die kamera dreht.", "la velocidad de la cámara.", "la velocità della telecamera."}},
    {TextDescDeadZone, {"how far a stick moves before it counts.", "la course du joystick ignorée.", "wie weit ein stick ohne wirkung geht.", "el recorrido ignorado del joystick.", "la corsa ignorata della levetta."}},
    {TextDescMuteInactive, {"no sound when the window is in the background.", "pas de son en arrière-plan.", "kein ton im hintergrund.", "sin sonido en segundo plano.", "nessun suono in secondo piano."}},
    {TextNext, {"next", "suivant", "weiter", "siguiente", "avanti"}},
    {TextPositional, {"position", "position", "position", "posición", "posizione"}},
    {TextRestartCheckpoint, {"restart checkpoint", "recommencer", "neu starten", "reiniciar", "ricomincia"}},
    {TextLevelSelect, {"level select", "choix du niveau", "levelauswahl", "elegir nivel", "scelta livello"}},
    {TextLeftStickUp, {"left stick up", "stick g. haut", "linker stick hoch", "stick izq. arriba", "levetta sin. su"}},
    {TextLeftStickDown, {"left stick down", "stick g. bas", "linker stick runter", "stick izq. abajo", "levetta sin. giù"}},
    {TextLeftStickLeft, {"left stick left", "stick g. gauche", "linker stick links", "stick izq. izquierda", "levetta sin. sinistra"}},
    {TextLeftStickRight, {"left stick right", "stick g. droite", "linker stick rechts", "stick izq. derecha", "levetta sin. destra"}},
    {TextRightStickUp, {"right stick up", "stick d. haut", "rechter stick hoch", "stick der. arriba", "levetta des. su"}},
    {TextRightStickDown, {"right stick down", "stick d. bas", "rechter stick runter", "stick der. abajo", "levetta des. giù"}},
    {TextRightStickLeft, {"right stick left", "stick d. gauche", "rechter stick links", "stick der. izquierda", "levetta des. sinistra"}},
    {TextRightStickRight, {"right stick right", "stick d. droite", "rechter stick rechts", "stick der. derecha", "levetta des. destra"}},
    {TextMouseMiddle, {"mouse middle", "souris milieu", "maus mitte", "ratón central", "mouse centr."}},
    {TextMouse4, {"mouse 4", "souris 4", "maus 4", "ratón 4", "mouse 4"}},
    {TextMouse5, {"mouse 5", "souris 5", "maus 5", "ratón 5", "mouse 5"}},
    {TextWheelUp, {"wheel up", "molette haut", "mausrad hoch", "rueda arriba", "rotella su"}},
    {TextWheelDown, {"wheel down", "molette bas", "mausrad runter", "rueda abajo", "rotella giù"}},
    {TextMouseLook, {"mouse look", "visée souris", "mausblick", "vista con ratón", "visuale mouse"}},
    {TextMouseSensitivity, {"mouse sensitivity", "sensibilité souris", "mausempfindlichkeit", "sensibilidad del ratón", "sensibilità mouse"}},
    {TextInvertMouseY, {"invert mouse y", "inverser souris y", "maus y umkehren", "invertir ratón y", "inverti mouse y"}},
    {TextDescMouseLook, {"move the camera with the mouse while playing.", "la souris tourne la caméra en jeu.", "die maus dreht die kamera im spiel.", "el ratón mueve la cámara al jugar.", "il mouse muove la telecamera in gioco."}},
    {TextDescMouseSensitivity, {"how fast the mouse turns the camera.", "la vitesse de la caméra à la souris.", "wie schnell die maus die kamera dreht.", "lo rápido que el ratón gira la cámara.", "quanto velocemente il mouse gira la telecamera."}},
    {TextDescInvertMouseY, {"moving the mouse up looks down.", "souris vers le haut : la caméra baisse.", "maus hoch blickt nach unten.", "ratón arriba mira hacia abajo.", "mouse in alto guarda in basso."}},
    {TextDithering, {"dithering", "tramage", "dithering", "tramado", "dithering"}},
    {TextDescDithering, {"the ps2 shows the picture in 16-bit colour with a fine pattern. off keeps its full colour.", "la ps2 affiche l'image en couleurs 16 bits avec une fine trame. désactivé garde toutes ses couleurs.", "die ps2 zeigt das bild in 16-bit-farben mit feinem muster. aus behält die vollen farben.", "la ps2 muestra la imagen en color de 16 bits con una trama fina. desactivado mantiene todo el color.", "la ps2 mostra l'immagine a colori a 16 bit con una trama fine. disattivato mantiene tutti i colori."}},
    {TextDitherPs2, {"as the ps2", "comme la ps2", "wie die ps2", "como la ps2", "come la ps2"}},
    {TextSsaa2, {"ssaa 2x", "ssaa 2x", "ssaa 2x", "ssaa 2x", "ssaa 2x"}},
    {TextSsaa4, {"ssaa 4x", "ssaa 4x", "ssaa 4x", "ssaa 4x", "ssaa 4x"}},
    {TextRenderDistance, {"draw distance", "distance d'affichage", "sichtweite", "distancia de dibujado", "distanza di disegno"}},
    {TextDescRenderDistance, {"how far things and their detailed models are drawn. farther is slower on older pcs.", "jusqu'où les objets et leurs détails sont affichés. plus loin ralentit les vieux pc.", "wie weit objekte und ihre details gezeichnet werden. weiter bremst ältere pcs.", "hasta dónde se ven los objetos y sus detalles. más lejos es más lento en pcs antiguos.", "fin dove si vedono gli oggetti e i loro dettagli. più lontano rallenta i pc vecchi."}},
    {TextDistanceShort, {"short", "courte", "kurz", "corta", "corta"}},
    {TextDistanceOriginal, {"original", "d'origine", "original", "original", "originale"}},
    {TextDistanceMedium, {"medium", "moyenne", "mittel", "media", "media"}},
    {TextDistanceLong, {"long", "longue", "weit", "larga", "lunga"}},
    {TextQuitDesktop, {"quit to desktop", "quitter vers le bureau", "zum desktop beenden", "salir al escritorio", "esci al desktop"}},
    {TextDescQuitDesktop, {"closes the game as the window's close button does. progress not saved is lost.", "ferme le jeu comme le bouton de la fenêtre. la progression non sauvegardée est perdue.", "schließt das spiel wie der knopf des fensters. nicht gespeicherter fortschritt geht verloren.", "cierra el juego como el botón de la ventana. el progreso no guardado se pierde.", "chiude il gioco come il pulsante della finestra. i progressi non salvati vanno persi."}},
    {TextQuitAsk, {"quit the game now? progress not saved is lost.", "quitter le jeu maintenant ? la progression non sauvegardée est perdue.", "das spiel jetzt beenden? nicht gespeicherter fortschritt geht verloren.", "¿salir del juego ahora? el progreso no guardado se pierde.", "uscire dal gioco adesso? i progressi non salvati vanno persi."}},
    {TextCredits, {"credits", "crédits", "mitwirkende", "créditos", "riconoscimenti"}},
    {TextDescCredits, {"the people who made this port and the work it stands on.", "les personnes qui ont fait ce portage et le travail sur lequel il repose.", "die leute hinter dieser portierung und der arbeit, auf der sie aufbaut.", "las personas que hicieron este port y el trabajo en el que se basa.", "le persone che hanno fatto questo port e il lavoro su cui si basa."}},
    {TextCreditsWindows, {"the windows port", "le portage windows", "die windows-portierung", "el port para windows", "il port per windows"}},
    {TextCreditsDecompilation, {"the decompilation", "la décompilation", "die dekompilierung", "la descompilación", "la decompilazione"}},
    {TextCreditsMac, {"the mac port", "le portage mac", "die mac-portierung", "el port para mac", "il port per mac"}},
    {TextCreditsImproved, {"crash twinsanity improved's fixes and 21:9", "les corrections et le 21:9 de crash twinsanity improved", "korrekturen und 21:9 aus crash twinsanity improved", "las correcciones y el 21:9 de crash twinsanity improved", "le correzioni e il 21:9 di crash twinsanity improved"}},
    {TextCreditsLinux, {"the linux build", "la version linux", "die linux-version", "la versión para linux", "la versione per linux"}},
    {TextCreditsTesters, {"tests and reports", "tests et rapports", "tests und berichte", "pruebas e informes", "prove e segnalazioni"}},
    {TextCreditsTextures, {"the hd texture pack", "le pack de textures hd", "das hd-texturpaket", "el pack de texturas hd", "il pacchetto di texture hd"}},
    {TextCreditsEvolved, {"code from twinsanityevolved", "du code de twinsanityevolved", "code aus twinsanityevolved", "código de twinsanityevolved", "codice da twinsanityevolved"}},
    {TextSpeechLanguage, {"voice language", "langue des voix", "sprache der stimmen", "idioma de las voces", "lingua delle voci"}},
    {TextDescSpeechLanguage, {"the voices and the movies' sound: as the texts, or another. from the next start.", "les voix et le son des films : comme les textes, ou une autre. au prochain lancement.", "stimmen und ton der filme: wie die texte oder eine andere. ab dem nächsten start.", "las voces y el sonido de los vídeos: como los textos u otro. al volver a abrir el juego.", "le voci e l'audio dei filmati: come i testi, o un'altra. dal prossimo avvio del gioco."}},
    {TextLanguageAsTexts, {"as the texts", "comme les textes", "wie die texte", "como los textos", "come i testi"}},
    {TextLanguagePortuguese, {"português", "português", "português", "português", "português"}},
    {TextLanguageDutch, {"nederlands", "nederlands", "nederlands", "nederlands", "nederlands"}},
    {TextSmaa, {"smaa", "smaa", "smaa", "smaa", "smaa"}},
    {TextAnisotropic, {"anisotropic filtering", "filtrage anisotrope", "anisotrope filterung", "filtrado anisotrópico", "filtro anisotropico"}},
    {TextDescAnisotropic, {"textures seen at an angle (floors, paths) sharper. off is the ps2's look.", "textures vues de biais (sols, chemins) plus nettes. désactivé : comme la ps2.", "texturen im schrägen winkel (böden, wege) schärfer. aus: wie die ps2.", "texturas vistas en ángulo (suelos, caminos) más nítidas. desactivado: como la ps2.", "texture viste di sbieco (pavimenti, sentieri) più nitide. disattivato: come la ps2."}},
    {TextUpdateCheck, {"check for updates", "vérifier les mises à jour", "nach updates suchen", "buscar actualizaciones", "controlla aggiornamenti"}},
    {TextDescUpdateCheck, {"at each start, asks github for the latest version. it never downloads anything.", "à chaque lancement, demande à github la dernière version. rien n'est téléchargé.", "fragt bei jedem start github nach der neuesten version. lädt nie etwas herunter.", "en cada inicio, pregunta a github por la última versión. nunca descarga nada.", "a ogni avvio chiede a github l'ultima versione. non scarica mai niente."}},
    {TextUpdatePage, {"new version's page", "page de la nouvelle version", "seite der neuen version", "página de la nueva versión", "pagina della nuova versione"}},
    {TextDescUpdatePage, {"opens the new version's page on github in your browser.", "ouvre la page de la nouvelle version sur github dans ton navigateur.", "öffnet die seite der neuen version auf github im browser.", "abre la página de la nueva versión en github en tu navegador.", "apre la pagina della nuova versione su github nel browser."}},
    {TextUpdateNotice, {"version x.y.z is out: options, gameplay.", "la version x.y.z est sortie : options, jeu.", "version x.y.z ist da: optionen, spiel.", "ya está la versión x.y.z: opciones, juego.", "è uscita la versione x.y.z: opzioni, gioco."}},
    {TextInfiniteLives, {"infinite lives", "vies infinies", "unendliche leben", "vidas infinitas", "vite infinite"}},
    {TextDescInfiniteLives, {"a death costs no life: the count stays where it is. off plays as the original.", "une mort ne fait perdre aucune vie : le compteur ne bouge pas. désactivé : comme l'original.", "ein tod kostet kein leben: der zähler bleibt. aus: wie das original.", "morir no cuesta vidas: el contador no cambia. desactivado: como el original.", "morire non costa vite: il conteggio resta. disattivato: come l'originale."}},
    {TextStrongMask, {"stronger aku aku", "aku aku plus fort", "stärkerer aku aku", "aku aku más fuerte", "aku aku più forte"}},
    {TextDescStrongMask, {"aku aku's masks also take tnt and nitro blasts, as in the earlier crash games.", "les masques d'aku aku encaissent aussi tnt et nitro, comme dans les anciens crash.", "aku akus masken halten auch tnt und nitro aus, wie in den früheren crash-spielen.", "las máscaras de aku aku aguantan también tnt y nitro, como en los crash anteriores.", "le maschere di aku aku reggono anche tnt e nitro, come nei crash precedenti."}},
    {TextSlideJump, {"slide jump", "saut glissé", "rutschsprung", "salto deslizante", "salto in scivolata"}},
    {TextDescSlideJump, {"the long jump from a slide: at any moment of it, or easier (a later press, steered in the air).", "le saut long depuis une glissade : à tout moment, ou plus facile (plus tard, dirigé en l'air).", "der weitsprung aus dem rutschen: jederzeit oder leichter (später drücken, in der luft lenken).", "el salto largo al deslizarte: en cualquier momento, o más fácil (más tarde, dirigido en el aire).", "il salto lungo dalla scivolata: in qualunque momento, o più facile (più tardi, guidato in aria)."}},
    {TextSlideJumpAnyMoment, {"any moment", "à tout moment", "jederzeit", "en cualquier momento", "in qualunque momento"}},
    {TextSlideJumpEasier, {"easier", "plus facile", "leichter", "más fácil", "più facile"}},
    {TextDescLevelSelect, {"start any level from the main menu. what you save then is kept apart from your saves.", "lancer n'importe quel niveau depuis le menu principal. ses sauvegardes restent à part.", "jedes level aus dem hauptmenü starten. was du dann speicherst, bleibt getrennt.", "empezar cualquier nivel desde el menú principal. lo que guardes entonces queda aparte.", "inizia qualsiasi livello dal menu principale. ciò che salvi allora resta separato."}},
    {TextLevelSelectLevel, {"level", "niveau", "level", "nivel", "livello"}},
    {TextDescLevelSelectLevel, {"the level to start, from its beginning. the story is opened up to it.", "le niveau à lancer, depuis son début. l'histoire est ouverte jusqu'à lui.", "das level, von seinem anfang an. die geschichte ist bis dorthin offen.", "el nivel que empezar, desde el principio. la historia se abre hasta él.", "il livello da iniziare, dal suo inizio. la storia è aperta fino a lì."}},
    {TextLevelSelectStart, {"start the level", "lancer le niveau", "level starten", "empezar el nivel", "inizia il livello"}},
    {TextLevelSelectFromMainMenu, {"from the main menu", "depuis le menu principal", "aus dem hauptmenü", "desde el menú principal", "dal menu principale"}},
    {TextDescLevelSelectFromMainMenu, {"quit to the main menu, then open the options there to start a level.", "quitte vers le menu principal, puis ouvre les options pour lancer un niveau.", "zum hauptmenü beenden und dort in den optionen ein level starten.", "sal al menú principal y abre allí las opciones para empezar un nivel.", "esci al menu principale e apri lì le opzioni per iniziare un livello."}},
    {TextSpeedrunTimer, {"speedrun timer", "chrono speedrun", "speedrun-timer", "cronómetro speedrun", "cronometro speedrun"}},
    {TextDescSpeedrunTimer, {"real time from new game, with each level's time as you finish it.", "temps réel depuis nouvelle partie, avec le temps de chaque niveau fini.", "echtzeit ab neues spiel, mit der zeit jedes beendeten levels.", "tiempo real desde nueva partida, con el tiempo de cada nivel al acabarlo.", "tempo reale da nuova partita, con il tempo di ogni livello finito."}},
    {TextDropShadow, {"drop shadow", "ombre au sol", "bodenschatten", "sombra bajo el personaje", "ombra a terra"}},
    {TextDescDropShadow, {"a round shadow under you while you're in the air, to see where you'll land.", "une ombre ronde sous toi en l'air, pour voir où tu vas atterrir.", "ein runder schatten unter dir in der luft, um zu sehen, wo du landest.", "una sombra redonda bajo ti en el aire, para ver dónde caerás.", "un'ombra rotonda sotto di te in aria, per vedere dove atterrerai."}},
    {TextShadowsOnCrates, {"shadows on crates", "ombres sur les caisses", "schatten auf kisten", "sombras en las cajas", "ombre sulle casse"}},
    {TextDescShadowsOnCrates, {"your shadow falls on the tops of crates too. from a level's next loading.", "ton ombre tombe aussi sur le dessus des caisses. au prochain chargement d'un niveau.", "dein schatten fällt auch auf kisten. ab dem nächsten laden eines levels.", "tu sombra cae también sobre las cajas. desde la próxima carga de un nivel.", "la tua ombra cade anche sopra le casse. dal prossimo caricamento di un livello."}},
    {TextFxaa, {"fxaa", "fxaa", "fxaa", "fxaa", "fxaa"}},
    {TextMenuHints, {"menu hints", "aides des menus", "menühinweise", "ayudas de menú", "suggerimenti menu"}},
    {TextDescMenuHints, {"the menus' button hints: the game's own, or a bar at the bottom.", "les aides des boutons : celles du jeu, ou une barre en bas.", "die tastenhinweise: die des spiels oder eine leiste unten.", "las ayudas de botones: las del juego o una barra abajo.", "i suggerimenti dei tasti: quelli del gioco o una barra in basso."}},
    {TextHintsOriginal, {"original", "originales", "original", "originales", "originali"}},
    {TextHintsBar, {"bar", "barre", "leiste", "barra", "barra"}},
    {TextShowFps, {"show fps", "afficher les fps", "fps anzeigen", "mostrar fps", "mostra fps"}},
    {TextDescShowFps, {"a frames per second counter in the top left corner.", "un compteur d'images par seconde en haut à gauche.", "ein bilder-pro-sekunde-zähler oben links.", "un contador de imágenes por segundo arriba a la izquierda.", "un contatore di fotogrammi al secondo in alto a sinistra."}},
    {TextPortable, {"portable mode", "mode portable", "portabler modus", "modo portátil", "modalità portatile"}},
    {TextDescPortable, {"settings, saves and mods in the game's own user folder, to carry it on a usb stick.", "réglages, sauvegardes et mods dans le dossier user du jeu, pour l'emporter sur une clé usb.", "einstellungen, spielstände und mods im user-ordner des spiels, für einen usb-stick.", "ajustes, partidas y mods en la carpeta user del juego, para llevarlo en un usb.", "impostazioni, salvataggi e mod nella cartella user del gioco, da portare su chiavetta usb."}},
    {TextPortableAskOn, {"copy your settings, saves and mods from appdata into the game's user folder and use them from there? the game closes when it's done.", "copier tes réglages, sauvegardes et mods d'appdata dans le dossier user du jeu et les utiliser de là ? le jeu se ferme une fois fini.", "deine einstellungen, spielstände und mods aus appdata in den user-ordner des spiels kopieren und von dort nutzen? danach schließt sich das spiel.", "¿copiar tus ajustes, partidas y mods de appdata a la carpeta user del juego y usarlos desde allí? el juego se cierra al terminar.", "copiare impostazioni, salvataggi e mod da appdata nella cartella user del gioco e usarli da lì? il gioco si chiude alla fine."}},
    {TextPortableAskOff, {"copy your settings, saves and mods from the game's user folder back to appdata and use them from there? the game closes when it's done.", "recopier tes réglages, sauvegardes et mods du dossier user du jeu dans appdata et les utiliser de là ? le jeu se ferme une fois fini.", "deine einstellungen, spielstände und mods aus dem user-ordner des spiels zurück nach appdata kopieren und von dort nutzen? danach schließt sich das spiel.", "¿copiar de nuevo tus ajustes, partidas y mods de la carpeta user del juego a appdata y usarlos desde allí? el juego se cierra al terminar.", "ricopiare impostazioni, salvataggi e mod dalla cartella user del gioco in appdata e usarli da lì? il gioco si chiude alla fine."}},
    {TextPortableCopying, {"copying…", "copie…", "kopieren…", "copiando…", "copia in corso…"}},
    {TextPortableDone, {"done. the game closes now: start it again.", "terminé. le jeu se ferme : relance-le.", "fertig. das spiel schließt sich jetzt: starte es neu.", "hecho. el juego se cierra ahora: vuelve a abrirlo.", "fatto. il gioco ora si chiude: riavvialo."}},
    {TextPortableBackup, {"what was already there is kept in:", "ce qui s'y trouvait déjà est gardé dans :", "was schon dort war, liegt jetzt in:", "lo que ya había se guarda en:", "quello che c'era già è conservato in:"}},
    {TextPortableRefused, {"the game's folder can't be written to (is it in program files?). move the game's folder somewhere else first.", "impossible d'écrire dans le dossier du jeu (est-il dans program files ?). déplace d'abord le dossier du jeu ailleurs.", "in den ordner des spiels kann nicht geschrieben werden (liegt er in program files?). verschiebe ihn zuerst woandershin.", "no se puede escribir en la carpeta del juego (¿está en program files?). mueve antes la carpeta del juego a otro sitio.", "nella cartella del gioco non si può scrivere (è in program files?). sposta prima la cartella del gioco altrove."}},
    {TextPortableFailed, {"the copy didn't work, so nothing changed: the reason is in log.txt.", "la copie n'a pas marché, rien n'a changé : la raison est dans log.txt.", "das kopieren hat nicht geklappt, nichts wurde geändert: der grund steht in log.txt.", "la copia no ha funcionado y nada ha cambiado: el motivo está en log.txt.", "la copia non è riuscita e non è cambiato nulla: il motivo è in log.txt."}},
    {TextOk, {"ok", "ok", "ok", "aceptar", "ok"}},
    {TextCreditsThanks, {"thanks", "remerciements", "dank", "agradecimientos", "ringraziamenti"}},
    {TextTabMods, {"mods", "mods", "mods", "mods", "mod"}},
    {TextModsImport, {"import mods", "importer des mods", "mods importieren", "importar mods", "importa mod"}},
    {TextDescModsImport, {"turns the zips and disc images in the mods folder into mods. from the next start.", "transforme les zip et images disque du dossier mods en mods. au prochain lancement.", "macht aus den zips und disc-images im mods-ordner mods. ab dem nächsten start.", "convierte los zip e imágenes de disco de la carpeta mods en mods. al volver a abrir el juego.", "trasforma zip e immagini disco della cartella mods in mod. dal prossimo avvio del gioco."}},
    {TextModsOpenFolder, {"open the mods folder", "ouvrir le dossier mods", "mods-ordner öffnen", "abrir la carpeta mods", "apri la cartella mods"}},
    {TextDescModsOpenFolder, {"put mods there. where two change a file, the first on wins. changes from the next start.", "mets-y les mods. si deux changent un fichier, le premier activé gagne. au prochain lancement.", "leg die mods dort ab. ändern zwei eine datei, gilt die erste aktive. ab dem nächsten start.", "pon ahí los mods. si dos cambian un archivo, gana el primero activo. al volver a abrir el juego.", "mettici le mod. se due cambiano un file, vince la prima attiva. dal prossimo avvio del gioco."}},
    {TextModsLoose, {"files in the mods folder", "fichiers du dossier mods", "dateien im mods-ordner", "archivos de la carpeta mods", "file nella cartella mods"}},
    {TextModsImporting, {"importing the mods: a disc image takes a minute or two.", "import des mods : une image disque prend une minute ou deux.", "mods werden importiert: ein disc-image dauert ein, zwei minuten.", "importando los mods: una imagen de disco tarda un minuto o dos.", "importazione delle mod: un'immagine disco richiede un minuto o due."}},
    {TextModsImported, {"done: the mods are used from the next start of the game.", "terminé : les mods sont utilisés au prochain lancement du jeu.", "fertig: die mods gelten ab dem nächsten start des spiels.", "hecho: los mods se usan al volver a abrir el juego.", "fatto: le mod si usano dal prossimo avvio del gioco."}},
    {TextModsImportFailed, {"something couldn't be imported: the reason is in log.txt.", "quelque chose n'a pas pu être importé : la raison est dans log.txt.", "etwas konnte nicht importiert werden: der grund steht in log.txt.", "algo no se pudo importar: el motivo está en log.txt.", "qualcosa non è stato importato: il motivo è in log.txt."}},
    {TextModRefused, {"can't be used:", "inutilisable :", "nicht nutzbar:", "no se puede usar:", "non utilizzabile:"}},
    {TextModProgram, {"its program changes don't apply.", "ses changements du programme ne s'appliquent pas.", "seine programmänderungen gelten nicht.", "sus cambios del programa no se aplican.", "le sue modifiche al programma non valgono."}},
};
const u32 g_NativeTextCount = sizeof(g_NativeTexts) / sizeof(g_NativeTexts[0]);
const char* const g_TextLanguages[TextLanguages] = {"English", "French", "German", "Spanish", "Italian"};

s32 TextLanguageIndex(const char* language)
{
    for (u32 i = 0; i < TextLanguages; i++)
    {
        if (language != nullptr && std::string_view(language) == g_TextLanguages[i])
        {
            return static_cast<s32>(i);
        }
    }

    return -1;
}

// UTF-8 to the game's Windows-1252 (the characters the table uses: Latin-1's and the right quote); unknown is false
bool ToGameText(const char* utf8, std::string* out)
{
    out->clear();
    bool known = true;
    const auto* at = reinterpret_cast<const unsigned char*>(utf8);
    while (*at != 0)
    {
        u32 code = *at++;
        if (code >= 0xC0 && code < 0xE0 && (*at & 0xC0) == 0x80)
        {
            code = ((code & 0x1F) << 6) | (*at++ & 0x3F);
        }
        else if (code >= 0xE0 && code < 0xF0 && (at[0] & 0xC0) == 0x80 && (at[1] & 0xC0) == 0x80)
        {
            code = ((code & 0x0F) << 12) | ((at[0] & 0x3F) << 6) | (at[1] & 0x3F);
            at += 2;
        }
        else if (code >= 0x80)
        {
            known = false;
            code = '?';
        }

        if (code == 0x2019)
        {
            code = 0x92;
        }
        else if (code == 0x2026)
        {
            code = 0x85;
        }
        else if (code >= 0x100 || (code >= 0x80 && code < 0xA0))
        {
            known = false;
            code = '?';
        }

        out->push_back(static_cast<char>(code));
    }

    return known;
}

namespace
{
// The converted strings (they stay: the game keeps pointers to them) and each file's and language's lines
std::deque<std::string> g_Strings;
std::map<std::pair<u32, std::string>, std::vector<const char*>> g_Lines;
// The lines before the button prompts' labels (the game's glyphs), and the labelled lines made from them (kept: the game keeps
// pointers to them)
std::map<std::pair<u32, std::string>, std::vector<const char*>> g_BaseLines;
std::map<std::string, std::string> g_Labelled;
std::string g_PromptLabels[PromptGlyphCount];
std::string g_MenuSelectLabel = "\\";
std::string g_MenuBackLabel = "^";
bool g_PromptLabelled = false;

// The menus' own lines, whose cross and triangle glyphs are the menus' select and back: the footer's "select \\" and "^ back",
// "next \\" and "^ cancel"
constexpr u32 MenuLines[] = {0x1C, 0x1D, 0xB2, 0xBB};

bool IsMenuLine(u32 file, size_t line)
{
    for (u32 menu : MenuLines)
    {
        if (file == CodeTexts && line == menu)
        {
            return true;
        }
    }

    return false;
}

// A line with the prompts' glyphs replaced by the labels (the line itself when it has none, or the labels are the glyphs)
const char* Labelled(const char* line, bool menu)
{
    if (!g_PromptLabelled || std::strpbrk(line, PromptGlyphs) == nullptr)
    {
        return line;
    }

    std::string key(line);
    key += menu ? '\x02' : '\x03';
    for (const std::string& label : g_PromptLabels)
    {
        key += '\x01';
        key += label;
    }

    key += '\x01' + g_MenuSelectLabel + '\x01' + g_MenuBackLabel;

    auto found = g_Labelled.find(key);
    if (found != g_Labelled.end())
    {
        return found->second.c_str();
    }

    // A label the same as the one just before it, only spaces between, is written once (the pages' "< { ¦", L1 and L2, on one
    // key: "< z")
    std::string out;
    std::string last;
    size_t afterLast = std::string::npos;
    for (const char* at = line; *at != '\0'; at++)
    {
        const char* glyph = std::strchr(PromptGlyphs, *at);
        if (glyph == nullptr)
        {
            out += *at;
            continue;
        }

        const std::string& label = menu && *at == '\\' ? g_MenuSelectLabel
                                   : menu && *at == '^' ? g_MenuBackLabel
                                                        : g_PromptLabels[glyph - PromptGlyphs];
        if (afterLast != std::string::npos && label == last && out.find_first_not_of(' ', afterLast) == std::string::npos)
        {
            out.resize(afterLast);
            continue;
        }

        out += label;
        last = label;
        afterLast = out.size();
    }

    return g_Labelled.emplace(key, out).first->second.c_str();
}

void LabelLines(u32 file, std::vector<const char*>& kept, const std::vector<const char*>& base)
{
    for (size_t line = 0; line < base.size() && line < kept.size(); line++)
    {
        kept[line] = Labelled(base[line], IsMenuLine(file, line));
    }
}
const char* const g_Blank = "";

const char* Keep(const char* utf8)
{
    g_Strings.emplace_back();
    ToGameText(utf8, &g_Strings.back());
    return g_Strings.back().c_str();
}
}

void ForgetTextLines()
{
    g_Lines.clear();
    g_BaseLines.clear();
}

void SetPromptLabels(const char* const labels[PromptGlyphCount], const char* menuSelect, const char* menuBack)
{
    g_PromptLabelled = false;
    g_MenuSelectLabel = menuSelect != nullptr ? menuSelect : "\\";
    g_MenuBackLabel = menuBack != nullptr ? menuBack : "^";
    if (g_MenuSelectLabel != "\\" || g_MenuBackLabel != "^")
    {
        g_PromptLabelled = true;
    }

    for (u32 glyph = 0; glyph < PromptGlyphCount; glyph++)
    {
        std::string label = labels != nullptr && labels[glyph] != nullptr ? labels[glyph] : std::string(1, PromptGlyphs[glyph]);
        if (label != std::string(1, PromptGlyphs[glyph]))
        {
            g_PromptLabelled = true;
        }

        g_PromptLabels[glyph] = label;
    }

    for (auto& [key, kept] : g_Lines)
    {
        LabelLines(key.first, kept, g_BaseLines[key]);
    }
}

const char** ApplyTextOverrides(u32 file, const char* language, const char** lines, u32 count)
{
    s32 index = TextLanguageIndex(language);
    std::vector<const char*>& kept = g_Lines[{file, language != nullptr ? language : ""}];
    u32 size = file == CodeTexts && count < NativeTextEnd ? static_cast<u32>(NativeTextEnd) : count;
    kept.assign(size, g_Blank);
    for (u32 line = 0; line < count; line++)
    {
        kept[line] = lines[line];
    }

    u32 replaced = 0;
    std::string original;
    for (const TextOverride& entry : g_TextOverrides)
    {
        if (entry.file != file || language == nullptr || std::string_view(entry.language) != language)
        {
            continue;
        }

        ToGameText(entry.original, &original);
        if (entry.line >= count || original != kept[entry.line])
        {
            Native::Log("PC text: %s's line %#x of text file %u isn't the disc's line it replaces, left as it is", language,
                        entry.line, file);
            continue;
        }

        kept[entry.line] = Keep(entry.replacement);
        replaced++;
    }

    if (file == CodeTexts)
    {
        for (const NativeTextLine& text : g_NativeTexts)
        {
            kept[text.text] = Keep(text.languages[index >= 0 ? index : 0]);
        }
    }

    if (replaced != 0)
    {
        Native::Log("PC text: %u of %s's lines of text file %u in the PC's wording", replaced, language, file);
    }

    // The button prompts as the input in use has them (SetPromptLabels)
    std::vector<const char*>& base = g_BaseLines[{file, language != nullptr ? language : ""}];
    base = kept;
    LabelLines(file, kept, base);
    return kept.data();
}
}
