# The PC wording of the console's texts

The native build replaces every line of the game's language files that talks about the PlayStation 2, its memory cards and slots, its controller ports, the DUALSHOCK 2 or the console itself with wording for a computer. The replacements are applied when the game splits a text file into its lines (`ReadTextFile`, `src/game/language.cpp`, under `TWIN_NATIVE`), keyed by file, language and line number, from the table in `src/platform/native/ui/pctext.cpp`. Each entry keeps the disc's original: a line that doesn't match it (another disc) is left alone and logged. The PS2 build is unchanged.

Line numbers are the game's text numbers (`GameText`: blank lines don't count). `~` is the game's line break, shown here as a break. Glyph codes (`\ ^ { } ¦ ¬ < > [ ]`) and the save code's marks `(xxx)` (the game's name) and `(x)` (the save's size) are kept; `@` only appeared in "dualshock@2", which is gone. Every character used is one the language's own files already use (so the game's font has it). `native/tools/dump_texts.py` dumps every line of every language from the disc image; `--selftest-ui` checks the table against the disc and the font's characters, and that no line still mentions the console.

## Counts

| Language | Code | AgentLab | Total |
|---|---|---|---|
| English | 25 | 4 | 29 |
| French | 25 | 4 | 29 |
| German | 25 | 4 | 29 |
| Spanish | 25 | 4 | 29 |
| Italian | 25 | 4 | 29 |
| **All** | 125 | 20 | 145 |

Formatting lines (0x28, 0x32, 0x38, 0x3E, 0x5F, 0xB4) and the "no card" lines (0x31, 0xB7) belong to screens the native build never reaches (its storage is always there and ready, and the boot check is gone: see NATIVE.md, "Saves"); they're reworded anyway so that no console wording is left in the files.

## Left as they are

Lines the search (`memory|card|ps2|playstation|dualshock|analog|console|slot|insert|format|port|reset|r1` and each language's words for them) finds that aren't about the console:

| File | Language | Line | Text | Why |
|---|---|---|---|---|
| AgentLab | English | 0x45 | use cortex to override security consoles. | the security consoles in the game's world |
| AgentLab | French | 0x30 | poursuis et abats les soucoupes volantes<br>avant que la porte anti-explosion ne s'ouvre ! | a door (porte, porta), not a port |
| AgentLab | French | 0x37 | frappe les boutons pour fermer les portes | a door (porte, porta), not a port |
| AgentLab | French | 0x45 | utilise cortex pour arrêter les<br>consoles de sécurité. | the security consoles in the game's world |
| AgentLab | German | 0x45 | benutz cortex, um die sicherheitskonsolen<br>auszutricksen. | the security consoles in the game's world |
| AgentLab | Spanish | 0x45 | haz que cortex invalide las<br>consolas de seguridad. | the security consoles in the game's world |
| AgentLab | Italian | 0x30 | insegui e abbatti tutti i dischi volanti<br>prima che si apra la porta di protezione! | a door (porte, porta), not a port |
| AgentLab | Italian | 0x37 | colpisci i pulsanti con il<br>corpo per chiudere le porte | a door (porte, porta), not a port |
| AgentLab | Italian | 0x45 | usa cortex per superare le<br>console di sicurezza. | the security consoles in the game's world |
| AgentLab | Italian | 0x61 | sali sulla mia barca omino peloso<br>ti porterò in quell'altro luogo. | "porterò" (I'll take you), not a port |

The disc error lines (0x21, 0x22: "please replace the crash twinsanity disc") talk about the game's disc, not the console; the native build reads the disc image and never shows them. The button glyphs (`\ ^ { } ¦ ¬ < > [ ]`) are drawn by the game's font as they are.

## The native menu texts

The graphic options' resolution setting (NATIVE.md) adds texts of its own after the file's lines, at text numbers 0x100 on (`NativeUi::NativeText`):

