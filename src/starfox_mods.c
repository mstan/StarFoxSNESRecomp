#include "starfox_mods.h"

#if defined(RECOMP_LAUNCHER)
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct StarFoxLauncherModsContext {
  RecompLauncherCSettings *settings;
  const char *config_path;
  char error[256];
} StarFoxLauncherModsContext;

typedef struct StarFoxModFeatureInfo {
  const char *id;
  const char *name;
  const char *group;
  const char *description;
} StarFoxModFeatureInfo;

enum {
  kStarFoxModFeature_EnhancedWidescreen,
  kStarFoxModFeature_CrosshairColor,
  kStarFoxModFeature_GodMode,
  kStarFoxModFeature_GodNuke,
  kStarFoxModFeature_PresentationFps,
  kStarFoxModFeature_ShowFps,
  kStarFoxModFeature_Count,
};

static const char kStarFoxModsPackageId[] = "starfox-enhanced-builtins";
static const char kStarFoxModsPackageVersion[] = "0.1.0";
static const char kStarFoxModsPackageName[] = "Star Fox Enhanced Built-ins";
static const char kStarFoxModsAuthor[] = "kandowontu (starfox-enhanced)";
static const char kStarFoxModsSourceName[] = "starfox-enhanced";
static const char kStarFoxModsSourceUrl[] =
    "https://github.com/kandowontu/starfox-enhanced";

static const StarFoxModFeatureInfo kStarFoxModFeatures[] = {
  {
    "enhanced_widescreen", "Enhanced Widescreen", "Display",
    "Adapted from DisplayMode in starfox-enhanced; selects the separate "
    "native renderer output width. Disabling this returns to Authentic 4:3."
  },
  {
    "crosshair_color", "Crosshair Color", "Gameplay",
    "Tint the reticle and cockpit Super FX HUD crosshair color."
  },
  {
    "god_mode", "God Mode", "Gameplay",
    "Keep player invulnerability and bomb stock active during gameplay."
  },
  {
    "god_nuke", "God Nuke", "Gameplay",
    "Let held R turn a fired smart bomb into an immediate room-wide nuke."
  },
  {
    "presentation_fps", "Presentation FPS", "Presentation",
    "Choose the host presentation cadence while keeping simulation timing fixed."
  },
  {
    "show_fps", "Show FPS", "Presentation",
    "Show the measured host presentation rate in the window title."
  },
};

typedef struct StarFoxChoice {
  const char *value;
  const char *label;
} StarFoxChoice;

static const StarFoxChoice kDisplayModeChoices[] = {
  { "16:10", "Widescreen 16:10" },
  { "16:9", "Widescreen 16:9" },
  { "21:9", "Ultrawide 21:9" },
  { "32:9", "Super Ultrawide 32:9" },
};

static const StarFoxChoice kCrosshairChoices[] = {
  { "Original", "Original" },
  { "White", "White" },
  { "Green", "Green" },
  { "Blue", "Blue" },
  { "Red", "Red" },
  { "Yellow", "Yellow" },
  { "Cyan", "Cyan" },
  { "Magenta", "Magenta" },
  { "Orange", "Orange" },
};

static const StarFoxChoice kPresentationFpsChoices[] = {
  { "20", "20 FPS" },
  { "30", "30 FPS" },
  { "60", "60 FPS" },
  { "90", "90 FPS" },
  { "120", "120 FPS" },
  { "240", "240 FPS" },
  { "360", "360 FPS" },
  { "480", "480 FPS" },
};

static StarFoxLauncherModsContext g_starfox_mods_context;

static void mod_copy(char *dst, size_t dst_size, const char *src) {
  if (!dst || !dst_size) return;
  snprintf(dst, dst_size, "%s", src ? src : "");
}

static int feature_index(const char *feature_id) {
  if (!feature_id) return -1;
  for (int i = 0; i < kStarFoxModFeature_Count; i++) {
    if (StringEqualsNoCase(feature_id, kStarFoxModFeatures[i].id))
      return i;
  }
  return -1;
}

