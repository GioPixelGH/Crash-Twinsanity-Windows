#pragma once

// The settings screen's model (settings.cpp: its pages and rows, what's selected, the input) as its view (settingsview.cpp) sees
// it. The view draws the model on a canvas (uicanvas.h) and records its hit regions; the logic takes the mouse against them
#include "ui/bindings.h"
#include "ui/uicanvas.h"

#include <functional>
#include <string>
#include <vector>

namespace NativeUi
{
enum RowKind
{
    RowChoice,
    RowSlider,
    RowAction,
    RowBinding,
    RowHeader,
};

struct Row
{
    RowKind kind;
    u32 label;
    u32 description;
    // A choice's or a slider's value, and its range
    std::function<s32()> get;
    std::function<void(s32)> set;
    s32 minimum;
    s32 maximum;
    // A choice's value's text
    std::function<std::string(s32)> valueText;
    // An action's
    std::function<void()> activate;
    // A binding row's function
    u32 function;
    std::function<bool()> shown;
    // A display row whose change asks to be kept (the revert countdown)
    bool asksToKeep;
    // A row's label and description made when drawn (the Mods page's mods: their names and what they are), in the overlay's
    // Windows-1252, instead of label's and description's texts
    std::function<std::string()> labelText;
    std::function<std::string()> descriptionText;
};

// The tabs (the research's categories, native/research/MODERN_CRASH_UI.md 3.2; the design's, native/design/options/SPEC.md)
enum Tab : u32
{
    TabGameplay,
    TabDisplay,
    TabAudio,
    TabControls,
    TabAccessibility,
    TabMods,
    TabCount,
    Pages = TabCount,
};

// The controls table's columns: the key, the alt key, the controller's button
enum BindingColumn : u32
{
    ColumnKey,
    ColumnAltKey,
    ColumnPad,
    BindingColumns,
};

// The settings' dialogs (settings.cpp). Portable mode's: none, the switch asked about, refused (the program's folder can't be
// written to), copying, done (the game closes) or failed. Quit to the desktop's question (yes, no), and the credits (OK)
enum PortableDialog : u32
{
    DialogNone,
    DialogAsk,
    DialogRefused,
    DialogCopying,
    DialogDone,
    DialogFailed,
    DialogQuit,
    DialogCredits,
    // The Mods page's import: under way (its bar: copied of copyTotal thousandths, backup the source's name), done, or failed
    // (failure what went wrong, in English)
    DialogImporting,
    DialogImported,
    DialogImportFailed,
};

// What the view draws this frame
struct SettingsView
{
    u32 page;
    s32 row;
    u32 column;
    bool capturing;
    bool keeping;
    s32 keepSecondsLeft;
    // Portable mode's dialog: which, whether it's turning portable mode on, the copy's bytes done of the bytes there are, the backup
    // folder's name, what went wrong (English), the seconds left before the game closes
    PortableDialog dialog;
    bool dialogPortable;
    // The asked dialog's chosen button (yes, else no)
    bool dialogOnYes;
    u64 copied;
    u64 copyTotal;
    std::string backup;
    std::string failure;
    s32 closeSecondsLeft;
    // The rows swapped by a binding just made, while they flash (-1 none)
    s32 flashRows[2];
    bool flashOn;
    // When the page changed (the view's cross-fade) and the selection's row (its bar slides)
    u32 pageChanges;
};

SettingsView CurrentSettingsView();
const std::vector<Row>& SettingsRows(u32 page);
// The page's rows shown (their indexes), and whether a row can be selected
std::vector<s32> VisibleRowIndexes(u32 page);
bool RowSelectable(const Row& row);
u32 PageTitleText(u32 page);
// A binding row's cell: its key's or button's name; whether it can be bound
std::string BindingCellText(u32 function, u32 column);
bool BindingCellBindable(u32 function, u32 column);
// The list's first row shown (the view keeps the selection in its window)
s32& SettingsScroll();

// The view (settingsview.cpp): the settings screen on the overlay (reference pixels), the game's menus' bar of prompts, the cursor;
// each records its hit regions
class OverlayFrame;
void DrawSettingsView(OverlayFrame& frame, std::vector<HitRegion>& regions);
void DrawGameMenuChrome(OverlayFrame& frame, std::vector<HitRegion>& regions);
void DrawCursor(OverlayFrame& frame, f32 x, f32 y, bool overClickable);
// A row's description's width on the screen (reference pixels, the view's font and size: one line from x 360 of 1280)
f32 DescriptionWidth(const std::string& gameText);
constexpr f32 DescriptionRoom = 1280.0f - 360.0f - 10.0f;
// The skip's hold bar under the letterbox's "hold any button to skip" while a button is held (progress 0-1)
void DrawSkipProgress(OverlayFrame& frame, f32 progress);
}