| Text | English | French | German | Spanish | Italian |
|---|---|---|---|---|---|
| 0x100 | resolution | résolution | auflösung | resolución | risoluzione |
| 0x101 | display | affichage | anzeige | pantalla | schermo |
| 0x102 | 1x | 1x | 1x | 1x | 1x |
| 0x103 | 2x | 2x | 2x | 2x | 2x |
| 0x104 | 3x | 3x | 3x | 3x | 3x |
| 0x105 | 4x | 4x | 4x | 4x | 4x |
| 0x106 | 960x720 | 960x720 | 960x720 | 960x720 | 960x720 |
| 0x107 | 1280x960 | 1280x960 | 1280x960 | 1280x960 | 1280x960 |
| 0x108 | 1600x1200 | 1600x1200 | 1600x1200 | 1600x1200 | 1600x1200 |
| 0x109 | 1920x1440 | 1920x1440 | 1920x1440 | 1920x1440 | 1920x1440 |
| 0x10A | 1280x720 | 1280x720 | 1280x720 | 1280x720 | 1280x720 |
| 0x10B | 1920x1080 | 1920x1080 | 1920x1080 | 1920x1080 | 1920x1080 |
| 0x10C | fullscreen | plein écran | vollbild | completa | intero |
| 0x10D | filtering | filtrage | filter | filtrado | filtro |
| 0x10E | smooth | lisse | weich | suave | morbido |
| 0x10F | sharp | net | scharf | nítido | nitido |
| 0x110 | scanlines | lignes crt | crt-zeilen | líneas crt | linee crt |
| 0x111 | voice volume | volume des voix | stimmen-lautstärke | volumen de las voces | volume voci |
| 0x112 | skip cutscenes | passer les scènes | szenen überspringen | saltar escenas | salta scene |
| 0x113 | fast loading | chargement rapide | schnelles laden | carga rápida | caricamento rapido |
| 0x114 | pause in background | pause en arrière-plan | pause im hintergrund | pausa en segundo plano | pausa in secondo piano |
| 0x115 | detailed log | journal détaillé | ausführliches protokoll | registro detallado | log dettagliato |
| 0x116 | refresh rate | fréquence | bildfrequenz | frecuencia | frequenza |
| 0x117 | 50 hz | 50 hz | 50 hz | 50 hz | 50 hz |
| 0x118 | 60 hz | 60 hz | 60 hz | 60 hz | 60 hz |
| 0x119 | 120 hz | 120 hz | 120 hz | 120 hz | 120 hz |
| 0x11A | 144 hz | 144 hz | 144 hz | 144 hz | 144 hz |
| 0x11B | unlimited | illimitée | unbegrenzt | ilimitada | illimitata |
| 0x11C | text language | langue des textes | textsprache | idioma de los textos | lingua dei testi |
| 0x11D | as windows | comme windows | wie windows | como windows | come windows |
| 0x11E | english | english | english | english | english |
| 0x11F | français | français | français | français | français |
| 0x120 | deutsch | deutsch | deutsch | deutsch | deutsch |
| 0x121 | italiano | italiano | italiano | italiano | italiano |
| 0x122 | español | español | español | español | español |
| 0x123 | bug fixes | corrections | fehlerkorrekturen | correcciones | correzioni |
| 0x124 | controls | commandes | steuerung | controles | comandi |
| 0x125 | reset controls | réinitialiser | zurücksetzen | restablecer | ripristina |
| 0x126 | press a key or button (esc to cancel) | appuie sur une touche (échap pour annuler) | taste drücken (esc bricht ab) | pulsa una tecla (esc para cancelar) | premi un tasto (esc per annullare) |
| 0x127 | l3 | l3 | l3 | l3 | l3 |
| 0x128 | r3 | r3 | r3 | r3 | r3 |
| 0x129 | start | start | start | start | start |
| 0x12A | select | select | select | select | select |
| 0x12B | up | haut | oben | arriba | su |
| 0x12C | down | bas | unten | abajo | giù |
| 0x12D | left | gauche | links | izquierda | sinistra |
| 0x12E | right | droite | rechts | derecha | destra |
| 0x12F | stick | joystick | stick | joystick | levetta |
| 0x130 | jump | sauter | springen | saltar | saltare |
| 0x131 | crouch / slide | accroupi / glisser | ducken / rutschen | agacharse / deslizar | abbassarsi / scivolare |
| 0x132 | status | statut | status | estado | stato |
| 0x133 | spin | toupie | drehen | girar | girare |
| 0x134 | layout | boutons | belegung | botones | tasti |
| 0x135 | nintendo | nintendo | nintendo | nintendo | nintendo |
| 0x136 | controller | manette | controller | mando | controller |
| 0x137 | auto | auto | auto | auto | auto |
| 0x138 | keyboard | clavier | tastatur | teclado | tastiera |
| 0x139 | xbox | xbox | xbox | xbox | xbox |
| 0x13A | playstation | playstation | playstation | playstation | playstation |
| 0x13B | switch | switch | switch | switch | switch |
| 0x13C | mouse | souris | maus | ratón | mouse |
| 0x13D | display | affichage | anzeige | pantalla | schermo |
| 0x13E | audio | audio | audio | audio | audio |
| 0x13F | gameplay | jeu | spiel | juego | gioco |
| 0x140 | display mode | mode d'affichage | anzeigemodus | modo de pantalla | modalità schermo |
| 0x141 | texture upscale | textures agrandies | texturen skalieren | escalar texturas | scala texture |
| 0x142 | texture pack | pack de textures | texturpaket | pack de texturas | pacchetto texture |
| 0x143 | anti-aliasing | anticrénelage | kantenglättung | antialiasing | antialiasing |
| 0x144 | v-sync | v-sync | v-sync | v-sync | v-sync |
| 0x145 | action | action | aktion | acción | azione |
| 0x146 | key | touche | taste | tecla | tasto |
| 0x147 | alt key | autre | alternativ | alternativa | alternativo |
| 0x148 | controller | manette | controller | mando | controller |
| 0x149 | move up | avancer | vorwärts | avanzar | avanti |
| 0x14A | move down | reculer | rückwärts | retroceder | indietro |
| 0x14B | move left | gauche | links | izquierda | sinistra |
| 0x14C | move right | droite | rechts | derecha | destra |
| 0x14D | camera left | caméra gauche | kamera links | cámara izquierda | telecamera sin. |
| 0x14E | camera right | caméra droite | kamera rechts | cámara derecha | telecamera des. |
| 0x14F | strafe left | pas latéral g. | seitlich links | lateral izq. | laterale sin. |
| 0x150 | strafe right | pas latéral d. | seitlich rechts | lateral der. | laterale des. |
| 0x151 | pause | pause | pause | pausa | pausa |
| 0x152 | reset keyboard | clavier par défaut | tastatur zurücksetzen | restablecer teclado | ripristina tastiera |
| 0x153 | reset controller | manette par défaut | controller zurücksetzen | restablecer mando | ripristina controller |
| 0x154 | reset to defaults | valeurs par défaut | standard | valores por defecto | valori predefiniti |
| 0x155 | a window of a set size, or fullscreen. | une fenêtre d'une taille donnée, ou plein écran. | ein fenster fester größe oder vollbild. | una ventana de tamaño fijo o pantalla completa. | una finestra di dimensione fissa o schermo intero. |
| 0x156 | sharper 3d, smoother edges. 3x and up are slower: 6x and 8x need a strong graphics card. | 3d plus net, bords lissés. 3x et plus sont plus lents : 6x et 8x demandent une carte graphique puissante. | schärferes 3d, glatte kanten. ab 3x langsamer: 6x und 8x brauchen eine starke grafikkarte. | 3d más nítido, bordes suaves. desde 3x es más lento: 6x y 8x piden una gráfica potente. | 3d più nitido, bordi lisci. da 3x è più lento: 6x e 8x chiedono una scheda video potente. |
| 0x157 | draw the game for a 16:9 screen. | jeu adapté aux écrans 16:9. | das spiel für 16:9-bildschirme. | el juego para pantallas 16:9. | il gioco per schermi 16:9. |
| 0x158 | smooth or sharp pixels when the picture is scaled. | pixels lissés ou nets à l'agrandissement. | weiche oder scharfe pixel beim skalieren. | píxeles suaves o nítidos al escalar. | pixel morbidi o nitidi nel ridimensionamento. |
| 0x159 | smooth the game's textures at a larger size. | textures lissées en plus grand. | texturen größer und glatter. | texturas suavizadas a mayor tamaño. | texture levigate più grandi. |
| 0x15A | use the textures of a texture pack. | utiliser un pack de textures. | texturen eines texturpakets nutzen. | usar un pack de texturas. | usa un pacchetto di texture. |
| 0x15B | fxaa and smaa smooth the edges, smaa better. ssaa draws the picture 2 or 4 times larger: slower. | fxaa et smaa lissent les bords, smaa mieux. ssaa dessine l'image 2 ou 4 fois plus grande : plus lent. | fxaa und smaa glätten kanten, smaa besser. ssaa zeichnet das bild 2- oder 4-mal größer: langsamer. | fxaa y smaa suavizan bordes, smaa mejor. ssaa dibuja la imagen 2 o 4 veces mayor: más lento. | fxaa e smaa levigano i bordi, smaa meglio. ssaa disegna l'immagine 2 o 4 volte più grande: più lento. |
| 0x15C | wait for the screen's refresh: no tearing. | synchronisé à l'écran: pas de déchirure. | auf den bildschirm warten: kein tearing. | sincronizar con la pantalla: sin cortes. | sincronizza con lo schermo: niente tearing. |
| 0x15D | the look of an old crt television. | l'aspect d'une vieille télévision. | wie ein alter röhrenfernseher. | el aspecto de una tele antigua. | l'aspetto di un vecchio televisore. |
| 0x15E | 50 hz is the original timing. above 60 needs a fast pc. | 50 hz est la cadence d'origine. au-delà de 60, un pc rapide. | 50 hz ist das original. über 60 braucht einen schnellen pc. | 50 hz es el ritmo original. más de 60 pide un pc rápido. | 50 hz è la frequenza originale. oltre 60 serve un pc veloce. |
| 0x15F | the texts and title pictures, and the voices unless chosen below. from the next start. | les textes et les titres, et les voix sauf choix plus bas. au prochain lancement. | texte und titelbilder, die stimmen auch, außer unten gewählt. ab dem nächsten start. | los textos y los títulos, y las voces salvo que elijas abajo. al volver a abrir el juego. | i testi e i titoli, e le voci se non scelte sotto. dal prossimo avvio del gioco. |
| 0x160 | move the picture on the screen. | déplacer l'image à l'écran. | das bild verschieben. | mover la imagen en pantalla. | sposta l'immagine sullo schermo. |
| 0x161 | the music's volume. | le volume de la musique. | die lautstärke der musik. | el volumen de la música. | il volume della musica. |
| 0x162 | the sound effects' volume. | le volume des sons. | die lautstärke der effekte. | el volumen de los efectos. | il volume degli effetti. |
| 0x163 | the characters' voices' volume. | le volume des voix. | die lautstärke der stimmen. | el volumen de las voces. | il volume delle voci. |
| 0x164 | mono, stereo or dolby pro logic ii. | mono, stéréo ou dolby pro logic ii. | mono, stereo oder dolby pro logic ii. | mono, estéreo o dolby pro logic ii. | mono, stereo o dolby pro logic ii. |
| 0x165 | the button prompts: auto follows what you use. | les boutons affichés: auto suit ce que tu utilises. | die tastensymbole: auto folgt dem gerät. | los botones: auto sigue lo que usas. | i tasti mostrati: auto segue ciò che usi. |
| 0x166 | nintendo: a selects and b goes back in menus. | nintendo: a valide et b revient dans les menus. | nintendo: a wählt, b zurück in menüs. | nintendo: a elige y b vuelve en los menús. | nintendo: a seleziona e b torna nei menu. |
| 0x167 | the controller's vibration. | les vibrations de la manette. | die vibration des controllers. | la vibración del mando. | la vibrazione del controller. |
| 0x168 | use the mouse in menus. | utiliser la souris dans les menus. | die maus in menüs nutzen. | usar el ratón en los menús. | usa il mouse nei menu. |
| 0x169 | select a key or button to change it. | choisis une touche pour la changer. | taste wählen, um sie zu ändern. | elige una tecla para cambiarla. | scegli un tasto per cambiarlo. |
| 0x16A | the default keys again. | les touches par défaut. | die standardtasten. | las teclas por defecto. | i tasti predefiniti. |
| 0x16B | the default buttons again. | les boutons par défaut. | die standardknöpfe. | los botones por defecto. | i pulsanti predefiniti. |
| 0x16C | hold any button to skip cutscenes and movies. | maintiens une touche pour passer les scènes. | beliebige taste halten, um szenen zu überspringen. | mantén cualquier botón para saltar escenas. | tieni premuto un tasto per saltare le scene. |
| 0x16D | load levels faster. | chargement plus rapide des niveaux. | level schneller laden. | cargar niveles más rápido. | caricamento più rapido dei livelli. |
| 0x16E | pause when the window is in the background. | pause quand la fenêtre est en arrière-plan. | pause im hintergrund. | pausa en segundo plano. | pausa quando la finestra è in secondo piano. |
| 0x16F | write more of what happens to log.txt, for bug reports. | plus de détails dans log.txt, pour signaler un bug. | mehr details in log.txt, für fehlerberichte. | más detalles en log.txt, para informar de errores. | più dettagli in log.txt, per segnalare i bug. |
| 0x170 | fixes from the community patch and the decomp's bug list. off plays exactly like the original. | corrections du patch de la communauté et de la liste de la décompilation. désactivé : comme l'original. | korrekturen aus dem community-patch und der fehlerliste der dekompilierung. aus: wie das original. | arreglos del parche de la comunidad y de la lista de la descompilación. desactivado: como el original. | correzioni della patch della comunità e dell'elenco della decompilazione. disattivato: come l'originale. |
| 0x171 | this tab's settings as they were at first. | les réglages de cet onglet par défaut. | die einstellungen dieser seite zurücksetzen. | los ajustes de esta pestaña por defecto. | le impostazioni di questa scheda predefinite. |
| 0x172 | tabs | onglets | reiter | pestañas | schede |
| 0x173 | accessibility | accessibilité | barrierefreiheit | accesibilidad | accessibilità |
| 0x174 | window size | taille de fenêtre | fenstergröße | tamaño de ventana | dimensione finestra |
| 0x175 | window | fenêtre | fenster | ventana | finestra |
| 0x176 | keyboard | clavier | tastatur | teclado | tastiera |
| 0x177 | controller | manette | controller | mando | controller |
| 0x178 | button | bouton | knopf | botón | pulsante |
| 0x179 | alt button | autre | alternativ | alternativo | alternativo |
| 0x17A | camera shake | tremblement caméra | kamerawackeln | vibración de cámara | tremolio telecamera |
| 0x17B | invert camera x | inverser caméra x | kamera x umkehren | invertir cámara x | inverti telecamera x |
| 0x17C | invert camera y | inverser caméra y | kamera y umkehren | invertir cámara y | inverti telecamera y |
| 0x17D | camera speed | vitesse caméra | kamerageschwindigkeit | velocidad de cámara | velocità telecamera |
| 0x17E | stick dead zone | zone morte | stick-totzone | zona muerta | zona morta |
| 0x17F | mute in background | muet en arrière-plan | stumm im hintergrund | silencio en segundo plano | muto in secondo piano |
| 0x180 | shoulder left | gâchette gauche | schulter links | gatillo izquierdo | dorsale sinistro |
| 0x181 | shoulder right | gâchette droite | schulter rechts | gatillo derecho | dorsale destro |
| 0x182 | walk (toggle) | marcher (bascule) | gehen (umschalten) | caminar (alternar) | cammina (attiva) |
| 0x183 | camera up | caméra haut | kamera oben | cámara arriba | telecamera su |
| 0x184 | camera down | caméra bas | kamera unten | cámara abajo | telecamera giù |
| 0x185 | mouse left | souris gauche | maus links | ratón izq. | mouse sin. |
| 0x186 | mouse right | souris droite | maus rechts | ratón der. | mouse des. |
| 0x187 | keep these display settings? | garder cet affichage ? | diese anzeige behalten? | ¿mantener esta pantalla? | mantenere questo schermo? |
| 0x188 | clear | effacer | löschen | borrar | cancella |
| 0x189 | the window's size. | la taille de la fenêtre. | die größe des fensters. | el tamaño de la ventana. | la dimensione della finestra. |
| 0x18A | the keys for each action. | les touches de chaque action. | die tasten jeder aktion. | las teclas de cada acción. | i tasti di ogni azione. |
| 0x18B | the controller's buttons for each action. | les boutons de chaque action. | die knöpfe jeder aktion. | los botones de cada acción. | i pulsanti di ogni azione. |
| 0x18C | the camera shakes with explosions and impacts. | la caméra tremble aux explosions. | die kamera wackelt bei explosionen. | la cámara tiembla con explosiones. | la telecamera trema con le esplosioni. |
| 0x18D | turn the camera the other way left and right. | caméra inversée à gauche et à droite. | kamera links und rechts umgekehrt. | cámara invertida a izquierda y derecha. | telecamera invertita a sinistra e destra. |
| 0x18E | turn the camera the other way up and down. | caméra inversée en haut et en bas. | kamera oben und unten umgekehrt. | cámara invertida arriba y abajo. | telecamera invertita su e giù. |
| 0x18F | how fast the camera turns. | la vitesse de la caméra. | wie schnell die kamera dreht. | la velocidad de la cámara. | la velocità della telecamera. |
| 0x190 | how far a stick moves before it counts. | la course du joystick ignorée. | wie weit ein stick ohne wirkung geht. | el recorrido ignorado del joystick. | la corsa ignorata della levetta. |
| 0x191 | no sound when the window is in the background. | pas de son en arrière-plan. | kein ton im hintergrund. | sin sonido en segundo plano. | nessun suono in secondo piano. |
| 0x192 | next | suivant | weiter | siguiente | avanti |
| 0x193 | position | position | position | posición | posizione |
| 0x194 | restart checkpoint | recommencer | neu starten | reiniciar | ricomincia |
| 0x195 | level select | choix du niveau | levelauswahl | elegir nivel | scelta livello |
| 0x196 | left stick up | stick g. haut | linker stick hoch | stick izq. arriba | levetta sin. su |
| 0x197 | left stick down | stick g. bas | linker stick runter | stick izq. abajo | levetta sin. giù |
| 0x198 | left stick left | stick g. gauche | linker stick links | stick izq. izquierda | levetta sin. sinistra |
| 0x199 | left stick right | stick g. droite | linker stick rechts | stick izq. derecha | levetta sin. destra |
| 0x19A | right stick up | stick d. haut | rechter stick hoch | stick der. arriba | levetta des. su |
| 0x19B | right stick down | stick d. bas | rechter stick runter | stick der. abajo | levetta des. giù |
| 0x19C | right stick left | stick d. gauche | rechter stick links | stick der. izquierda | levetta des. sinistra |
| 0x19D | right stick right | stick d. droite | rechter stick rechts | stick der. derecha | levetta des. destra |
| 0x19E | mouse middle | souris milieu | maus mitte | ratón central | mouse centr. |
| 0x19F | mouse 4 | souris 4 | maus 4 | ratón 4 | mouse 4 |
| 0x1A0 | mouse 5 | souris 5 | maus 5 | ratón 5 | mouse 5 |
| 0x1A1 | wheel up | molette haut | mausrad hoch | rueda arriba | rotella su |
| 0x1A2 | wheel down | molette bas | mausrad runter | rueda abajo | rotella giù |
| 0x1A3 | mouse look | visée souris | mausblick | vista con ratón | visuale mouse |
| 0x1A4 | mouse sensitivity | sensibilité souris | mausempfindlichkeit | sensibilidad del ratón | sensibilità mouse |
| 0x1A5 | invert mouse y | inverser souris y | maus y umkehren | invertir ratón y | inverti mouse y |
| 0x1A6 | move the camera with the mouse while playing. | la souris tourne la caméra en jeu. | die maus dreht die kamera im spiel. | el ratón mueve la cámara al jugar. | il mouse muove la telecamera in gioco. |
| 0x1A7 | how fast the mouse turns the camera. | la vitesse de la caméra à la souris. | wie schnell die maus die kamera dreht. | lo rápido que el ratón gira la cámara. | quanto velocemente il mouse gira la telecamera. |
| 0x1A8 | moving the mouse up looks down. | souris vers le haut : la caméra baisse. | maus hoch blickt nach unten. | ratón arriba mira hacia abajo. | mouse in alto guarda in basso. |
| 0x1A9 | fxaa | fxaa | fxaa | fxaa | fxaa |
| 0x1AA | menu hints | aides des menus | menühinweise | ayudas de menú | suggerimenti menu |
| 0x1AB | the menus' button hints: the game's own, or a bar at the bottom. | les aides des boutons : celles du jeu, ou une barre en bas. | die tastenhinweise: die des spiels oder eine leiste unten. | las ayudas de botones: las del juego o una barra abajo. | i suggerimenti dei tasti: quelli del gioco o una barra in basso. |
| 0x1AC | original | originales | original | originales | originali |
| 0x1AD | bar | barre | leiste | barra | barra |
| 0x1AE | show fps | afficher les fps | fps anzeigen | mostrar fps | mostra fps |
| 0x1AF | a frames per second counter in the top left corner. | un compteur d'images par seconde en haut à gauche. | ein bilder-pro-sekunde-zähler oben links. | un contador de imágenes por segundo arriba a la izquierda. | un contatore di fotogrammi al secondo in alto a sinistra. |
| 0x1B0 | portable mode | mode portable | portabler modus | modo portátil | modalità portatile |
| 0x1B1 | settings, saves and mods in the game's own user folder, to carry it on a usb stick. | réglages, sauvegardes et mods dans le dossier user du jeu, pour l'emporter sur une clé usb. | einstellungen, spielstände und mods im user-ordner des spiels, für einen usb-stick. | ajustes, partidas y mods en la carpeta user del juego, para llevarlo en un usb. | impostazioni, salvataggi e mod nella cartella user del gioco, da portare su chiavetta usb. |
| 0x1B2 | copy your settings, saves and mods from appdata into the game's user folder and use them from there? the game closes when it's done. | copier tes réglages, sauvegardes et mods d'appdata dans le dossier user du jeu et les utiliser de là ? le jeu se ferme une fois fini. | deine einstellungen, spielstände und mods aus appdata in den user-ordner des spiels kopieren und von dort nutzen? danach schließt sich das spiel. | ¿copiar tus ajustes, partidas y mods de appdata a la carpeta user del juego y usarlos desde allí? el juego se cierra al terminar. | copiare impostazioni, salvataggi e mod da appdata nella cartella user del gioco e usarli da lì? il gioco si chiude alla fine. |
| 0x1B3 | copy your settings, saves and mods from the game's user folder back to appdata and use them from there? the game closes when it's done. | recopier tes réglages, sauvegardes et mods du dossier user du jeu dans appdata et les utiliser de là ? le jeu se ferme une fois fini. | deine einstellungen, spielstände und mods aus dem user-ordner des spiels zurück nach appdata kopieren und von dort nutzen? danach schließt sich das spiel. | ¿copiar de nuevo tus ajustes, partidas y mods de la carpeta user del juego a appdata y usarlos desde allí? el juego se cierra al terminar. | ricopiare impostazioni, salvataggi e mod dalla cartella user del gioco in appdata e usarli da lì? il gioco si chiude alla fine. |
| 0x1B4 | copying… | copie… | kopieren… | copiando… | copia in corso… |
| 0x1B5 | done. the game closes now: start it again. | terminé. le jeu se ferme : relance-le. | fertig. das spiel schließt sich jetzt: starte es neu. | hecho. el juego se cierra ahora: vuelve a abrirlo. | fatto. il gioco ora si chiude: riavvialo. |
| 0x1B6 | what was already there is kept in: | ce qui s'y trouvait déjà est gardé dans : | was schon dort war, liegt jetzt in: | lo que ya había se guarda en: | quello che c'era già è conservato in: |
| 0x1B7 | the game's folder can't be written to (is it in program files?). move the game's folder somewhere else first. | impossible d'écrire dans le dossier du jeu (est-il dans program files ?). déplace d'abord le dossier du jeu ailleurs. | in den ordner des spiels kann nicht geschrieben werden (liegt er in program files?). verschiebe ihn zuerst woandershin. | no se puede escribir en la carpeta del juego (¿está en program files?). mueve antes la carpeta del juego a otro sitio. | nella cartella del gioco non si può scrivere (è in program files?). sposta prima la cartella del gioco altrove. |
| 0x1B8 | the copy didn't work, so nothing changed: the reason is in log.txt. | la copie n'a pas marché, rien n'a changé : la raison est dans log.txt. | das kopieren hat nicht geklappt, nichts wurde geändert: der grund steht in log.txt. | la copia no ha funcionado y nada ha cambiado: el motivo está en log.txt. | la copia non è riuscita e non è cambiato nulla: il motivo è in log.txt. |
| 0x1B9 | ok | ok | ok | aceptar | ok |
| 0x1BA | dithering | tramage | dithering | tramado | dithering |
| 0x1BB | the ps2 shows the picture in 16-bit colour with a fine pattern. off keeps its full colour. | la ps2 affiche l'image en couleurs 16 bits avec une fine trame. désactivé garde toutes ses couleurs. | die ps2 zeigt das bild in 16-bit-farben mit feinem muster. aus behält die vollen farben. | la ps2 muestra la imagen en color de 16 bits con una trama fina. desactivado mantiene todo el color. | la ps2 mostra l'immagine a colori a 16 bit con una trama fine. disattivato mantiene tutti i colori. |
| 0x1BC | as the ps2 | comme la ps2 | wie die ps2 | como la ps2 | come la ps2 |
| 0x1BD | ssaa 2x | ssaa 2x | ssaa 2x | ssaa 2x | ssaa 2x |
| 0x1BE | ssaa 4x | ssaa 4x | ssaa 4x | ssaa 4x | ssaa 4x |
| 0x1BF | draw distance | distance d'affichage | sichtweite | distancia de dibujado | distanza di disegno |
| 0x1C0 | how far things and their detailed models are drawn. farther is slower on older pcs. | jusqu'où les objets et leurs détails sont affichés. plus loin ralentit les vieux pc. | wie weit objekte und ihre details gezeichnet werden. weiter bremst ältere pcs. | hasta dónde se ven los objetos y sus detalles. más lejos es más lento en pcs antiguos. | fin dove si vedono gli oggetti e i loro dettagli. più lontano rallenta i pc vecchi. |
| 0x1C1 | short | courte | kurz | corta | corta |
| 0x1C2 | original | d'origine | original | original | originale |
| 0x1C3 | medium | moyenne | mittel | media | media |
| 0x1C4 | long | longue | weit | larga | lunga |
| 0x1C5 | quit to desktop | quitter vers le bureau | zum desktop beenden | salir al escritorio | esci al desktop |
| 0x1C6 | closes the game as the window's close button does. progress not saved is lost. | ferme le jeu comme le bouton de la fenêtre. la progression non sauvegardée est perdue. | schließt das spiel wie der knopf des fensters. nicht gespeicherter fortschritt geht verloren. | cierra el juego como el botón de la ventana. el progreso no guardado se pierde. | chiude il gioco come il pulsante della finestra. i progressi non salvati vanno persi. |
| 0x1C7 | quit the game now? progress not saved is lost. | quitter le jeu maintenant ? la progression non sauvegardée est perdue. | das spiel jetzt beenden? nicht gespeicherter fortschritt geht verloren. | ¿salir del juego ahora? el progreso no guardado se pierde. | uscire dal gioco adesso? i progressi non salvati vanno persi. |
| 0x1C8 | credits | crédits | mitwirkende | créditos | riconoscimenti |
| 0x1C9 | the people who made this port and the work it stands on. | les personnes qui ont fait ce portage et le travail sur lequel il repose. | die leute hinter dieser portierung und der arbeit, auf der sie aufbaut. | las personas que hicieron este port y el trabajo en el que se basa. | le persone che hanno fatto questo port e il lavoro su cui si basa. |
| 0x1CA | the windows port | le portage windows | die windows-portierung | el port para windows | il port per windows |
| 0x1CB | the decompilation | la décompilation | die dekompilierung | la descompilación | la decompilazione |
| 0x1CC | the mac port | le portage mac | die mac-portierung | el port para mac | il port per mac |
| 0x1CD | crash twinsanity improved's fixes and 21:9 | les corrections et le 21:9 de crash twinsanity improved | korrekturen und 21:9 aus crash twinsanity improved | las correcciones y el 21:9 de crash twinsanity improved | le correzioni e il 21:9 di crash twinsanity improved |
| 0x1CE | the linux build | la version linux | die linux-version | la versión para linux | la versione per linux |
| 0x1CF | tests and reports | tests et rapports | tests und berichte | pruebas e informes | prove e segnalazioni |
| 0x1D0 | the hd texture pack | le pack de textures hd | das hd-texturpaket | el pack de texturas hd | il pacchetto di texture hd |
| 0x1D1 | code from twinsanityevolved | du code de twinsanityevolved | code aus twinsanityevolved | código de twinsanityevolved | codice da twinsanityevolved |
| 0x1D2 | voice language | langue des voix | sprache der stimmen | idioma de las voces | lingua delle voci |
| 0x1D3 | the voices and the movies' sound: as the texts, or another. from the next start. | les voix et le son des films : comme les textes, ou une autre. au prochain lancement. | stimmen und ton der filme: wie die texte oder eine andere. ab dem nächsten start. | las voces y el sonido de los vídeos: como los textos u otro. al volver a abrir el juego. | le voci e l'audio dei filmati: come i testi, o un'altra. dal prossimo avvio del gioco. |
| 0x1D4 | as the texts | comme les textes | wie die texte | como los textos | come i testi |
| 0x1D5 | português | português | português | português | português |
| 0x1D6 | nederlands | nederlands | nederlands | nederlands | nederlands |
| 0x1D7 | smaa | smaa | smaa | smaa | smaa |
| 0x1D8 | anisotropic filtering | filtrage anisotrope | anisotrope filterung | filtrado anisotrópico | filtro anisotropico |
| 0x1D9 | textures seen at an angle (floors, paths) sharper. off is the ps2's look. | textures vues de biais (sols, chemins) plus nettes. désactivé : comme la ps2. | texturen im schrägen winkel (böden, wege) schärfer. aus: wie die ps2. | texturas vistas en ángulo (suelos, caminos) más nítidas. desactivado: como la ps2. | texture viste di sbieco (pavimenti, sentieri) più nitide. disattivato: come la ps2. |
| 0x1DA | check for updates | vérifier les mises à jour | nach updates suchen | buscar actualizaciones | controlla aggiornamenti |
| 0x1DB | at each start, asks github for the latest version. it never downloads anything. | à chaque lancement, demande à github la dernière version. rien n'est téléchargé. | fragt bei jedem start github nach der neuesten version. lädt nie etwas herunter. | en cada inicio, pregunta a github por la última versión. nunca descarga nada. | a ogni avvio chiede a github l'ultima versione. non scarica mai niente. |
| 0x1DC | new version's page | page de la nouvelle version | seite der neuen version | página de la nueva versión | pagina della nuova versione |
| 0x1DD | opens the new version's page on github in your browser. | ouvre la page de la nouvelle version sur github dans ton navigateur. | öffnet die seite der neuen version auf github im browser. | abre la página de la nueva versión en github en tu navegador. | apre la pagina della nuova versione su github nel browser. |
| 0x1DE | version x.y.z is out: options, gameplay. | la version x.y.z est sortie : options, jeu. | version x.y.z ist da: optionen, spiel. | ya está la versión x.y.z: opciones, juego. | è uscita la versione x.y.z: opzioni, gioco. |
| 0x1DF | infinite lives | vies infinies | unendliche leben | vidas infinitas | vite infinite |
| 0x1E0 | a death costs no life: the count stays where it is. off plays as the original. | une mort ne fait perdre aucune vie : le compteur ne bouge pas. désactivé : comme l'original. | ein tod kostet kein leben: der zähler bleibt. aus: wie das original. | morir no cuesta vidas: el contador no cambia. desactivado: como el original. | morire non costa vite: il conteggio resta. disattivato: come l'originale. |
| 0x1E1 | stronger aku aku | aku aku plus fort | stärkerer aku aku | aku aku más fuerte | aku aku più forte |
| 0x1E2 | aku aku's masks also take tnt and nitro blasts, as in the earlier crash games. | les masques d'aku aku encaissent aussi tnt et nitro, comme dans les anciens crash. | aku akus masken halten auch tnt und nitro aus, wie in den früheren crash-spielen. | las máscaras de aku aku aguantan también tnt y nitro, como en los crash anteriores. | le maschere di aku aku reggono anche tnt e nitro, come nei crash precedenti. |
| 0x1E3 | slide jump | saut glissé | rutschsprung | salto deslizante | salto in scivolata |
| 0x1E4 | the long jump from a slide: at any moment of it, or easier (a later press, steered in the air). | le saut long depuis une glissade : à tout moment, ou plus facile (plus tard, dirigé en l'air). | der weitsprung aus dem rutschen: jederzeit oder leichter (später drücken, in der luft lenken). | el salto largo al deslizarte: en cualquier momento, o más fácil (más tarde, dirigido en el aire). | il salto lungo dalla scivolata: in qualunque momento, o più facile (più tardi, guidato in aria). |
| 0x1E5 | any moment | à tout moment | jederzeit | en cualquier momento | in qualunque momento |
| 0x1E6 | easier | plus facile | leichter | más fácil | più facile |
| 0x1E7 | start any level from the main menu. what you save then is kept apart from your saves. | lancer n'importe quel niveau depuis le menu principal. ses sauvegardes restent à part. | jedes level aus dem hauptmenü starten. was du dann speicherst, bleibt getrennt. | empezar cualquier nivel desde el menú principal. lo que guardes entonces queda aparte. | inizia qualsiasi livello dal menu principale. ciò che salvi allora resta separato. |
| 0x1E8 | level | niveau | level | nivel | livello |
| 0x1E9 | the level to start, from its beginning. the story is opened up to it. | le niveau à lancer, depuis son début. l'histoire est ouverte jusqu'à lui. | das level, von seinem anfang an. die geschichte ist bis dorthin offen. | el nivel que empezar, desde el principio. la historia se abre hasta él. | il livello da iniziare, dal suo inizio. la storia è aperta fino a lì. |
| 0x1EA | start the level | lancer le niveau | level starten | empezar el nivel | inizia il livello |
| 0x1EB | from the main menu | depuis le menu principal | aus dem hauptmenü | desde el menú principal | dal menu principale |
| 0x1EC | quit to the main menu, then open the options there to start a level. | quitte vers le menu principal, puis ouvre les options pour lancer un niveau. | zum hauptmenü beenden und dort in den optionen ein level starten. | sal al menú principal y abre allí las opciones para empezar un nivel. | esci al menu principale e apri lì le opzioni per iniziare un livello. |
| 0x1ED | speedrun timer | chrono speedrun | speedrun-timer | cronómetro speedrun | cronometro speedrun |
| 0x1EE | real time from new game, with each level's time as you finish it. | temps réel depuis nouvelle partie, avec le temps de chaque niveau fini. | echtzeit ab neues spiel, mit der zeit jedes beendeten levels. | tiempo real desde nueva partida, con el tiempo de cada nivel al acabarlo. | tempo reale da nuova partita, con il tempo di ogni livello finito. |
| 0x1EF | drop shadow | ombre au sol | bodenschatten | sombra bajo el personaje | ombra a terra |
| 0x1F0 | a round shadow under you while you're in the air, to see where you'll land. | une ombre ronde sous toi en l'air, pour voir où tu vas atterrir. | ein runder schatten unter dir in der luft, um zu sehen, wo du landest. | una sombra redonda bajo ti en el aire, para ver dónde caerás. | un'ombra rotonda sotto di te in aria, per vedere dove atterrerai. |
| 0x1F1 | shadows on crates | ombres sur les caisses | schatten auf kisten | sombras en las cajas | ombre sulle casse |
| 0x1F2 | your shadow falls on the tops of crates too. from a level's next loading. | ton ombre tombe aussi sur le dessus des caisses. au prochain chargement d'un niveau. | dein schatten fällt auch auf kisten. ab dem nächsten laden eines levels. | tu sombra cae también sobre las cajas. desde la próxima carga de un nivel. | la tua ombra cade anche sopra le casse. dal prossimo caricamento di un livello. |
| 0x1F3 | thanks | remerciements | dank | agradecimientos | ringraziamenti |
| 0x1F4 | mods | mods | mods | mods | mod |
| 0x1F5 | import mods | importer des mods | mods importieren | importar mods | importa mod |
| 0x1F6 | turns the zips and disc images in the mods folder into mods. from the next start. | transforme les zip et images disque du dossier mods en mods. au prochain lancement. | macht aus den zips und disc-images im mods-ordner mods. ab dem nächsten start. | convierte los zip e imágenes de disco de la carpeta mods en mods. al volver a abrir el juego. | trasforma zip e immagini disco della cartella mods in mod. dal prossimo avvio del gioco. |
| 0x1F7 | open the mods folder | ouvrir le dossier mods | mods-ordner öffnen | abrir la carpeta mods | apri la cartella mods |
| 0x1F8 | put mods there. where two change a file, the first on wins. changes from the next start. | mets-y les mods. si deux changent un fichier, le premier activé gagne. au prochain lancement. | leg die mods dort ab. ändern zwei eine datei, gilt die erste aktive. ab dem nächsten start. | pon ahí los mods. si dos cambian un archivo, gana el primero activo. al volver a abrir el juego. | mettici le mod. se due cambiano un file, vince la prima attiva. dal prossimo avvio del gioco. |
| 0x1F9 | files in the mods folder | fichiers du dossier mods | dateien im mods-ordner | archivos de la carpeta mods | file nella cartella mods |
| 0x1FA | importing the mods: a disc image takes a minute or two. | import des mods : une image disque prend une minute ou deux. | mods werden importiert: ein disc-image dauert ein, zwei minuten. | importando los mods: una imagen de disco tarda un minuto o dos. | importazione delle mod: un'immagine disco richiede un minuto o due. |
| 0x1FB | done: the mods are used from the next start of the game. | terminé : les mods sont utilisés au prochain lancement du jeu. | fertig: die mods gelten ab dem nächsten start des spiels. | hecho: los mods se usan al volver a abrir el juego. | fatto: le mod si usano dal prossimo avvio del gioco. |
| 0x1FC | something couldn't be imported: the reason is in log.txt. | quelque chose n'a pas pu être importé : la raison est dans log.txt. | etwas konnte nicht importiert werden: der grund steht in log.txt. | algo no se pudo importar: el motivo está en log.txt. | qualcosa non è stato importato: il motivo è in log.txt. |
| 0x1FD | can't be used: | inutilisable : | nicht nutzbar: | no se puede usar: | non utilizzabile: |
| 0x1FE | its program changes don't apply. | ses changements du programme ne s'appliquent pas. | seine programmänderungen gelten nicht. | sus cambios del programa no se aplican. | le sue modifiche al programma non valgono. |

## English (29 lines)

| File | Line | What | Original | PC |
|---|---|---|---|---|
| Code | 0x20 | no controller notice | please insert an<br>analog controller<br>(dualshock@2)<br>into controller<br>port 1. | please connect<br>a controller<br>to the computer. |
| Code | 0x26 | screen position help | use the directional buttons<br>or left analog stick<br>on the analog controller<br>(dualshock@2)<br>in controller port 1<br>to position the screen. | use the directional buttons<br>or left stick<br>on the controller<br>to position the screen. |
| Code | 0x27 | save: checking (boot check, Check) | checking memory card (ps2)<br>in memory card slot 1.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | checking save data.<br><br>please don't turn<br>off your computer. |
| Code | 0x28 | save: formatting (Format) | formatting memory card (ps2)<br>in memory card slot 1.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | preparing save storage.<br><br>please don't turn<br>off your computer. |
| Code | 0x29 | save: checking (Measure) | checking memory card (ps2)<br>in memory card slot 1.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | checking save data.<br><br>please don't turn<br>off your computer. |
| Code | 0x2A | save: creating the save (Create) | saving data.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | saving game.<br><br>please don't turn<br>off your computer. |
| Code | 0x2B | save: writing every file (WriteFolder) | saving data.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | saving game.<br><br>please don't turn<br>off your computer. |
| Code | 0x2C | save: reading the slots (ReadFolder) | loading data.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | loading game.<br><br>please don't turn<br>off your computer. |
| Code | 0x2D | save: checking (Find) | checking memory card (ps2)<br>in memory card slot 1.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | checking save data.<br><br>please don't turn<br>off your computer. |
| Code | 0x2E | save: writing a slot (WriteFile) | saving data.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | saving game.<br><br>please don't turn<br>off your computer. |
| Code | 0x2F | save: reading a slot (ReadFile) | loading data.<br><br>do not remove<br>memory card (ps2),<br>controller, reset, or<br>switch-off the console. | loading game.<br><br>please don't turn<br>off your computer. |
| Code | 0x30 | save: no save, create one? (Create screen) | no save file on the<br>memory card (ps2)<br>in memory card slot 1.<br><br>Would you like to create a<br>(xxx)<br>save file? | no saved games found<br>on this computer.<br><br>Would you like to create a<br>(xxx)<br>save game? |
| Code | 0x31 | save: no card (InsertCard screen) | no memory card (ps2)<br>in memory card slot 1. | save storage is<br>not available<br>on this computer. |
| Code | 0x32 | save: unformatted (Unformatted screen) | memory card (ps2)<br>in memory card slot 1<br>is unformatted.<br><br>format memory card (ps2)? | save storage<br>on this computer<br>is not ready.<br><br>prepare save storage? |
| Code | 0x33 | load: no save (InsertSave screen) | no (xxx)<br>save data present<br>on memory card (ps2)<br>in memory card slot 1. | no (xxx)<br>saved games found<br>on this computer. |
| Code | 0x34 | save: not enough space (NoRoom / InsertRoom screens) | insufficient free space<br>on memory card (ps2)<br>in memory card slot 1.<br><br>(xxx)<br>requires (x)kb of free space<br>to save data. | your computer doesn't have<br>enough free space<br>to save games.<br><br>(xxx)<br>requires (x)kb of free space. |
| Code | 0x38 | save: format failed (FormatFailed screen) | format failed!<br><br>check memory card (ps2)<br>in memory card slot 1,<br>and please try again. | preparation failed!<br><br>check your computer's<br>storage and please<br>try again. |
| Code | 0x39 | save: save failed (SaveFailed screen) | save failed!<br><br>check memory card (ps2)<br>in memory card slot 1,<br>and please try again. | save failed!<br><br>check your computer's<br>storage and please<br>try again. |
| Code | 0x3A | load: load failed (LoadFailed screen) | load failed!<br><br>check memory card (ps2)<br>in memory card slot 1,<br>and please try again. | the saved game is damaged<br>and cannot be used. |
| Code | 0x3E | save: "format" item | format | prepare |
| Code | 0x42 | autosave warning (autosave turned on) | do not remove<br>memory card (ps2),<br>in memory card slot 1<br>controller, reset, or<br>switch-off the console<br>when the autosave indicator<br>(below) is present. | please don't turn<br>off your computer<br>when the autosave indicator<br>(below) is present. |
| Code | 0x43 | autosaving (autosave indicator's text) | autosaving data.<br><br>do not remove<br>memory card (ps2),<br>in memory card slot 1<br>controller, reset, or<br>switch-off the console. | autosaving data.<br><br>please don't turn<br>off your computer. |
| Code | 0x5F | save: really format? (ConfirmFormat screen) | do you really wish to<br>format memory card (ps2)<br>in memory card slot 1? | do you really wish to<br>prepare save storage<br>on this computer? |
| Code | 0xB4 | save: format successful | format successful | preparation successful |
| Code | 0xB7 | save: no card at boot (NoCard screen) | no memory card (ps2)<br>in memory card slot 1.<br><br>(xxx)<br>requires (x)kb of free space<br>to save data. | save storage is<br>not available<br>on this computer.<br><br>(xxx)<br>requires (x)kb of free space<br>to save data. |
| AgentLab | 0x2C | hint: the UFO | use the left analog stick to drive the ufo | use the left stick to drive the ufo |
| AgentLab | 0x2E | hint: the hoverboard | use the left analog stick to steer the hoverboard<br>hold the } button to hover higher and slower | use the left stick to steer the hoverboard<br>hold the } button to hover higher and slower |
| AgentLab | 0x41 | hint: the hover scooter | use the left analog stick to control the hover scooter. | use the left stick to control the hover scooter. |
| AgentLab | 0x18 | the cutscene skip's prompt (not console wording: the community mod's mod/text.txt, made "hold any button", in the unused line; NATIVE.md, "Bug fixes (optional)") | zzz | hold any button to skip |

