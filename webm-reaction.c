// webm-reaction.c
// OBS Plugin: WebM Reaction Source
// Plays one WebM/video when silent, another when audio is detected.
//
// Based on obs-image-reaction by scaled:
//   https://github.com/scaledteam/obs-image-reaction
// License: GPL-2.0

#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>
#include <media-io/audio-math.h>
#include <math.h>

#define blog(log_level, format, ...) \
	blog(log_level, "[webm_reaction_source: '%s'] " format, \
		obs_source_get_name(context->source), ##__VA_ARGS__)

#define debug(format, ...) blog(LOG_DEBUG, format, ##__VA_ARGS__)
#define info(format,  ...) blog(LOG_INFO,  format, ##__VA_ARGS__)
#define warn(format,  ...) blog(LOG_WARNING, format, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */

struct webm_reaction_source {
	obs_source_t  *source;

	/* inner media sources that do the actual decoding */
	obs_source_t  *media_silent;
	obs_source_t  *media_loud;

	/* file paths chosen by the user */
	char *file_silent;
	char *file_loud;

	/* audio detection */
	char           source_name[256];
	obs_weak_source_t *audio_source;
	uint64_t       capture_check_time;

	float          threshold;   /* linear amplitude */
	float          smoothness;  /* IIR coefficient  */
	float          average;     /* running average  */

	bool           loud;
	bool           loud_old;
	bool           anim_reset_trigger;

	/* restart-on-switch options */
	bool           restart_silent;
	bool           restart_loud;
};

/* ------------------------------------------------------------------ */
/*  Helpers to (re)create inner media sources                          */
/* ------------------------------------------------------------------ */

static void create_media_source(struct webm_reaction_source *ctx,
                                obs_source_t **out,
                                const char *file)
{
	if (*out) {
		obs_source_release(*out);
		*out = NULL;
	}
	if (!file || !*file)
		return;

	obs_data_t *s = obs_data_create();
	obs_data_set_string(s, "local_file", file);
	obs_data_set_bool(s,   "is_local_file", true);
	obs_data_set_bool(s,   "looping", true);
	obs_data_set_bool(s,   "restart_on_activate", false);
	obs_data_set_bool(s,   "close_when_inactive", false);

	*out = obs_source_create_private("ffmpeg_source", NULL, s);
	obs_data_release(s);

	if (!*out)
		warn("Failed to create media source for '%s'", file);
}

static void restart_media(obs_source_t *media)
{
	if (!media)
		return;
	/* Send OBS_MEDIA_ACTION_RESTART via proc handler */
	calldata_t cd = {0};
	proc_handler_t *ph = obs_source_get_proc_handler(media);
	if (ph)
		proc_handler_call(ph, "restart", &cd);
	calldata_free(&cd);
}

/* ------------------------------------------------------------------ */
/*  OBS source callbacks                                               */
/* ------------------------------------------------------------------ */

static const char *webm_reaction_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("WebMReactionSource");
}

static void *webm_reaction_create(obs_data_t *settings, obs_source_t *source)
{
	struct webm_reaction_source *ctx =
		bzalloc(sizeof(struct webm_reaction_source));

	ctx->source           = source;
	ctx->source_name[0]   = '\0';
	ctx->loud             = false;

	/* will call create_media_source internally */
	obs_source_update(source, settings); /* triggers webm_reaction_update */

	(void)ctx; /* silence warning – returned below */
	return ctx;
}

static void audio_capture_cb(void *param, obs_source_t *src,
                              const struct audio_data *data, bool muted)
{
	struct webm_reaction_source *ctx = param;

	if (muted) {
		ctx->average = 0.0f;
	} else {
		uint32_t n      = data->frames;
		float   *samples = (float *)data->data[0];
		float    local   = 0.0f;
		for (uint32_t i = 0; i < n; i++)
			local += fabsf(samples[i]) / (float)n;
		ctx->average += ctx->smoothness * (local - ctx->average);
	}

	ctx->loud_old = ctx->loud;
	ctx->loud     = ctx->average > ctx->threshold;

	if (ctx->loud != ctx->loud_old)
		ctx->anim_reset_trigger = true;
}

