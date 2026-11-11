#include "ui/application.hpp"
#include "ui/controller_helpers.hpp"
#include "ui/home_layout.hpp"
#include <algorithm>

namespace nativetv {
namespace {
// Fork: the Glass Pill Tabs' categories, from what the TV really has: all the apps, each kind of
// app the TV has (in the owner's order of kinds) and its inputs when it has some. The apps row shows
// the selected one's apps. Settings is the top bar's gear, not a category.
void pillTabsOf(HomeState &home, const Json &catalogue, const Json &discover, const Json &guide) {
    CardRow all;
    for (const CardRow &row : home.rows)
        if (home::isApps(row))
            all = row;
    if (all.cards.empty())
        all = appsRow(catalogue, guide, false);
    struct Category {
        std::string key, label;
    };
    std::vector<Category> categories = {{"all", "Apps"}};
    for (const Json &kind : catalogue["kinds"].items()) {
        const std::string key = kind["key"].string();
        const bool used = std::any_of(all.cards.begin(), all.cards.end(),
                                      [&key](const Card &card) { return kindOf(card) == key; });
        if (used && key != "other")
            categories.push_back({key, kind["label"].string(key)});
    }
    std::vector<Card> inputs;
    for (const CardRow &row : homeRows("inputs", catalogue, discover, guide))
        inputs.insert(inputs.end(), row.cards.begin(), row.cards.end());
    if (!inputs.empty())
        categories.push_back({"inputs", "Inputs"});
    home.pillTab = std::min(home.pillTab, categories.size() - 1);
    CardRow tabs;
    tabs.key = "tabs";
    tabs.title = "Categories";
    for (const Category &category : categories) {
        Card card;
        card.id = "tab:" + category.key;
        card.kind = "tab";
        card.title = category.label;
        tabs.cards.push_back(card);
    }
    const std::string selected = categories[home.pillTab].key;
    CardRow apps = all;
    if (selected == "inputs") {
        apps.cards = inputs;
    } else if (selected != "all") {
        apps.cards.clear();
        for (const Card &card : all.cards)
            if (kindOf(card) == selected)
                apps.cards.push_back(card);
    }
    std::vector<CardRow> rows;
    for (const CardRow &row : home.rows)
        if (home::videoRow(row))
            rows.push_back(row); // the videos stand above the categories
    rows.push_back(tabs);
    rows.push_back(apps);
    home.rows = rows;
}
} // namespace
void Application::pillTabs(HomeState &home) {
    pillTabsOf(home, state_.catalogue, state_.discover, state_.guide);
}
void Application::rebuildHome() {
    // The cards being moved are the person's own order until it is saved.
    if (state_.home.editing)
        return;
    state_.home.tabs = homeTabs(state_.catalogue);
    auto &home = state_.home;
    home.tab = clampIndex(home.tab, home.tabs.size());
    std::string oldId;
    if (home.row < home.rows.size() && home.column < home.rows[home.row].cards.size())
        oldId = home.rows[home.row].cards[home.column].id;
    home.rows = homeRows(home.tabs[home.tab].key, state_.catalogue, state_.discover, state_.guide);
    // Fork: the Ribbon style's Home, and an app bar's, is its apps and one row of videos above
    // them (home_layout.hpp): the subscriptions when an account is signed in, else trending.
    if (home::strip() && home.tabs[home.tab].key == "home") {
        const bool subs = std::any_of(home.rows.begin(), home.rows.end(),
                                      [](const CardRow &row) { return row.key == "subs" && !row.cards.empty(); });
        home.rows.erase(std::remove_if(home.rows.begin(), home.rows.end(),
                                       [subs](const CardRow &row) {
                                           if (home::isApps(row))
                                               return false;
                                           if (!home::videoRow(row) || row.cards.empty())
                                               return true;
                                           return subs && row.key != "subs";
                                       }),
                        home.rows.end());
        // The videos above the apps, whatever the owner's order of Home's rows.
        std::stable_partition(home.rows.begin(), home.rows.end(),
                              [](const CardRow &row) { return home::videoRow(row); });
        if (home::bar().design == home::Bar::Pills)
            pillTabs(home);
    }
    home.arranging = false; // the rows are the server's again
    home.grid = home.tabs[home.tab].key == "apps" &&
                state_.catalogue["layout"]["sort"]["app"].string() != "kind";
    home.row = clampIndex(home.row, home.rows.size());
    if (home.row < home.rows.size()) {
        const auto &cards = home.rows[home.row].cards;
        home.column = clampIndex(home.column, cards.size());
        for (size_t i = 0; i < cards.size(); ++i)
            if (cards[i].id == oldId)
                home.column = i;
    }
    auto stops = navigationStops(state_);
    if (std::find(stops.begin(), stops.end(), home.nav) == stops.end())
        home.nav = home.tab;
    homeData();
    dirty_ = true;
}
void Application::refresh() {
    if (!active_)
        return;
    if (state_.screen == Screen::Dashboard)
        services_.read("menu", dashboardTabs()[state_.menu.tab].operation);
    else if (state_.screen == Screen::Setup)
        services_.read("setup", Operation::Setup);
    else if (state_.screen == Screen::Quick) {
        if (state_.menu.level == "recents")
            state_.recents.revision = services_.read("recents", Operation::Recents);
    } else if (state_.screen == Screen::Catalog) {
        services_.read("catalog", Operation::AppCatalog);
        services_.read("install", Operation::InstallStatus);
    } else {
        services_.read("catalogue", Operation::HomeCatalogue);
        if (state_.screen == Screen::Home) {
            services_.read("discover", Operation::HomeDiscover);
            if (state_.catalogue["layout"]["showLive"].boolean())
                services_.read("guide", Operation::HomeGuide);
        }
    }
    refreshAt_ = now_ + (state_.screen == Screen::Catalog ||
                                 (state_.screen == Screen::Setup && state_.menu.level == "handoff")
                             ? 2000
                             : 10000);
}
void Application::response(const Response &response) {
    if (response.channel == "beat") {
        homeBeat(&response);
        return;
    }
    askAdminPin(response);
    // The replies to writes are the layer's; reads pass through it and on.
    if (optimistic_.response(response)) {
        dirty_ = true;
        return;
    }
    if (screen_)
        screen_->response(*this, response);
    if (response.channel == "recents" &&
        (state_.screen != Screen::Quick || state_.menu.level != "recents" ||
         !state_.recents.revision || response.generation != state_.recents.revision))
        return;
    if (response.channel == "home-now" || response.channel == "home-picture") {
        pictureResponse(response);
        return;
    }
    Json data = response.json();
    dirty_ = true;
    if (response.channel == "action") {
        state_.busy = false;
        if (!response.ok() || !data.valid() || (data.has("ok") && !data["ok"].boolean())) {
            toast(data["error"].string("That did not work. Try again."));
            refresh();
            return;
        }
        if (pending_.operation == Operation::Setup) {
            std::string action = pending_.body["action"].string();
            if (action == "handoff") {
                state_.qr = data["url"].string();
            } else if (state_.screen == Screen::Setup && action == "network") {
                if (data["demo"].boolean())
                    state_.setup.set("network", pending_.body["open"]);
                route("setup:ha");
            } else if (state_.screen == Screen::Setup && action == "alwaysReady")
                route("setup:summary");
            else if (action == "done")
                route("dashboard:system");
        } else if ((pending_.operation == Operation::FetchPackage ||
                    pending_.operation == Operation::ConfirmPackage ||
                    pending_.operation == Operation::CancelPackage)) {
            if (data["install"].valid())
                state_.install = data["install"];
            services_.read("install", Operation::InstallStatus);
            rebuildMenu();
        } else if (pending_.operation == Operation::YouTubeUse) {
            toast("Showing " + data["account"]["name"].string("the next account") + "'s subscriptions");
        } else if (pending_.operation != Operation::OpenHomeItem &&
                   pending_.operation != Operation::Recents)
            toast("Saved");
        if (pending_.operation == Operation::Recents && state_.screen == Screen::Quick &&
            state_.menu.level == "recents") {
            if (pending_.body["action"].string() == "open" ||
                pending_.body["action"].string() == "closeAll")
                route("home");
            else
                recentResponse(data);
        }
        refresh();
        return;
    }
    if (!response.ok() || !data.valid() || (data.has("ok") && !data["ok"].boolean())) {
        if (response.channel == "catalogue") {
            clearPicture();
            // Nothing to show yet and nobody answering: say so rather than a blank page.
            if (!state_.catalogue.valid() && !state_.serverSilent) {
                state_.serverSilent = true;
                motion_.tween("fault", 1, kit::motion::ms::fast, Easing::out());
            }
        }
        else if (response.channel == "recents" && state_.screen == Screen::Quick &&
                 state_.menu.level == "recents")
            state_.recents.status = "Could not load recent apps. Press OK to retry.";
        else if (response.channel == "menu") {
            state_.menuData =
                fields({{"ok", Json::flag(false)},
                        {"error", Json::text("The server did not answer. Press OK to retry.")}});
            rebuildMenu();
        }
        return;
    }
    if (response.channel == "caps") {
        state_.caps = data;
        capsAnswered_ = true;
        // The rule of the old dashboard page: a server that says setup is needed opens setup first,
        // whichever screen the client was asked for. A setup screen already open (Settings'
        // phone link, or setup itself) stays: it is where the person was sent.
        if (data["setupNeeded"].boolean() &&
            !(screen_ && screen_->name().compare(0, 6, "setup:") == 0))
            route("setup:welcome");
    } else if (response.channel == "catalogue") {
        state_.catalogue = data;
        if (state_.serverSilent) {
            state_.serverSilent = false;
            motion_.tween("fault", 0, kit::motion::ms::fast, Easing::out());
        }
        // The ten-second refresh must not drop a lifted row: the answer is stored, and Back or
        // the save rebuilds from it.
        if (!state_.home.arranging)
            rebuildHome();
        syncWallpaper();
        if (state_.screen == Screen::Home && active_ && data["layout"]["picture"].boolean())
            state_.home.sourceGeneration = services_.read("home-now", Operation::HomeNow);
        else
            clearPicture();
    } else if (response.channel == "discover") {
        state_.discover = data;
        if (!state_.home.arranging)
            rebuildHome();
    } else if (response.channel == "guide") {
        state_.guide = data["guide"];
        if (!state_.home.arranging)
            rebuildHome();
    } else if (response.channel == "stats") {
        state_.stats = data;
    } else if (response.channel == "menu" && state_.screen == Screen::Dashboard) {
        state_.menuData = data;
        rebuildMenu();
    } else if (response.channel == "setup") {
        state_.setup = data;
        if (state_.screen == Screen::Setup) {
            if (state_.menu.level == "handoff" &&
                data["homeAssistant"]["state"].string() == "connected")
                route(data.has("alwaysReady") ? "setup:standby" : "setup:summary");
            else
                rebuildMenu();
        }
    } else if (response.channel == "catalog") {
        state_.catalog = data;
        if (state_.screen == Screen::Catalog)
            rebuildMenu();
    } else if (response.channel == "install") {
        if (data["jobId"].dump() != state_.install["jobId"].dump())
            state_.installRoot = state_.installReplace = false;
        state_.install = data;
        if (state_.screen == Screen::Catalog)
            rebuildMenu();
    } else if (response.channel == "recents" && state_.screen == Screen::Quick &&
               state_.menu.level == "recents" && response.generation == state_.recents.revision) {
        recentResponse(data);
    }
}
} // namespace nativetv