## French (29 lines)

| File | Line | What | Original | PC |
|---|---|---|---|---|
| Code | 0x20 | no controller notice | branchez une manette<br>analogique (dualshock@2) <br>dans le port de<br>manette n°1. | branchez une manette<br>sur l'ordinateur. |
| Code | 0x26 | screen position help | utiliser les touches<br>directionnelles ou le joystick<br>analogique gauche sur la<br>manette analogique<br>(dualshock@2) branchée sur<br>le port de manette n°1<br>pour centrer l'écran. | utiliser les touches<br>directionnelles ou le joystick<br>gauche de la manette<br>pour centrer l'écran. |
| Code | 0x27 | save: checking (boot check, Check) | verification de la memory card (ps2)<br>dans la fente pour memory card n°1.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | vérification des données<br>de sauvegarde.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x28 | save: formatting (Format) | formatage de la memory card (ps2)<br>dans la fente pour memory card n°1.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | préparation de l'espace<br>de sauvegarde.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x29 | save: checking (Measure) | verification de la memory card (ps2)<br>dans la fente pour memory card n°1.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | vérification des données<br>de sauvegarde.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x2A | save: creating the save (Create) | sauvegarde en cours.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | sauvegarde en cours.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x2B | save: writing every file (WriteFolder) | sauvegarde en cours.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | sauvegarde en cours.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x2C | save: reading the slots (ReadFolder) | chargement en cours.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | chargement en cours.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x2D | save: checking (Find) | verification de la memory card (ps2)<br>dans la fente pour memory card n°1.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | vérification des données<br>de sauvegarde.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x2E | save: writing a slot (WriteFile) | sauvegarde en cours.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | sauvegarde en cours.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x2F | save: reading a slot (ReadFile) | chargement en cours.<br><br>ne pas retirer la<br>memory card (ps2)<br>ou la manette, ni redemarrer ou<br>eteindre la console. | chargement en cours.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x30 | save: no save, create one? (Create screen) | pas de sauvegarde sur<br>la memory card (ps2)<br>dans la fente pour<br>memory card n°1.<br><br>Voulez-vous créer un<br>fichier de sauvegarde<br>(xxx) ? | pas de sauvegarde<br>sur l'ordinateur.<br><br>Voulez-vous créer un<br>fichier de sauvegarde<br>(xxx) ? |
| Code | 0x31 | save: no card (InsertCard screen) | pas de memory card (ps2)<br>dans la fente pour memory card n°1. | l'espace de sauvegarde<br>de l'ordinateur<br>est inaccessible. |
| Code | 0x32 | save: unformatted (Unformatted screen) | la memory card (ps2)<br>dans la fente pour memory card n°1<br>n'est pas formatee.<br><br>formater la memory card (ps2) ? | l'espace de sauvegarde<br>de l'ordinateur<br>n'est pas prêt.<br><br>le préparer ? |
| Code | 0x33 | load: no save (InsertSave screen) | pas de donnees (xxx)<br>presentes sur<br>la memory card (ps2)<br>dans la fente pour memory card n°1. | pas de données (xxx)<br>présentes<br>sur l'ordinateur. |
| Code | 0x34 | save: not enough space (NoRoom / InsertRoom screens) | espace insuffisant<br>sur la memory card (ps2)<br>dans la fente pour memory card n°1.<br><br>(xxx)<br>necessite (x)kb d'espace libre<br>pour sauvegarder les donnees. | espace insuffisant<br>sur l'ordinateur.<br><br>(xxx)<br>nécessite (x)kb d'espace libre<br>pour sauvegarder les données. |
| Code | 0x38 | save: format failed (FormatFailed screen) | echec du formatage !<br><br>verifier la memory card (ps2)<br>dans la fente pour memory card n°1<br>et reessayer. | échec de la préparation !<br><br>vérifier le stockage<br>de l'ordinateur<br>et réessayer. |
| Code | 0x39 | save: save failed (SaveFailed screen) | echec de la sauvegarde !<br><br>verifier la memory card (ps2)<br>dans la fente pour memory card n°1<br>et reessayer. | échec de la sauvegarde !<br><br>vérifier le stockage<br>de l'ordinateur<br>et réessayer. |
| Code | 0x3A | load: load failed (LoadFailed screen) | echec du chargement !<br><br>verifier la memory card (ps2)<br>dans la fente pour memory card n°1<br>et reessayer. | échec du chargement !<br><br>vérifier le stockage<br>de l'ordinateur<br>et réessayer. |
| Code | 0x3E | save: "format" item | formater | préparer |
| Code | 0x42 | autosave warning (autosave turned on) | ne pas retirer la memory<br>card (ps2) dans la fente<br>pour memory card n°1,<br>ou la manette, ni redemarrer<br>ou eteindre la console quand<br>l'indicateur d'auto-sauvegarde<br>(ci-dessous) est present. | ne pas quitter le jeu<br>ni éteindre l'ordinateur<br>quand l'indicateur<br>d'auto-sauvegarde<br>(ci-dessous) est présent. |
| Code | 0x43 | autosaving (autosave indicator's text) | auto-sauvegarde en cours.<br><br>ne pas retirer la<br>memory card (ps2)<br>dans la fente pour<br>memory card n°1,<br>ou la manette, ni redemarrer<br>ou eteindre la console. | auto-sauvegarde en cours.<br><br>ne pas quitter le jeu<br>ni éteindre l'ordinateur. |
| Code | 0x5F | save: really format? (ConfirmFormat screen) | formater la memory<br>card (ps2) dans la fente<br>pour memory card n°1 ? | préparer l'espace<br>de sauvegarde<br>de l'ordinateur ? |
| Code | 0xB4 | save: format successful | formatage réussi | préparation réussie |
| Code | 0xB7 | save: no card at boot (NoCard screen) | pas de memory card (ps2)<br>dans la fente pour<br>memory card n°1.<br><br>(xxx)<br>necessite (x)kb d'espace<br>libre pour sauvegarder<br>les donnees. | l'espace de sauvegarde<br>de l'ordinateur<br>est inaccessible.<br><br>(xxx)<br>nécessite (x)kb d'espace<br>libre pour sauvegarder<br>les données. |
| AgentLab | 0x2C | hint: the UFO | Utilise le joystick analogique gauche pour piloter l'ovni | Utilise le joystick gauche pour piloter l'ovni |
| AgentLab | 0x2E | hint: the hoverboard | utilise le joystick analogique gauche pour diriger l'aéroplanche<br>maintiens la touche } enfoncée pour monter plus haut et aller moins vite | utilise le joystick gauche pour diriger l'aéroplanche<br>maintiens la touche } enfoncée pour monter plus haut et aller moins vite |
| AgentLab | 0x41 | hint: the hover scooter | utilise le joystick analogique gauche pour piloter le scooter. | utilise le joystick gauche pour piloter le scooter. |
| AgentLab | 0x18 | the cutscene skip's prompt (not console wording: the community mod's mod/text.txt, made "hold any button", in the unused line; NATIVE.md, "Bug fixes (optional)") | zzz | maintiens une touche pour passer |

