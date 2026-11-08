#pragma once

namespace nativetv {
enum class Operation {
    None,
    Capabilities,
    Metrics,
    SystemSettings,
    Apps,
    OledCare,
    Game,
    ScreenSaver,
    HomeStatus,
    HomeCatalogue,
    HomeDiscover,
    HomeGuide,
    HomePicture,
    HomeNow,
    HomeBeat,
    Privacy,
    ServiceMenu,
    Setup,
    AppCatalog,
    InstallStatus,
    TitleDetails,
    SearchTitles,
    NetflixTitle,
    Recents,
    Control,
    OpenHomeItem,
    SetProfile,
    SetHomeLayout,
    EnableHome,
    DisableHome,
    SetHomeStartLayout,
    SetHomeStartup,
    UninstallApp,
    SetAppHidden,
    SetAppVisible,
    SetTileHiding,
    SetLgHomeLayout,
    SetPreload,
    SetBackgroundService,
    SetScreenTime, // /api/screentime: the screen-time limits and the idle power-off
    FetchPackage,
    ConfirmPackage,
    CancelPackage,
    Settings, // /api/settings: the dashboard's MQTT and device settings (setup's broker entry)
    AdminUnlock, // /api/admin/unlock: the four-digit admin PIN, answered with a session
    // Fork: the YouTube account for the subscriptions row (youtube_account.hpp).
    YouTubeAccount, // GET /api/youtube/account
    YouTubeLink,    // POST /api/youtube/account/link
    YouTubeUnlink,  // POST /api/youtube/account/unlink
    YouTubeUse,     // POST /api/youtube/account/use: the profile's account, by id or the next one
    HomeVideo       // POST /api/home/video: fork, Home's live wallpaper ({play})
};
}