static const char *display_mode_value(void) {
  switch (g_config.widescreen_extra) {
  case 52: return "16:10";
  case 71: return "16:9";
  case 132: return "21:9";
  case 272: return "32:9";
  case 0:
  default: return "16:9";
  }
}

static const char *crosshair_value(void) {
  switch (g_config.crosshair_color) {
  case kCrosshairColor_White: return "White";
  case kCrosshairColor_Green: return "Green";
  case kCrosshairColor_Blue: return "Blue";
  case kCrosshairColor_Red: return "Red";
  case kCrosshairColor_Yellow: return "Yellow";
  case kCrosshairColor_Cyan: return "Cyan";
  case kCrosshairColor_Magenta: return "Magenta";
  case kCrosshairColor_Orange: return "Orange";
  case kCrosshairColor_Original:
  default: return "Original";
  }
}

static void sync_settings(void *ctx) {
  StarFoxLauncherModsContext *mod_ctx = (StarFoxLauncherModsContext *)ctx;
  if (mod_ctx && mod_ctx->settings) {
    mod_ctx->settings->widescreen =
        g_config.enhanced_renderer && g_config.widescreen_extra != 0;
    mod_ctx->settings->widescreen_hud = 0;
  }
}

static int package_count(void *ctx) {
  (void)ctx;
  return 1;
}

static int package_get(void *ctx, int index,
                       RecompLauncherCModPackage *out) {
  (void)ctx;
  if (!out || index != 0) return 0;
  memset(out, 0, sizeof(*out));
  mod_copy(out->id, sizeof(out->id), kStarFoxModsPackageId);
  mod_copy(out->version, sizeof(out->version), kStarFoxModsPackageVersion);
  mod_copy(out->name, sizeof(out->name), kStarFoxModsPackageName);
  mod_copy(out->author, sizeof(out->author), kStarFoxModsAuthor);
  mod_copy(out->description, sizeof(out->description),
           "Built-in options adapted from kandowontu's starfox-enhanced; "
           "widescreen uses a separate native renderer.");
  mod_copy(out->license, sizeof(out->license), "Mixed");
  mod_copy(out->source_name, sizeof(out->source_name),
           kStarFoxModsSourceName);
  mod_copy(out->source_url, sizeof(out->source_url), kStarFoxModsSourceUrl);
  mod_copy(out->status, sizeof(out->status), "Built in");
  out->enabled = 1;
  out->removable = 0;
  return 1;
}

static int no_package_option(void *ctx, const char *package_id, int index,
                             RecompLauncherCModOption *out) {
  (void)ctx; (void)package_id; (void)index; (void)out;
  return 0;
}

static int no_package_choice(void *ctx, const char *package_id,
                             const char *option_id, int index,
                             RecompLauncherCModChoice *out) {
  (void)ctx; (void)package_id; (void)option_id; (void)index; (void)out;
  return 0;
}

static int version_count(void *ctx, const char *package_id) {
  (void)ctx;
  return package_id && StringEqualsNoCase(package_id, kStarFoxModsPackageId)
      ? 1 : 0;
}

static int version_get(void *ctx, const char *package_id, int index,
                       RecompLauncherCModVersion *out) {
  (void)ctx;
  if (!out || index != 0 ||
      !package_id || !StringEqualsNoCase(package_id, kStarFoxModsPackageId))
    return 0;
  memset(out, 0, sizeof(*out));
  mod_copy(out->version, sizeof(out->version), kStarFoxModsPackageVersion);
  out->selected = 1;
  out->removable = 0;
  return 1;
}

static int unsupported(void *ctx, const char *unused) {
  (void)unused;
  StarFoxLauncherModsContext *mod_ctx = (StarFoxLauncherModsContext *)ctx;
  if (mod_ctx)
    mod_copy(mod_ctx->error, sizeof(mod_ctx->error),
             "Star Fox built-in mods are not installable packages.");
  return 0;
}

static int unsupported2(void *ctx, const char *a, const char *b) {
  (void)a; (void)b;
  return unsupported(ctx, NULL);
}