## German (29 lines)

| File | Line | What | Original | PC |
|---|---|---|---|---|
| Code | 0x20 | no controller notice | bitte einen analog<br>controller (dualshock@2) an<br>controller-anschluss 1<br>anschließen. | bitte einen<br>controller an den<br>computer anschließen. |
| Code | 0x26 | screen position help | benutz die richtungstasten <br>oder den linken analog-stick <br>des analog controllers <br>(dualshock@2) in <br>controller-anschluss 1, um <br>den bildschirm auszurichten. | benutz die richtungstasten <br>oder den linken stick <br>des controllers, um <br>den bildschirm auszurichten. |
| Code | 0x27 | save: checking (boot check, Check) | memory card (ps2)<br>in memory card-steckplatz 1<br>wird überprüft.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speicherdaten<br>werden überprüft.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x28 | save: formatting (Format) | formatiert memory card (ps2)<br>in memory card-steckplatz 1.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speicherplatz wird<br>vorbereitet.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x29 | save: checking (Measure) | memory card (ps2)<br>in memory card-steckplatz 1<br>wird überprüft.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speicherdaten<br>werden überprüft.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x2A | save: creating the save (Create) | speichert daten.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speichert daten.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x2B | save: writing every file (WriteFolder) | speichert daten.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speichert daten.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x2C | save: reading the slots (ReadFolder) | lädt daten.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht <br>zurückstellen oder abschalten. | lädt daten.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x2D | save: checking (Find) | memory card (ps2)<br>in memory card-steckplatz 1<br>wird überprüft.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speicherdaten<br>werden überprüft.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x2E | save: writing a slot (WriteFile) | speichert daten.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | speichert daten.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x2F | save: reading a slot (ReadFile) | lädt daten.<br><br>memory card (ps2) und<br>controller nicht entfernen<br>und die konsole nicht <br>zurückstellen oder abschalten. | lädt daten.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x30 | save: no save, create one? (Create screen) | keine datei auf<br>der memory card (ps2)<br>in memory card-steckplatz 1.<br><br>möchtest du eine<br>(xxx)<br>-datei erstellen? | keine datei auf<br>diesem computer.<br><br>möchtest du eine<br>(xxx)<br>-datei erstellen? |
| Code | 0x31 | save: no card (InsertCard screen) | keine memory card (ps2)<br>in memory card-steckplatz 1. | kein speicherplatz<br>auf diesem computer<br>verfügbar. |
| Code | 0x32 | save: unformatted (Unformatted screen) | die memory card (ps2)<br>in memory card-steckplatz 1<br>ist nicht formatiert.<br><br>möchtest du die<br>memory card (ps2)<br>formatieren? | der speicherplatz<br>auf diesem computer<br>ist nicht bereit.<br><br>möchtest du ihn<br>vorbereiten? |
| Code | 0x33 | load: no save (InsertSave screen) | keine gespeicherten<br>(xxx)-daten auf der<br>memory card (ps2)<br>in memory card-steckplatz 1<br>vorhanden. | keine gespeicherten<br>(xxx)-daten auf<br>diesem computer<br>vorhanden. |
| Code | 0x34 | save: not enough space (NoRoom / InsertRoom screens) | ungenügend speicherplatz auf<br>der memory card (ps2)<br>in memory card-steckplatz 1.<br><br>(xxx)<br>benötigt (x) kb freien<br>Speicherplatz, um Daten zu<br>speichern. | ungenügend speicherplatz<br>auf diesem computer.<br><br>(xxx)<br>benötigt (x) kb freien<br>Speicherplatz, um Daten zu<br>speichern. |
| Code | 0x38 | save: format failed (FormatFailed screen) | formatieren gescheitert!<br><br>bitte überprüf die<br>memory card (ps2)<br>in memory card-steckplatz 1<br>und versuch es noch einmal. | vorbereiten gescheitert!<br><br>bitte überprüf den<br>speicher des computers<br>und versuch es noch einmal. |
| Code | 0x39 | save: save failed (SaveFailed screen) | speichern gescheitert!<br><br>bitte überprüf die<br>memory card (ps2)<br>in memory card-steckplatz 1<br>und versuch es noch einmal. | speichern gescheitert!<br><br>bitte überprüf den<br>speicher des computers<br>und versuch es noch einmal. |
| Code | 0x3A | load: load failed (LoadFailed screen) | laden gescheitert!<br><br>bitte überprüf die<br>memory card (ps2)<br>in memory card-steckplatz 1<br>und versuch es noch einmal. | laden gescheitert!<br><br>bitte überprüf den<br>speicher des computers<br>und versuch es noch einmal. |
| Code | 0x3E | save: "format" item | formatieren | vorbereiten |
| Code | 0x42 | autosave warning (autosave turned on) | memory card (ps2) in<br>memory card-steckplatz 1 und<br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten,<br>solange die automatische<br>speicher-anzeige<br>(unten) angezeigt wird. | das spiel nicht beenden<br>und den computer<br>nicht ausschalten,<br>solange die automatische<br>speicher-anzeige<br>(unten) angezeigt wird. |
| Code | 0x43 | autosaving (autosave indicator's text) | automatische datenspeicherung.<br><br>memory card (ps2) in<br>memory card-steckplatz 1 und <br>controller nicht entfernen<br>und die konsole nicht<br>zurückstellen oder abschalten. | automatische datenspeicherung.<br><br>das spiel nicht beenden<br>und den computer<br>nicht ausschalten. |
| Code | 0x5F | save: really format? (ConfirmFormat screen) | bist du sicher, dass du die<br>memory card (ps2) in<br>memory card-steckplatz 1<br>formatieren möchtest? | bist du sicher, dass du den<br>speicherplatz auf<br>diesem computer<br>vorbereiten möchtest? |
| Code | 0xB4 | save: format successful | formatieren erfolgreich. | vorbereiten erfolgreich. |
| Code | 0xB7 | save: no card at boot (NoCard screen) | keine memory card (ps2)<br>in memory card-steckplatz 1.<br><br>(xxx)<br>benötigt (x) kb freien <br>speicherplatz, um daten<br>zu speichern. | kein speicherplatz<br>auf diesem computer<br>verfügbar.<br><br>(xxx)<br>benötigt (x) kb freien <br>speicherplatz, um daten<br>zu speichern. |
| AgentLab | 0x2C | hint: the UFO | benutz den linken analog-stick, um das ufo zu steuern. | benutz den linken stick, um das ufo zu steuern. |
| AgentLab | 0x2E | hint: the hoverboard | benutz den linken analog-stick, um das hoverboard zu steuern.<br>drück die r1-taste, um höher und langsamer zu schweben. | benutz den linken stick, um das hoverboard zu steuern.<br>drück die }-taste, um höher und langsamer zu schweben. |
| AgentLab | 0x41 | hint: the hover scooter | benutz den linken analog-stick, um den hover-roller zu steuern. | benutz den linken stick, um den hover-roller zu steuern. |
| AgentLab | 0x18 | the cutscene skip's prompt (not console wording: the community mod's mod/text.txt, made "hold any button", in the unused line; NATIVE.md, "Bug fixes (optional)") | zzz | beliebige taste halten zum überspringen |

