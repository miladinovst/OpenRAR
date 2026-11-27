#pragma once

#include <string>
#include <vector>

namespace openrar::i18n {

/// Supported UI and CLI languages (13 locales).
enum class Language : int {
    En = 0, ///< English
    Zh,     ///< Chinese (Simplified)
    Es,     ///< Spanish
    Hi,     ///< Hindi
    Ar,     ///< Arabic
    Pt,     ///< Portuguese
    Id,     ///< Indonesian
    Fr,     ///< French
    Ja,     ///< Japanese
    Ru,     ///< Russian
    Sr,     ///< Serbian
    Hr,     ///< Croatian
    Bs,     ///< Bosnian
    Count
};

/// Metadata describing an available language entry.
struct LanguageInfo {
    Language id;
    const char* code;        ///< ISO 639-1 code (e.g., "en", "zh", "sr")
    const char* native_name; ///< Native display name (e.g., "Srpski", "中文")
};

/// Translatable UI and CLI string keys.
enum class StrKey : int {
    // Top-level menus
    MenuFile = 0,
    MenuCommands,
    MenuTools,
    MenuFavorites,
    MenuOptions,
    MenuLanguage,
    MenuHelp,

    // File menu items
    FileOpen,
    FileBrowseFolder,
    FilePassword,
    FileExit,

    // Commands menu items
    CmdAdd,
    CmdExtract,
    CmdTest,
    CmdView,
    CmdDelete,
    CmdInfo,
    CmdComment,

    // Tools menu items
    ToolsWizard,
    ToolsFind,
    ToolsRepair,
    ToolsSfx,
    ToolsBenchmark,

    // Favorites & Options
    FavAdd,
    OptShellReg,
    OptShellUnreg,
    HelpAbout,

    // Toolbar buttons (9)
    TbAdd,
    TbExtractTo,
    TbTest,
    TbView,
    TbDelete,
    TbFind,
    TbWizard,
    TbInfo,
    TbRepair,

    // ListView columns & types
    ColName,
    ColSize,
    ColType,
    ColModified,
    ColPacked,
    ColCrc32,
    TypeSystemFolder,
    TypeFolder,
    TypeFile,

    // Context menu
    CtxExtractHere,
    CtxExtractFiles,
    CtxRefresh,

    // Status bar
    StatusTotal,
    StatusSelected,
    StatusBytesIn,
    StatusFile,
    StatusFiles,
    StatusPasswordSet,

    // Common dialog buttons & labels
    BtnOk,
    BtnCancel,
    BtnBrowse,
    BtnNext,
    DlgAddTitle,
    DlgArchiveName,
    DlgArchiveFormat,
    DlgCompMethod,
    DlgSetPassword,
    DlgSplitVolumes,
    DlgSolidArchive,
    DlgRecoveryRecord,
    DlgEncryptNames,
    DlgArchiveComment,
    DlgExtractTitle,
    DlgDestPath,
    DlgOverwriteMode,
    DlgOverwriteAll,
    DlgSkipExisting,
    DlgRenameAuto,
    DlgOptions,
    DlgPreservePaths,
    DlgOpenAfter,
    DlgPasswordIfEnc,
    DlgWizardTitle,
    DlgWizardPrompt,
    DlgWizardUnpack,
    DlgWizardCreate,
    DlgWizardAddExist,
    DlgEnterPassword,
    DlgConfirmDelete,
    MsgShellRegOk,
    MsgShellUnregOk,
    Count
};

/// Returns all 13 supported language descriptors.
const std::vector<LanguageInfo>& available_languages();

/// Sets the active language globally (thread-safe).
void set_language(Language lang);

/// Sets the active language from an ISO 639-1 code (e.g., "sr", "zh", "es"); returns false if unknown.
bool set_language_code(const std::string& code);

/// Returns the currently active language.
Language current_language();

/// Returns the ISO 639-1 code for a Language enum value.
const char* language_code(Language lang);

/// Detects the user's operating-system language (Windows UI language or POSIX LANG/LC_ALL).
Language detect_system_language();

/// Returns the UTF-8 translated string for `key` in the active language (falls back to English).
const char* tr(StrKey key);

} // namespace openrar::i18n