static bool feature_enabled(int index) {
  switch (index) {
  case kStarFoxModFeature_EnhancedWidescreen:
    return g_config.enhanced_renderer && g_config.widescreen_extra != 0;
  case kStarFoxModFeature_CrosshairColor:
    return g_config.crosshair_color != kCrosshairColor_Original;
  case kStarFoxModFeature_GodMode: return g_config.god_mode;
  case kStarFoxModFeature_GodNuke: return g_config.god_nuke;
  case kStarFoxModFeature_PresentationFps:
    return g_config.presentation_fps != 60;
  case kStarFoxModFeature_ShowFps: return g_config.show_fps;
  default: return false;
  }
}

static int feature_option_count(int index) {
  switch (index) {
  case kStarFoxModFeature_EnhancedWidescreen:
  case kStarFoxModFeature_CrosshairColor:
  case kStarFoxModFeature_PresentationFps:
    return 1;
  default:
    return 0;
  }
}

static int feature_enable(void *ctx, const char *package_id,
                          const char *feature_id, int enabled) {
  if (!package_id || !StringEqualsNoCase(package_id, kStarFoxModsPackageId))
    return 0;
  StarFoxLauncherModsContext *mod_ctx = (StarFoxLauncherModsContext *)ctx;
  if (mod_ctx) mod_ctx->error[0] = 0;
  switch (feature_index(feature_id)) {
  case kStarFoxModFeature_EnhancedWidescreen:
    g_config.widescreen_extra = enabled ? 71 : 0;
    g_config.enhanced_renderer = enabled != 0;
    break;
  case kStarFoxModFeature_CrosshairColor:
    g_config.crosshair_color =
        enabled ? kCrosshairColor_Green : kCrosshairColor_Original;
    break;
  case kStarFoxModFeature_GodMode:
    g_config.god_mode = enabled != 0;
    break;
  case kStarFoxModFeature_GodNuke:
    g_config.god_nuke = enabled != 0;
    break;
  case kStarFoxModFeature_PresentationFps:
    g_config.presentation_fps = enabled ? 120 : 60;
    break;
  case kStarFoxModFeature_ShowFps:
    g_config.show_fps = enabled != 0;
    break;
  default:
    return 0;
  }
  sync_settings(ctx);
  return 1;
}

static int set_package_enabled(void *ctx, const char *package_id,
                               int enabled) {
  if (!package_id || !StringEqualsNoCase(package_id, kStarFoxModsPackageId))
    return 0;
  for (int i = 0; i < kStarFoxModFeature_Count; i++)
    feature_enable(ctx, kStarFoxModsPackageId, kStarFoxModFeatures[i].id,
                   enabled);
  return 1;
}

static int package_set_option(void *ctx, const char *package_id,
                              const char *option_id, const char *value) {
  (void)package_id; (void)option_id; (void)value;
  return unsupported(ctx, NULL);
}

static int commit_mods(void *ctx, const char *image_path) {
  (void)image_path;
  StarFoxLauncherModsContext *mod_ctx = (StarFoxLauncherModsContext *)ctx;
  if (mod_ctx) mod_ctx->error[0] = 0;
  WriteConfigFile(mod_ctx ? mod_ctx->config_path : NULL);
  return 1;
}

static const char *last_error(void *ctx) {
  StarFoxLauncherModsContext *mod_ctx = (StarFoxLauncherModsContext *)ctx;
  return mod_ctx && mod_ctx->error[0] ? mod_ctx->error : "";
}

static int feature_count(void *ctx) {
  (void)ctx;
  return kStarFoxModFeature_Count;
}

static int feature_get(void *ctx, int index,
                       RecompLauncherCModFeature *out) {
  (void)ctx;
  if (!out || index < 0 || index >= kStarFoxModFeature_Count) return 0;
  const StarFoxModFeatureInfo *info = &kStarFoxModFeatures[index];
  memset(out, 0, sizeof(*out));
  mod_copy(out->id, sizeof(out->id), info->id);
  mod_copy(out->package_id, sizeof(out->package_id), kStarFoxModsPackageId);
  mod_copy(out->package_version, sizeof(out->package_version),
           kStarFoxModsPackageVersion);
  mod_copy(out->package_name, sizeof(out->package_name),
           kStarFoxModsPackageName);
  mod_copy(out->name, sizeof(out->name), info->name);
  mod_copy(out->author, sizeof(out->author), kStarFoxModsAuthor);
  mod_copy(out->description, sizeof(out->description), info->description);
  mod_copy(out->source_name, sizeof(out->source_name), kStarFoxModsSourceName);
  mod_copy(out->source_url, sizeof(out->source_url), kStarFoxModsSourceUrl);
  mod_copy(out->group, sizeof(out->group), info->group);
  mod_copy(out->status, sizeof(out->status),
           feature_enabled(index) ? "Enabled" : "Disabled");
  out->enabled = feature_enabled(index) ? 1 : 0;
  out->option_count = feature_option_count(index);
  return 1;
}