## Spanish (29 lines)

| File | Line | What | Original | PC |
|---|---|---|---|---|
| Code | 0x20 | no controller notice | inserta un <br>mando analógico (dualshock@2)<br>en el puerto de mando 1. | conecta un <br>mando al ordenador. |
| Code | 0x26 | screen position help | usa los botones de dirección<br>o el joystick analógico izquierdo<br>del mando analógico (dualshock@2)<br>conectado al puerto de mando 1<br>para centrar la pantalla. | usa los botones de dirección<br>o el joystick izquierdo<br>del mando<br>para centrar la pantalla. |
| Code | 0x27 | save: checking (boot check, Check) | comprobando la memory card (ps2)<br>de la ranura para memory card 1.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | comprobando los<br>datos guardados.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x28 | save: formatting (Format) | formateando la memory card (ps2)<br>de la ranura para memory card 1.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | preparando el espacio<br>de guardado.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x29 | save: checking (Measure) | comprobando la memory card (ps2)<br>de la ranura para memory card 1.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | comprobando los<br>datos guardados.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x2A | save: creating the save (Create) | guardando datos.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | guardando datos.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x2B | save: writing every file (WriteFolder) | guardando datos.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | guardando datos.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x2C | save: reading the slots (ReadFolder) | cargando datos.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | cargando datos.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x2D | save: checking (Find) | comprobando la memory card (ps2)<br>de la ranura para memory card 1.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | comprobando los<br>datos guardados.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x2E | save: writing a slot (WriteFile) | guardando datos.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | guardando datos.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x2F | save: reading a slot (ReadFile) | cargando datos.<br><br>no extraigas<br>la memory card (ps2)<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | cargando datos.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x30 | save: no save, create one? (Create screen) | no hay ningún archivo guardado en la<br>memory card (ps2)<br>de la ranura para memory card 1.<br><br>¿Quieres crear una<br>partida<br>guardada de (xxx)? | no hay ningún archivo guardado<br>en el ordenador.<br><br>¿Quieres crear una<br>partida<br>guardada de (xxx)? |
| Code | 0x31 | save: no card (InsertCard screen) | no hay memory card (ps2)<br>en la ranura para memory card 1. | no se puede acceder<br>al espacio de guardado<br>del ordenador. |
| Code | 0x32 | save: unformatted (Unformatted screen) | la memory card (ps2)<br>de la ranura para memory card 1<br>no está formateada.<br><br>¿Deseas formatear<br>la memory card (ps2)? | el espacio de guardado<br>del ordenador<br>no está preparado.<br><br>¿Deseas prepararlo? |
| Code | 0x33 | load: no save (InsertSave screen) | no hay<br>datos guardados de (xxx)<br>en la memory card (ps2)<br>de la ranura para memory card 1. | no hay<br>datos guardados de (xxx)<br>en el ordenador. |
| Code | 0x34 | save: not enough space (NoRoom / InsertRoom screens) | no hay espacio suficiente libre<br>en la memory card (ps2)<br>de la ranura para memory card 1.<br><br>(xxx)<br>necesita (x) kb de espacio libre<br>para guardar datos. | no hay espacio suficiente libre<br>en el ordenador.<br><br>(xxx)<br>necesita (x) kb de espacio libre<br>para guardar datos. |
| Code | 0x38 | save: format failed (FormatFailed screen) | ¡error al formatear!<br><br>comprueba la memory card (ps2)<br>de la ranura para memory card 1<br>e inténtalo de nuevo. | ¡error al preparar!<br><br>comprueba el almacenamiento<br>del ordenador<br>e inténtalo de nuevo. |
| Code | 0x39 | save: save failed (SaveFailed screen) | ¡error al guardar!<br><br>comprueba la memory card (ps2)<br>de la ranura para memory card 1<br>e inténtalo de nuevo. | ¡error al guardar!<br><br>comprueba el almacenamiento<br>del ordenador<br>e inténtalo de nuevo. |
| Code | 0x3A | load: load failed (LoadFailed screen) | ¡error al cargar!<br><br>comprueba la memory card (ps2)<br>de la ranura para memory card 1<br>e inténtalo de nuevo. | ¡error al cargar!<br><br>comprueba el almacenamiento<br>del ordenador<br>e inténtalo de nuevo. |
| Code | 0x3E | save: "format" item | formatear | preparar |
| Code | 0x42 | autosave warning (autosave turned on) | cuando el indicador de autoguardado<br>(abajo) esté presente, no reinicies ni<br>apagues la consola, no extraigas<br>la memory card (ps2)<br>de la ranura para memory card 1<br>ni el mando. | cuando el indicador de autoguardado<br>(abajo) esté presente, no salgas<br>del juego ni apagues<br>el ordenador. |
| Code | 0x43 | autosaving (autosave indicator's text) | autoguardando datos.<br><br>no extraigas la<br>memory card (ps2)<br>de la ranura para<br>memory card 1<br>ni el mando.<br><br>no reinicies ni<br>apagues la consola. | autoguardando datos.<br><br>no salgas del juego<br>ni apagues el ordenador. |
| Code | 0x5F | save: really format? (ConfirmFormat screen) | ¿seguro que quieres<br>formatear la memory card (ps2)<br>de la ranura para memory card 1? | ¿seguro que quieres<br>preparar el espacio<br>de guardado del ordenador? |
| Code | 0xB4 | save: format successful | éxito al formatear | éxito al preparar |
| Code | 0xB7 | save: no card at boot (NoCard screen) | no hay memory card (ps2)<br>en la ranura para memory card 1.<br><br>(xxx)<br>necesita (x) kb de espacio libre<br>para guardar datos. | no se puede acceder<br>al espacio de guardado<br>del ordenador.<br><br>(xxx)<br>necesita (x) kb de espacio libre<br>para guardar datos. |
| AgentLab | 0x2C | hint: the UFO | utiliza el joystick analógico izquierdo para pilotar el ovni | utiliza el joystick izquierdo para pilotar el ovni |
| AgentLab | 0x2E | hint: the hoverboard | usa el joystick analógico izquierdo para conducir la nave<br>mantén pulsado el botón } para volar más alto y más despacio | usa el joystick izquierdo para conducir la nave<br>mantén pulsado el botón } para volar más alto y más despacio |
| AgentLab | 0x41 | hint: the hover scooter | utiliza el joystick analógico izquierdo para controlar el patinete aéreo. | utiliza el joystick izquierdo para controlar el patinete aéreo. |
| AgentLab | 0x18 | the cutscene skip's prompt (not console wording: the community mod's mod/text.txt, made "hold any button", in the unused line; NATIVE.md, "Bug fixes (optional)") | zzz | mantén cualquier botón para saltar |

