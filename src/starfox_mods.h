#pragma once

#if defined(RECOMP_LAUNCHER)
#include "recomp_launcher.h"

const RecompLauncherCModProvider *StarFoxLauncherModsProvider(
    RecompLauncherCSettings *settings, const char *config_path);
#endif