static int feature_option_get(void *ctx, const char *package_id,
                              const char *feature_id, int index,
                              RecompLauncherCModOption *out) {
  (void)ctx;
  if (!out || index != 0 ||
      !package_id || !StringEqualsNoCase(package_id, kStarFoxModsPackageId))
    return 0;
  const int fi = feature_index(feature_id);
  if (fi < 0 || !feature_option_count(fi)) return 0;
  memset(out, 0, sizeof(*out));
  out->type = RECOMP_MOD_OPTION_CHOICE;
  out->step = 1;
  switch (fi) {
  case kStarFoxModFeature_EnhancedWidescreen:
    mod_copy(out->id, sizeof(out->id), "mode");
    mod_copy(out->label, sizeof(out->label), "Aspect ratio");
    mod_copy(out->description, sizeof(out->description),
             "Enhanced-style viewport width.");
    mod_copy(out->value, sizeof(out->value), display_mode_value());
    mod_copy(out->default_value, sizeof(out->default_value), "16:9");
    out->choice_count = (int)countof(kDisplayModeChoices);
    return 1;
  case kStarFoxModFeature_CrosshairColor:
    mod_copy(out->id, sizeof(out->id), "color");
    mod_copy(out->label, sizeof(out->label), "Color");
    mod_copy(out->description, sizeof(out->description),
             "Crosshair and cockpit HUD tint.");
    mod_copy(out->value, sizeof(out->value), crosshair_value());
    mod_copy(out->default_value, sizeof(out->default_value), "Original");
    out->choice_count = (int)countof(kCrosshairChoices);
    return 1;
  case kStarFoxModFeature_PresentationFps:
    mod_copy(out->id, sizeof(out->id), "fps");
    mod_copy(out->label, sizeof(out->label), "FPS");
    mod_copy(out->description, sizeof(out->description),
             "Host presentation rate.");
    snprintf(out->value, sizeof(out->value), "%u",
             g_config.presentation_fps ? g_config.presentation_fps : 60);
    mod_copy(out->default_value, sizeof(out->default_value), "60");
    out->choice_count = (int)countof(kPresentationFpsChoices);
    return 1;
  default:
    return 0;
  }
}

static int feature_choice_get(void *ctx, const char *package_id,
                              const char *feature_id, const char *option_id,
                              int index, RecompLauncherCModChoice *out) {
  (void)ctx;
  if (!out || index < 0 ||
      !package_id || !StringEqualsNoCase(package_id, kStarFoxModsPackageId))
    return 0;
  const int fi = feature_index(feature_id);
  const StarFoxChoice *choices = NULL;
  int n = 0;
  if (fi == kStarFoxModFeature_EnhancedWidescreen &&
      StringEqualsNoCase(option_id, "mode")) {
    choices = kDisplayModeChoices;
    n = (int)countof(kDisplayModeChoices);
  } else if (fi == kStarFoxModFeature_CrosshairColor &&
             StringEqualsNoCase(option_id, "color")) {
    choices = kCrosshairChoices;
    n = (int)countof(kCrosshairChoices);
  } else if (fi == kStarFoxModFeature_PresentationFps &&
             StringEqualsNoCase(option_id, "fps")) {
    choices = kPresentationFpsChoices;
    n = (int)countof(kPresentationFpsChoices);
  } else {
    return 0;
  }
  if (index >= n) return 0;
  memset(out, 0, sizeof(*out));
  mod_copy(out->value, sizeof(out->value), choices[index].value);
  mod_copy(out->label, sizeof(out->label), choices[index].label);
  return 1;
}

