#include <pd_api.h>
#include <tamalib.h>

#include "hal_types.h"

#define PIXEL_SIZE					4
#define PIXEL_OFFSET				1
#define ICON_STRIDE_X				38
#define ICON_STRIDE_Y				118
#define ICON_OFFSET_X				128
#define ICON_OFFSET_Y				46
#define LCD_OFFET_X					(400 - LCD_WIDTH * (PIXEL_SIZE + PIXEL_OFFSET)) / 2
#define LCD_OFFET_Y					(240 - LCD_HEIGHT * (PIXEL_SIZE + PIXEL_OFFSET)) / 2
#define TS_FREQ						1000000 // us
#define FRAME_FREQ					10 / TS_FREQ // 10 fps
#define SAVE_FREQ					TS_FREQ * 60 * 5 // 5 minutes

// hal
static PlaydateAPI* g_hal_pd;
static bool_t g_hal_matrix_buffer[LCD_HEIGHT][LCD_WIDTH] = { {0} };
static bool_t g_hal_icon_buffer[ICON_NUM] = { 0 };
static u32_t g_hal_sound_freq = 0;

// resources
static u12_t* g_program = NULL;
static LCDBitmapTable* g_icons_enabled = NULL;
static LCDBitmapTable* g_icons_disabled = NULL;
static LCDBitmap* g_background = NULL;
static PDSynth* g_synth;

// timestamps
static timestamp_t g_sleep_ts = 0;
static timestamp_t g_screen_ts = 0;
static timestamp_t g_save_ts = 0;

const char* s_rom_path = "rom.bin";
const char* s_save_path = "save.bin";

static void* hal_malloc(u32_t size)
{
	return NULL;
}

static void hal_free(void* ptr)
{
}

static void hal_halt(void)
{
}

static bool_t hal_is_log_enabled(log_level_t level)
{
	return 0;
}

static void hal_log(log_level_t level, char* buff, ...)
{
#if 0
	va_list arg_list;
	va_start(arg_list, buff);
	g_hal_pd->system->logToConsole(buff, arg_list);
	va_end(arg_list);
#endif
}

static timestamp_t hal_get_timestamp(void)
{
	return g_hal_pd->system->getCurrentTimeMilliseconds() * 1000;
}

static void hal_sleep_until(timestamp_t ts)
{
	const int32_t diff_usec = (int32_t)(ts - hal_get_timestamp());
	if (diff_usec > 0)
		g_sleep_ts = ts;
}

static void hal_update_screen(void)
{
}

static void hal_set_lcd_matrix(u8_t x, u8_t y, bool_t val)
{
	g_hal_matrix_buffer[y][x] = val;
}

static void hal_set_lcd_icon(u8_t icon, bool_t val)
{
	g_hal_icon_buffer[icon] = val;
}

static void hal_set_frequency(u32_t freq)
{
	g_hal_sound_freq = freq / 10;
}

static void hal_play_frequency(bool_t en)
{
	if (en)
		g_hal_pd->sound->synth->playNote(g_synth, (float)g_hal_sound_freq, 1.0f, -1.0f, 0);
	else
		g_hal_pd->sound->synth->noteOff(g_synth, 0);
}

static int hal_handler(void)
{
	return 0;
}

static hal_t hal = {
	.malloc = &hal_malloc,
	.free = &hal_free,
	.halt = &hal_halt,
	.is_log_enabled = &hal_is_log_enabled,
	.log = &hal_log,
	.sleep_until = &hal_sleep_until,
	.get_timestamp = &hal_get_timestamp,
	.update_screen = &hal_update_screen,
	.set_lcd_matrix = &hal_set_lcd_matrix,
	.set_lcd_icon = &hal_set_lcd_icon,
	.set_frequency = &hal_set_frequency,
	.play_frequency = &hal_play_frequency,
	.handler = &hal_handler,
};

int load_rom(PlaydateAPI* pd, const char* path)
{
	SDFile* tama_file = pd->file->open(path, kFileRead);
	if (tama_file == NULL)
	{
		pd->system->error("Couldn't open ROM %s", path);
		return -1;
	}

	if (pd->file->seek(tama_file, 0, SEEK_END) != 0)
	{
		pd->system->error("Couldn't seek file %s", path);
		pd->file->close(tama_file);
		return -1;
	}

	int size = pd->file->tell(tama_file);
	if (size == -1)
	{
		pd->system->error("Couldn't tell file %s", path);
		pd->file->close(tama_file);
		return -1;
	}
	size = size / 2;

	g_program = (u12_t*)pd->system->realloc(NULL, size * sizeof(u12_t));
	if (g_program == NULL)
	{
		pd->system->error("Couldn't allocate ROM memory");
		pd->file->close(tama_file);
		return -1;
	}

	if (pd->file->seek(tama_file, 0, SEEK_SET) != 0)
	{
		pd->system->error("Couldn't seek file %s", path);
		pd->file->close(tama_file);
		return -1;
	}

	uint8_t buf[2];
	for (uint32_t i = 0; i < (uint32_t)size; i++) {
		if (pd->file->read(tama_file, buf, 2) != 2)
		{
			pd->system->error("Couldn't read ROM %s", path);
			pd->file->close(tama_file);
			return -1;
		}
		g_program[i] = buf[1] | ((buf[0] & 0xF) << 8);
	}

	if (pd->file->close(tama_file) != 0)
	{
		pd->system->error("Couldn't close file %s", path);
		return -1;
	}
	return 0;
}