static void remove_audio_capture(struct webm_reaction_source *ctx)
{
	if (!ctx->audio_source)
		return;
	obs_source_t *old = obs_weak_source_get_source(ctx->audio_source);
	if (old) {
		info("Removed audio capture from '%s'",
		     obs_source_get_name(old));
		obs_source_remove_audio_capture_callback(old,
		                                         audio_capture_cb,
		                                         ctx);
		obs_source_release(old);
	}
	obs_weak_source_release(ctx->audio_source);
	ctx->audio_source = NULL;
}

static void webm_reaction_update(void *data, obs_data_t *settings)
{
	struct webm_reaction_source *ctx = data;

	const char *file_silent = obs_data_get_string(settings, "file_silent");
	const char *file_loud   = obs_data_get_string(settings, "file_loud");
	const char *src_name    = obs_data_get_string(settings, "audio_source");
	double threshold        = obs_data_get_double(settings, "threshold");
	double smoothness       = obs_data_get_double(settings, "smoothness");

	ctx->restart_silent = obs_data_get_bool(settings, "restart_silent");
	ctx->restart_loud   = obs_data_get_bool(settings, "restart_loud");
	ctx->threshold      = (float)db_to_mul((float)threshold);
	ctx->smoothness     = (float)pow(0.1, smoothness);

	/* Re-create media sources if files changed */
	bool changed_silent = (!ctx->file_silent && file_silent && *file_silent)
	                   || (ctx->file_silent && strcmp(ctx->file_silent, file_silent) != 0);
	bool changed_loud   = (!ctx->file_loud && file_loud && *file_loud)
	                   || (ctx->file_loud && strcmp(ctx->file_loud, file_loud) != 0);

	if (changed_silent) {
		bfree(ctx->file_silent);
		ctx->file_silent = bstrdup(file_silent);
		create_media_source(ctx, &ctx->media_silent, file_silent);
	}
	if (changed_loud) {
		bfree(ctx->file_loud);
		ctx->file_loud = bstrdup(file_loud);
		create_media_source(ctx, &ctx->media_loud, file_loud);
	}

	/* Update audio capture source */
	bool src_changed = (src_name[0] == '\0')
	                 ? (ctx->source_name[0] != '\0')
	                 : (strcmp(ctx->source_name, src_name) != 0);

	if (src_changed) {
		remove_audio_capture(ctx);
		if (src_name[0] != '\0') {
			strncpy(ctx->source_name, src_name,
			        sizeof(ctx->source_name) - 1);
			ctx->source_name[sizeof(ctx->source_name) - 1] = '\0';
			/* Try to attach immediately on next tick */
			ctx->capture_check_time = os_gettime_ns() - 3000000000ULL;
		} else {
			ctx->source_name[0] = '\0';
		}
	}
}

static void webm_reaction_destroy(void *data)
{
	struct webm_reaction_source *ctx = data;

	remove_audio_capture(ctx);

	if (ctx->media_silent) {
		obs_source_release(ctx->media_silent);
		ctx->media_silent = NULL;
	}
	if (ctx->media_loud) {
		obs_source_release(ctx->media_loud);
		ctx->media_loud = NULL;
	}

	bfree(ctx->file_silent);
	bfree(ctx->file_loud);
	bfree(ctx);
}

static void webm_reaction_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);
	struct webm_reaction_source *ctx = data;

	/* Try to attach audio capture if not yet connected */
	if (ctx->source_name[0] != '\0' && !ctx->audio_source) {
		uint64_t t = os_gettime_ns();
		if (t - ctx->capture_check_time > 3000000000ULL) {
			ctx->capture_check_time = t;
			obs_source_t *cap =
				obs_get_source_by_name(ctx->source_name);
			if (cap) {
				info("Added audio capture to '%s'",
				     obs_source_get_name(cap));
				obs_source_add_audio_capture_callback(
					cap, audio_capture_cb, ctx);
				ctx->audio_source =
					obs_source_get_weak_source(cap);
				obs_source_release(cap);
			}
		}
	}

	/* Handle switch + optional restart */
	if (ctx->anim_reset_trigger) {
		ctx->anim_reset_trigger = false;
		if (ctx->loud && ctx->restart_loud)
			restart_media(ctx->media_loud);
		else if (!ctx->loud && ctx->restart_silent)
			restart_media(ctx->media_silent);
	}
}

