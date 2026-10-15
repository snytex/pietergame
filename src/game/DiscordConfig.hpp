// Discord Rich Presence configuration.
#pragma once

#include <cstdlib>
#include <string>

namespace pg {

// Application ID of the "PieterGame 3" app on https://discord.com/developers/applications
// (Discord shows the app's name as "Playing PieterGame 3"). Upload the images from
// assets/discord/ under Rich Presence -> Art Assets using their file names as keys.
// The env var PG_DISCORD_APP_ID overrides this for testing.
inline constexpr const char* kDiscordAppId = "1560404348557861044";

inline std::string discordAppId() {
    if (const char* id = std::getenv("PG_DISCORD_APP_ID"); id && *id) return id;
    return kDiscordAppId;
}

}  // namespace pg