#define SAVE_OP(op, var, count)	if (pd->file->op(save_file, state->var, sizeof(*state->var) * count) != sizeof(*state->var) * count)\
								{ pd->system->error("Couldn't " #op " save %s", path); pd->file->close(save_file); return -1; }

#define SAVE_READ(var) SAVE_OP(read, var, 1)
#define SAVE_READ_ARRAY(var, count) SAVE_OP(read, var, count)
#define SAVE_WRITE(var) SAVE_OP(write, var, 1)
#define SAVE_WRITE_ARRAY(var, count) SAVE_OP(write, var, count)

int read_save(PlaydateAPI* pd, const char* path)
{
	SDFile* save_file = pd->file->open(path, kFileReadData);
	if (save_file != NULL)
	{
		state_t* state = cpu_get_state();

		SAVE_READ(pc);
		SAVE_READ(x);
		SAVE_READ(y);
		SAVE_READ(a);
		SAVE_READ(b);
		SAVE_READ(np);
		SAVE_READ(sp);
		SAVE_READ(flags);
		SAVE_READ(tick_counter);
		SAVE_READ(clk_timer_2hz_timestamp);
		SAVE_READ(clk_timer_4hz_timestamp);
		SAVE_READ(clk_timer_8hz_timestamp);
		SAVE_READ(clk_timer_16hz_timestamp);
		SAVE_READ(clk_timer_32hz_timestamp);
		SAVE_READ(clk_timer_64hz_timestamp);
		SAVE_READ(clk_timer_128hz_timestamp);
		SAVE_READ(clk_timer_256hz_timestamp);
		SAVE_READ(prog_timer_timestamp);
		SAVE_READ(prog_timer_enabled);
		SAVE_READ(prog_timer_data);
		SAVE_READ(prog_timer_rld);
		SAVE_READ(call_depth);
		SAVE_READ(cpu_halted);
		SAVE_READ_ARRAY(interrupts, INT_SLOT_NUM);
		SAVE_READ_ARRAY(memory, MEM_BUFFER_SIZE);

		pd->file->close(save_file);
	}
	return 0;
}

int write_save(PlaydateAPI* pd, const char* path)
{
	state_t* state = cpu_get_state();
	SDFile* save_file = pd->file->open(path, kFileWrite);
	if (save_file == NULL)
	{
		pd->system->error("Couldn't open save %s", path);
		return -1;
	}

	SAVE_WRITE(pc);
	SAVE_WRITE(x);
	SAVE_WRITE(y);
	SAVE_WRITE(a);
	SAVE_WRITE(b);
	SAVE_WRITE(np);
	SAVE_WRITE(sp);
	SAVE_WRITE(flags);
	SAVE_WRITE(tick_counter);
	SAVE_WRITE(clk_timer_2hz_timestamp);
	SAVE_WRITE(clk_timer_4hz_timestamp);
	SAVE_WRITE(clk_timer_8hz_timestamp);
	SAVE_WRITE(clk_timer_16hz_timestamp);
	SAVE_WRITE(clk_timer_32hz_timestamp);
	SAVE_WRITE(clk_timer_64hz_timestamp);
	SAVE_WRITE(clk_timer_128hz_timestamp);
	SAVE_WRITE(clk_timer_256hz_timestamp);
	SAVE_WRITE(prog_timer_timestamp);
	SAVE_WRITE(prog_timer_enabled);
	SAVE_WRITE(prog_timer_data);
	SAVE_WRITE(prog_timer_rld);
	SAVE_WRITE(call_depth);
	SAVE_WRITE(cpu_halted);
	SAVE_WRITE_ARRAY(interrupts, INT_SLOT_NUM);
	SAVE_WRITE_ARRAY(memory, MEM_BUFFER_SIZE);

	if (pd->file->flush(save_file) != 0)
	{
		pd->system->error("Couldn't flush save %s", path);
		return -1;
	}
	if (pd->file->close(save_file) != 0)
	{
		pd->system->error("Couldn't close save %s", path);
		return -1;
	}
	return 0;
}

int load_assets(PlaydateAPI* pd)
{
	const char* err;

	const char* icons_enabled_path = "assets/icons_enabled";
	g_icons_enabled = pd->graphics->loadBitmapTable(icons_enabled_path, &err);
	if (g_icons_enabled == NULL)
	{
		pd->system->error("Couldn't load table %s: %s", icons_enabled_path, err);
		return -1;
	}

	const char* icons_disabled_path = "assets/icons_disabled";
	g_icons_disabled = pd->graphics->loadBitmapTable(icons_disabled_path, &err);
	if (g_icons_disabled == NULL)
	{
		pd->system->error("Couldn't load table %s: %s", icons_disabled_path, err);
		return -1;
	}

	const char* background_path = "assets/background";
	g_background = pd->graphics->loadBitmap(background_path, &err);
	if (g_background == NULL)
	{
		pd->system->error("Couldn't load image %s: %s", background_path, err);
		return -1;
	}

	g_synth = pd->sound->synth->newSynth();
	if (g_synth == NULL)
	{
		pd->system->error("Couldn't create synth");
		return -1;
	}
	pd->sound->channel->addSource(pd->sound->getDefaultChannel(), (SoundSource*)g_synth);

	return 0;
}

void clean(PlaydateAPI* pd)
{
	if (g_icons_enabled != NULL)
		pd->graphics->freeBitmapTable(g_icons_enabled);
	if (g_icons_disabled != NULL)
		pd->graphics->freeBitmapTable(g_icons_disabled);
	if (g_background != NULL)
		pd->graphics->freeBitmap(g_background);
	if (g_synth != NULL)
		pd->sound->synth->freeSynth(g_synth);

	if (g_program != NULL)
		pd->system->realloc(g_program, 0);

	tamalib_release();
}

static int update(void* userdata);

#ifdef _WINDLL
__declspec(dllexport)
#endif
int eventHandler(PlaydateAPI* pd, PDSystemEvent event, uint32_t arg)
{
	(void)arg; // arg is currently only used for event = kEventKeyPressed

	if (event == kEventInit)
	{
		g_hal_pd = pd;

		if (load_rom(pd, s_rom_path) == -1)
		{
			clean(pd);
			return -1;
		}


		if (load_assets(pd) == -1)
		{
			clean(pd);
			return -1;
		}

		pd->system->setAutoLockDisabled(1);
		pd->display->setRefreshRate(0);

		pd->system->setUpdateCallback(update, pd);

		tamalib_set_speed(1);
		tamalib_register_hal(&hal);
		if (tamalib_init((const u12_t*)g_program, NULL, TS_FREQ))
		{
			pd->system->error("Couldn't initialize tamalib");
			clean(pd);
			return -1;
		}

		if (read_save(pd, s_save_path) == -1)
		{
			clean(pd);
			return -1;
		}
		g_save_ts = hal_get_timestamp();
	}
	else if (event == kEventTerminate)
	{
		int err = write_save(pd, s_save_path);
		clean(pd);
		return err;
	}
	return 0;
}

static int update(void* userdata)
{
	PlaydateAPI* pd = userdata;

	PDButtons pushed;
	PDButtons released;
	pd->system->getButtonState(NULL, &pushed, &released);

	if (pushed & kButtonRight)
		tamalib_set_button(BTN_LEFT, BTN_STATE_PRESSED);
	if (pushed & kButtonB)
		tamalib_set_button(BTN_MIDDLE, BTN_STATE_PRESSED);
	if (pushed & kButtonA)
		tamalib_set_button(BTN_RIGHT, BTN_STATE_PRESSED);

	if (released & kButtonRight)
		tamalib_set_button(BTN_LEFT, BTN_STATE_RELEASED);
	if (released & kButtonB)
		tamalib_set_button(BTN_MIDDLE, BTN_STATE_RELEASED);
	if (released & kButtonA)
		tamalib_set_button(BTN_RIGHT, BTN_STATE_RELEASED);

	while ((int32_t)(g_sleep_ts - hal_get_timestamp()) < 0)
		tamalib_step();


	const timestamp_t ts = hal_get_timestamp();
	// Save
	if (ts - g_save_ts >= SAVE_FREQ)
	{
		g_save_ts = ts;
		pd->system->logToConsole("auto save");
		if (!write_save(pd, s_save_path))
			return -1;
	}

	// Update the screen
	if (ts - g_screen_ts >= FRAME_FREQ)
	{
		g_screen_ts = ts;

		pd->graphics->clear(kColorWhite);
		pd->graphics->drawBitmap(g_background, 0, 0, kBitmapUnflipped);

		// Dot matrix
		for (u8_t j = 0; j < LCD_HEIGHT; j++) {
			for (u8_t i = 0; i < LCD_WIDTH; i++) {
				if (g_hal_matrix_buffer[j][i]) {
					pd->graphics->fillRect(i * (PIXEL_SIZE + PIXEL_OFFSET) + LCD_OFFET_X, j * (PIXEL_SIZE + PIXEL_OFFSET) + LCD_OFFET_Y, PIXEL_SIZE, PIXEL_SIZE, kColorBlack);
				}
			}
		}

		// Icons
		for (u8_t i = 0; i < ICON_NUM; i++)
		{
			LCDBitmapTable* icons = g_hal_icon_buffer[i] ? g_icons_enabled : g_icons_disabled;
			pd->graphics->drawBitmap(pd->graphics->getTableBitmap(icons, i), (i % 4) * ICON_STRIDE_X + ICON_OFFSET_X, (i / 4) * ICON_STRIDE_Y + ICON_OFFSET_Y, kBitmapUnflipped);
		}

		return 1;
	}

	return 0;
}