static int feature_set_option(void *ctx, const char *package_id,
                              const char *feature_id, const char *option_id,
                              const char *value) {
  if (!package_id || !StringEqualsNoCase(package_id, kStarFoxModsPackageId) ||
      !option_id || !value)
    return 0;
  StarFoxLauncherModsContext *mod_ctx = (StarFoxLauncherModsContext *)ctx;
  if (mod_ctx) mod_ctx->error[0] = 0;
  const int fi = feature_index(feature_id);
  if (fi == kStarFoxModFeature_EnhancedWidescreen &&
      StringEqualsNoCase(option_id, "mode")) {
    if (StringEqualsNoCase(value, "16:10")) {
      g_config.widescreen_extra = 52;
      g_config.enhanced_renderer = true;
    } else if (StringEqualsNoCase(value, "16:9")) {
      g_config.widescreen_extra = 71;
      g_config.enhanced_renderer = true;
    } else if (StringEqualsNoCase(value, "21:9")) {
      g_config.widescreen_extra = 132;
      g_config.enhanced_renderer = true;
    } else if (StringEqualsNoCase(value, "32:9")) {
      g_config.widescreen_extra = 272;
      g_config.enhanced_renderer = true;
    } else {
      return 0;
    }
    sync_settings(ctx);
    return 1;
  }
  if (fi == kStarFoxModFeature_CrosshairColor &&
      StringEqualsNoCase(option_id, "color")) {
    for (int i = 0; i < (int)countof(kCrosshairChoices); i++) {
      if (StringEqualsNoCase(value, kCrosshairChoices[i].value)) {
        g_config.crosshair_color = (uint8)i;
        return 1;
      }
    }
    return 0;
  }
  if (fi == kStarFoxModFeature_PresentationFps &&
      StringEqualsNoCase(option_id, "fps")) {
    const int fps = atoi(value);
    for (int i = 0; i < (int)countof(kPresentationFpsChoices); i++) {
      if (fps == atoi(kPresentationFpsChoices[i].value)) {
        g_config.presentation_fps = (uint16)fps;
        return 1;
      }
    }
  }
  return 0;
}

static int diagnostic_count(void *ctx, const char *package_id,
                            const char *feature_id) {
  (void)ctx; (void)package_id; (void)feature_id;
  return 0;
}

static int diagnostic_get(void *ctx, const char *package_id,
                          const char *feature_id, int index,
                          RecompLauncherCModDiagnostic *out) {
  (void)ctx; (void)package_id; (void)feature_id; (void)index; (void)out;
  return 0;
}

const RecompLauncherCModProvider *StarFoxLauncherModsProvider(
    RecompLauncherCSettings *settings, const char *config_path) {
  static RecompLauncherCModProvider provider;
  g_starfox_mods_context.settings = settings;
  g_starfox_mods_context.config_path =
      config_path && *config_path ? config_path : "config.ini";
  g_starfox_mods_context.error[0] = 0;
  memset(&provider, 0, sizeof(provider));
  provider.ctx = &g_starfox_mods_context;
  provider.package_count = package_count;
  provider.package_get = package_get;
  provider.option_get = no_package_option;
  provider.choice_get = no_package_choice;
  provider.version_count = version_count;
  provider.version_get = version_get;
  provider.install_archive = unsupported;
  provider.remove_package = unsupported2;
  provider.set_enabled = set_package_enabled;
  provider.select_version = unsupported2;
  provider.set_option = package_set_option;
  provider.commit = commit_mods;
  provider.last_error = last_error;
  provider.feature_count = feature_count;
  provider.feature_get = feature_get;
  provider.feature_option_get = feature_option_get;
  provider.feature_choice_get = feature_choice_get;
  provider.feature_enable = feature_enable;
  provider.feature_set_option = feature_set_option;
  provider.diagnostic_count = diagnostic_count;
  provider.diagnostic_get = diagnostic_get;
  provider.archive_extension = ".snesmod";
  provider.archive_description = "SNESRecomp mod package (.snesmod)";
  provider.commit_netplay = commit_mods;
  return &provider;
}
#endif