## Italian (29 lines)

| File | Line | What | Original | PC |
|---|---|---|---|---|
| Code | 0x20 | no controller notice | inserisci un controller<br>analogico (dualshock@2)<br>nell'ingresso<br>controller 1. | collega un controller<br>al computer. |
| Code | 0x26 | screen position help | usa i tasti direzionali<br>o la levetta analogica sinistra<br>del controller<br>analogico (dualshock@2)<br>inserito nell'ingresso controller 1<br>per posizionare la schermata. | usa i tasti direzionali<br>o la levetta sinistra<br>del controller<br>per posizionare la schermata. |
| Code | 0x27 | save: checking (boot check, Check) | controllo della memory card (ps2)<br>inserita nell’ingresso<br>memory card 1 in corso...<br><br>non rimuovere la<br>memory card (ps2)<br>né il controller e non<br>resettare/spegnere la console! | controllo dei dati<br>salvati in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x28 | save: formatting (Format) | formattazione della memory card (ps2)<br>inserita nell’ingresso<br>memory card 1 in corso...<br>non rimuovere la memory card (ps2)<br>né il controller e non<br>resettare/spegnere la console! | preparazione dello spazio<br>di salvataggio in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x29 | save: checking (Measure) | controllo della memory card (ps2)<br>inserita nell’ingresso<br>memory card 1 in corso...<br><br>non rimuovere la<br>memory card (ps2)<br>né il controller e non<br>resettare/spegnere la console! | controllo dei dati<br>salvati in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x2A | save: creating the save (Create) | salvataggio dati<br>in corso...<br><br>non rimuovere la<br>memory card (ps2) né<br>il controller e non<br>resettare/spegnere la console! | salvataggio dati<br>in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x2B | save: writing every file (WriteFolder) | salvataggio dati<br>in corso...<br><br>non rimuovere la<br>memory card (ps2) né<br>il controller e non<br>resettare/spegnere la console! | salvataggio dati<br>in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x2C | save: reading the slots (ReadFolder) | caricamento dati in corso...<br><br>non rimuovere<br>la memory card (ps2)<br>né il controller e non<br>resettare/spegnere la console! | caricamento dati in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x2D | save: checking (Find) | controllo della memory card (ps2)<br>inserita nell’ingresso<br>memory card 1 in corso...<br><br>non rimuovere la<br>memory card (ps2)<br>né il controller e non<br>resettare/spegnere la console! | controllo dei dati<br>salvati in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x2E | save: writing a slot (WriteFile) | salvataggio dati<br>in corso...<br><br>non rimuovere la<br>memory card (ps2) né<br>il controller e non<br>resettare/spegnere la console! | salvataggio dati<br>in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x2F | save: reading a slot (ReadFile) | caricamento dati in corso...<br><br>non rimuovere<br>la memory card (ps2)<br>né il controller e non<br>resettare/spegnere la console! | caricamento dati in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x30 | save: no save, create one? (Create screen) | nessun file salvato sulla<br>memory card (ps2)<br>inserita nell'ingresso<br>memory card 1.<br><br>creare un file di<br>salvataggio<br>(xxx)? | nessun file salvato<br>sul computer.<br><br>creare un file di<br>salvataggio<br>(xxx)? |
| Code | 0x31 | save: no card (InsertCard screen) | nessuna memory card (ps2)<br>inserita nell'ingresso memory card 1. | spazio di salvataggio<br>del computer<br>non disponibile. |
| Code | 0x32 | save: unformatted (Unformatted screen) | la memory card (ps2)<br>inserita nell'ingresso memory card 1<br>non è formattata.<br><br>formattare la memory card (ps2)? | lo spazio di salvataggio<br>del computer<br>non è pronto.<br><br>prepararlo? |
| Code | 0x33 | load: no save (InsertSave screen) | nessun dato salvato di<br>(xxx)<br>sulla memory card (ps2) inserita<br>nell'ingressso memory card 1. | nessun dato salvato di<br>(xxx)<br>sul computer. |
| Code | 0x34 | save: not enough space (NoRoom / InsertRoom screens) | spazio libero insufficiente<br>sulla memory card (ps2)<br>inserita nell'ingresso memory card 1<br><br>(xxx)<br>richiede (x) kb di spazio libero<br>per salvare i dati. | spazio libero insufficiente<br>sul computer.<br><br>(xxx)<br>richiede (x) kb di spazio libero<br>per salvare i dati. |
| Code | 0x38 | save: format failed (FormatFailed screen) | formattazione fallita!<br><br>controllare la memory card (ps2)<br>inserita nell’ingresso memory card 1<br>e riprovare. | preparazione fallita!<br><br>controllare lo spazio<br>di archiviazione del computer<br>e riprovare. |
| Code | 0x39 | save: save failed (SaveFailed screen) | salvataggio fallito!<br><br>controllare la memory card (ps2)<br>inserita nell’ingresso memory card 1<br>e riprovare. | salvataggio fallito!<br><br>controllare lo spazio<br>di archiviazione del computer<br>e riprovare. |
| Code | 0x3A | load: load failed (LoadFailed screen) | caricamento fallito!<br><br>controllare la memory card (ps2)<br>inserita nell’ingresso memory card 1<br>e riprovare. | caricamento fallito!<br><br>controllare lo spazio<br>di archiviazione del computer<br>e riprovare. |
| Code | 0x3E | save: "format" item | formatta | prepara |
| Code | 0x42 | autosave warning (autosave turned on) | non rimuovere la memory<br>card (ps2), inserita nell'ingresso<br>memory card 1 né il controller e<br>non resettare/spegnere la<br>console quando l'indicatore<br>dell'autosalvataggio (sotto)<br>è presente. | non uscire dal gioco<br>e non spegnere il computer<br>quando l'indicatore<br>dell'autosalvataggio (sotto)<br>è presente. |
| Code | 0x43 | autosaving (autosave indicator's text) | autosalvataggio dati<br>in corso...<br><br>non rimuovere la memory<br>card (ps2) inserita<br>nell'ingresso memory<br>card 1 né il controller e<br>non resettare/spegnere<br>la console! | autosalvataggio dati<br>in corso...<br><br>non uscire dal gioco<br>e non spegnere<br>il computer! |
| Code | 0x5F | save: really format? (ConfirmFormat screen) | <br>formattare la memory card (ps2)<br>inserita nell'ingresso memory card 1? | <br>preparare lo spazio<br>di salvataggio del computer? |
| Code | 0xB4 | save: format successful | formattazione completata | preparazione completata |
| Code | 0xB7 | save: no card at boot (NoCard screen) | nessuna memory card (ps2)<br>inserita nell'ingresso memory card 1<br><br>(xxx)<br>richiede (x) kb di spazio libero<br>per salvare i dati. | spazio di salvataggio<br>del computer<br>non disponibile.<br><br>(xxx)<br>richiede (x) kb di spazio libero<br>per salvare i dati. |
| AgentLab | 0x2C | hint: the UFO | usa la levetta analogica sinistra per guidare l'ufo | usa la levetta sinistra per guidare l'ufo |
| AgentLab | 0x2E | hint: the hoverboard | usa la levetta analogica sinistra per manovrare l' hoverboard<br>tieni premuto il tasto } per volare più in alto e più lentamente | usa la levetta sinistra per manovrare l' hoverboard<br>tieni premuto il tasto } per volare più in alto e più lentamente |
| AgentLab | 0x41 | hint: the hover scooter | usa la levetta analogica sinistra per controllare lo scooter volante. | usa la levetta sinistra per controllare lo scooter volante. |
| AgentLab | 0x18 | the cutscene skip's prompt (not console wording: the community mod's mod/text.txt, made "hold any button", in the unused line; NATIVE.md, "Bug fixes (optional)") | zzz | tieni premuto un tasto per saltare |