static uint32_t webm_reaction_get_width(void *data)
{
	struct webm_reaction_source *ctx = data;
	obs_source_t *active = ctx->loud ? ctx->media_loud : ctx->media_silent;
	return active ? obs_source_get_width(active) : 0;
}

static uint32_t webm_reaction_get_height(void *data)
{
	struct webm_reaction_source *ctx = data;
	obs_source_t *active = ctx->loud ? ctx->media_loud : ctx->media_silent;
	return active ? obs_source_get_height(active) : 0;
}

static void webm_reaction_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);
	struct webm_reaction_source *ctx = data;
	obs_source_t *active = ctx->loud ? ctx->media_loud : ctx->media_silent;
	if (active)
		obs_source_video_render(active);
}

/* ------------------------------------------------------------------ */
/*  Properties UI                                                      */
/* ------------------------------------------------------------------ */

static bool add_audio_source_to_list(void *param, obs_source_t *src)
{
	obs_property_t *list = param;
	uint32_t caps = obs_source_get_output_flags(src);
	if ((caps & OBS_SOURCE_AUDIO) == 0)
		return true;
	const char *name = obs_source_get_name(src);
	obs_property_list_add_string(list, name, name);
	return true;
}

static obs_properties_t *webm_reaction_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	/* File: silent */
	const char *vid_filter =
		"Video/WebM Files (*.webm *.mp4 *.mov *.avi *.mkv *.gif);;"
		"WebM Files (*.webm);;"
		"MP4 Files (*.mp4);;"
		"GIF Files (*.gif);;"
		"All Files (*.*)";

	obs_properties_add_path(props, "file_silent",
	                        obs_module_text("FileSilent"),
	                        OBS_PATH_FILE, vid_filter, NULL);
	obs_properties_add_bool(props, "restart_silent",
	                        obs_module_text("RestartOnSilent"));

	/* File: loud */
	obs_properties_add_path(props, "file_loud",
	                        obs_module_text("FileLoud"),
	                        OBS_PATH_FILE, vid_filter, NULL);
	obs_properties_add_bool(props, "restart_loud",
	                        obs_module_text("RestartOnLoud"));

	/* Audio source list */
	obs_property_t *src_list =
		obs_properties_add_list(props, "audio_source",
		                        obs_module_text("AudioSource"),
		                        OBS_COMBO_TYPE_LIST,
		                        OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(src_list, "", "");
	obs_enum_sources(add_audio_source_to_list, src_list);

	/* Threshold */
	obs_property_t *p =
		obs_properties_add_float_slider(props, "threshold",
		                                obs_module_text("Threshold"),
		                                -60.0, 0.0, 0.1);
	obs_property_float_set_suffix(p, " dB");

	/* Smoothness */
	obs_properties_add_float_slider(props, "smoothness",
	                                obs_module_text("Smoothness"),
	                                0.0, 5.0, 0.1);

	return props;
}

static void webm_reaction_defaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "audio_source", "");
	obs_data_set_default_double(settings, "threshold",  -40.0);
	obs_data_set_default_double(settings, "smoothness",   1.0);
	obs_data_set_default_bool(settings,   "restart_silent", false);
	obs_data_set_default_bool(settings,   "restart_loud",   false);
}

/* ------------------------------------------------------------------ */
/*  Module registration                                                */
/* ------------------------------------------------------------------ */

static struct obs_source_info webm_reaction_source_info = {
	.id             = "webm_reaction_source",
	.type           = OBS_SOURCE_TYPE_INPUT,
	.output_flags   = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW,
	.get_name       = webm_reaction_get_name,
	.create         = webm_reaction_create,
	.destroy        = webm_reaction_destroy,
	.update         = webm_reaction_update,
	.get_defaults   = webm_reaction_defaults,
	.get_width      = webm_reaction_get_width,
	.get_height     = webm_reaction_get_height,
	.video_render   = webm_reaction_render,
	.video_tick     = webm_reaction_tick,
	.get_properties = webm_reaction_properties,
	.icon_type      = OBS_ICON_TYPE_MEDIA,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("webm-reaction", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "WebM/Video Reaction Source — switches video based on audio level";
}

bool obs_module_load(void)
{
	obs_register_source(&webm_reaction_source_info);
	return true;
}
