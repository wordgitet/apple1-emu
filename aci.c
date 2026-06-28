#ifndef APPLE1_OMIT_ACI
#include "aci.h"
#include "bus.h"
#include "port.h"

struct aci_card {
	uint8_t rom[256];
	struct bus *bus;
	uint32_t *durations;
	uint32_t *recorded_durations;
	uint32_t current_cycle;
	uint32_t last_output_toggle_cycle;
	uint32_t last_read_cycle;
	int32_t cycles_until_toggle;
	uint32_t num_durations;
	uint32_t playback_index;
	uint32_t recorded_capacity;
	uint32_t recorded_count;
	bool input_level;
	bool initial_level;
	bool output_initial_level;
	bool output_level;
	bool recording_started;
	bool tape_active;
	bool tape_loaded;
};

static void
aci_record_toggle(struct aci_card *aci)
{
	uint32_t new_cap;
	uint32_t *buf;
	uint32_t delta;

	if (aci->recording_started == 0) {
		/* First toggle: capture initial output level before toggling */
		aci->output_initial_level = aci->output_level;
		aci->last_output_toggle_cycle = aci->current_cycle;
		aci->recording_started = true;
	} else {
		delta = aci->current_cycle - aci->last_output_toggle_cycle;
		if (delta > 0 && delta <= UINT32_MAX) {
			/* Grow recording buffer if needed */
			if (aci->recorded_count >= aci->recorded_capacity) {
				new_cap = aci->recorded_capacity == 0
				    ? 4096
				    : aci->recorded_capacity * 2;
				buf = port_realloc(aci->recorded_durations,
				    new_cap * sizeof(uint32_t));
				if (buf != NULL) {
					aci->recorded_durations = buf;
					aci->recorded_capacity = new_cap;
				}
			}
			if (aci->recorded_count < aci->recorded_capacity) {
				aci->recorded_durations[aci->recorded_count++] =
				    (uint32_t)delta;
			}
		}
		aci->last_output_toggle_cycle = aci->current_cycle;
	}
	aci->output_level = !aci->output_level;
}

static uint8_t
aci_read(void *ctx, uint16_t addr, bool is_dummy)
{
	struct aci_card *aci;
	uint8_t result;
	uint32_t gap;

	(void)is_dummy;
	aci = (struct aci_card *)ctx;

	/* ROM resides at $C100 - $C1FF */
	if (addr >= 0xC100 && addr <= 0xC1FF) {
		return (aci->rom[addr - 0xC100]);
	}

	/* Tape Input Register resides at $C081 */
	if (addr == 0xC081) {
		/* Leader preservation rewind check:
		 * 500 ms at ~1.023 MHz clock = 511,500 cycles. */
		gap = aci->current_cycle - aci->last_read_cycle;
		if (aci->tape_loaded != 0 && aci->playback_index > 0 &&
		    gap > 511500) {
			/* Rewind / Arm tape at start */
			aci->playback_index = 0;
			aci->cycles_until_toggle = aci->durations[0];
			aci->input_level = aci->initial_level;
			aci->tape_active = true;
		}
		aci->last_read_cycle = aci->current_cycle;
		result = aci->input_level != 0 ? 0x80 : 0x00;
		aci_record_toggle(aci); /* C081 access also toggles output FF */
		return (result);
	}

	/* Other $C000 - $C0FF addresses toggle ACI output level */
	if (addr >= 0xC000 && addr <= 0xC0FF) {
		aci_record_toggle(aci);
	}

	return (0x00);
}

static void
aci_write(void *ctx, uint16_t addr, uint8_t val, bool is_dummy)
{
	struct aci_card *aci;

	(void)val;
	(void)is_dummy;
	aci = (struct aci_card *)ctx;

	/* Any write to $C000 - $C0FF (except $C081) toggles ACI output level */
	if (addr >= 0xC000 && addr <= 0xC0FF && addr != 0xC081) {
		aci_record_toggle(aci);
	}
}

static void
aci_tick(void *ctx, uint8_t cycles)
{
	struct aci_card *aci;

	aci = (struct aci_card *)ctx;
	aci->current_cycle += cycles;

	if (aci->tape_loaded != 0 && aci->tape_active != 0) {
		aci->cycles_until_toggle -= cycles;
		while (aci->cycles_until_toggle <= 0 && aci->tape_active != 0) {
			aci->input_level = !aci->input_level;
			aci->playback_index++;
			if (aci->playback_index >= aci->num_durations) {
				aci->tape_active = false;
				break;
			}
			aci->cycles_until_toggle +=
			    aci->durations[aci->playback_index];
		}
	}
}

struct expansion_card *
aci_create(struct bus *bus, const char *rom_path)
{
	struct aci_card *aci;
	struct expansion_card *card;
	void *f;
	long size;

	if (rom_path == NULL)
		return (NULL);

	f = port_vfs_default.open(rom_path, PORT_VFS_READ);
	if (f == NULL)
		return (NULL);

	size = port_vfs_default.size(f);

	if (size != 256) {
		char msg[128];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: ACI ROM '%s' is %ld bytes, must be exactly 256.",
		    rom_path,
		    size);
		BUS_LOG(bus, BUS_LOG_ERROR, msg);
		port_vfs_default.close(f);
		return (NULL);
	}

	aci = (struct aci_card *)port_malloc(sizeof(struct aci_card));
	if (aci == NULL) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Failed to allocate ACI card context");
		port_vfs_default.close(f);
		return (NULL);
	}
	port_memset(aci, 0, sizeof(struct aci_card));
	aci->bus = bus;

	{
		port_size_t read_bytes;
		read_bytes = 0;
		if (port_vfs_default.read(f, aci->rom, 256, &read_bytes) != 0 ||
		    read_bytes != 256) {
			char msg[128];
			port_snprintf(msg,
			    sizeof(msg),
			    "Error: Failed to read ACI ROM '%s'",
			    rom_path);
			BUS_LOG(bus, BUS_LOG_ERROR, msg);
			port_free(aci);
			port_vfs_default.close(f);
			return (NULL);
		}
	}
	port_vfs_default.close(f);

	card = (struct expansion_card *)port_malloc(
	    sizeof(struct expansion_card));
	if (card == NULL) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Failed to allocate ACI expansion card");
		port_free(aci);
		return (NULL);
	}

	card->name = "ACI";
	card->base = 0xC000;
	card->mask = 0xFE00;
	card->rom_only = false;
	card->read = aci_read;
	card->write = aci_write;
	card->tick = aci_tick;
	card->ctx = aci;

	BUS_LOG(bus,
	    BUS_LOG_INFO,
	    "Registered ACI card: ROM mapped at 0xC100 - 0xC1FF, registers "
	    "at 0xC000 - 0xC0FF");
	return (card);
}

static port_result_t
pcm_to_durations(struct bus *bus,
    const float *mono,
    uint32_t num_samples,
    uint32_t sample_rate,
    uint32_t **out_durations,
    uint32_t *out_count,
    bool *out_initial_level)
{
	uint32_t *durations, *new_buf;
	uint32_t cap, count, first_active, i, last_transition;
	float threshold;
	bool current_level;

	if (num_samples == 0 || sample_rate == 0) {
		return (PORT_INVALID);
	}

	threshold = 0.02f;
	first_active = 0;

	while (first_active < num_samples &&
	    (mono[first_active] < 0.0f ? -mono[first_active]
				       : mono[first_active]) < threshold) {
		first_active++;
	}

	if (first_active == num_samples) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Error: Audio file does not contain a detectable "
		    "cassette signal");
		return (PORT_PROTOCOL);
	}

	*out_initial_level = mono[first_active] >= 0.0f;
	current_level = *out_initial_level;
	last_transition = first_active;

	cap = 4096;
	count = 0;
	durations = port_malloc(cap * sizeof(uint32_t));

	if (durations == NULL) {
		BUS_LOG(bus,
		    BUS_LOG_ERROR,
		    "Failed to allocate durations buffer");
		return (PORT_NOMEM);
	}

	for (i = first_active + 1; i < num_samples; i++) {
		bool new_level = current_level;

		if (mono[i] >= threshold) {
			new_level = true;
		} else if (mono[i] <= -threshold) {
			new_level = false;
		}

		if (new_level != current_level) {
			uint32_t delta_samples = i - last_transition;
			uint32_t cycles = (uint32_t)(((double)delta_samples *
							 (double)ACI_CLOCK) /
				(double)sample_rate +
			    0.5);

			if (cycles == 0) {
				cycles = 1;
			}

			if (count >= cap) {
				if (cap > (UINT32_MAX / 2) / sizeof(uint32_t)) {
					BUS_LOG(bus,
					    BUS_LOG_ERROR,
					    "Error: Cassette signal durations "
					    "array overflow");
					port_free(durations);
					return (PORT_RANGE);
				}
				cap *= 2;
				new_buf = port_realloc(durations,
				    cap * sizeof(uint32_t));

				if (new_buf == NULL) {
					BUS_LOG(bus,
					    BUS_LOG_ERROR,
					    "Failed to reallocate durations "
					    "buffer");
					port_free(durations);
					return (PORT_NOMEM);
				}
				durations = new_buf;
			}
			durations[count++] = cycles;
			current_level = new_level;
			last_transition = i;
		}
	}

	if (count == 0) {
		port_free(durations);
		return (PORT_PROTOCOL);
	}

	*out_durations = durations;
	*out_count = count;
	return (PORT_OK);
}

static port_result_t
load_wav_tape(struct aci_card *aci, const char *tape_path)
{
	void *f;
	uint8_t riff_header[12];
	uint16_t audio_format, bits_per_sample, channels;
	uint32_t data_size, sample_rate;
	uint8_t *data_chunk;
	uint32_t bytes_per_sample, frame_count, i;
	float *mono_samples;
	uint32_t *durations;
	uint32_t count;
	bool initial_level;
	port_size_t read_bytes;

	if (aci == (void *)0 || tape_path == (void *)0) {
		return (PORT_INVALID);
	}
	f = port_vfs_default.open(tape_path, PORT_VFS_READ);
	if (f == NULL) {
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Could not open WAV file '%s'",
		    tape_path);
		BUS_LOG(aci->bus, BUS_LOG_ERROR, msg);
		return (PORT_CANTOPEN);
	}

	read_bytes = 0;
	if (port_vfs_default.read(f, riff_header, 12, &read_bytes) != 0 ||
	    read_bytes != 12) {
		BUS_LOG(aci->bus, BUS_LOG_ERROR, "Error: Truncated WAV header");
		port_vfs_default.close(f);
		return (PORT_IO);
	}

	if (port_memcmp(riff_header, "RIFF", 4) != 0 ||
	    port_memcmp(riff_header + 8, "WAVE", 4) != 0) {
		BUS_LOG(aci->bus, BUS_LOG_ERROR, "Error: Invalid WAV format");
		port_vfs_default.close(f);
		return (PORT_PROTOCOL);
	}

	audio_format = 0;
	channels = 0;
	sample_rate = 0;
	bits_per_sample = 0;
	data_chunk = NULL;
	data_size = 0;

	for (;;) {
		uint8_t chunk_header[8];
		uint32_t chunk_size;

		read_bytes = 0;
		if (port_vfs_default.read(f, chunk_header, 8, &read_bytes) !=
			0 ||
		    read_bytes != 8) {
			break;
		}

		chunk_size = chunk_header[4] | (chunk_header[5] << 8) |
		    (chunk_header[6] << 16) | (chunk_header[7] << 24);

		if (port_memcmp(chunk_header, "fmt ", 4) == 0) {
			uint8_t fmt_data[16];

			if (chunk_size < 16) {
				BUS_LOG(aci->bus,
				    BUS_LOG_ERROR,
				    "Error: Invalid fmt chunk size");
				port_vfs_default.close(f);
				return (PORT_PROTOCOL);
			}
			read_bytes = 0;
			if (port_vfs_default.read(f,
				fmt_data,
				16,
				&read_bytes) != 0 ||
			    read_bytes != 16) {
				BUS_LOG(aci->bus,
				    BUS_LOG_ERROR,
				    "Error: Truncated fmt chunk");
				port_vfs_default.close(f);
				return (PORT_IO);
			}
			audio_format = fmt_data[0] | (fmt_data[1] << 8);
			channels = fmt_data[2] | (fmt_data[3] << 8);
			sample_rate = fmt_data[4] | (fmt_data[5] << 8) |
			    (fmt_data[6] << 16) | (fmt_data[7] << 24);
			bits_per_sample = fmt_data[14] | (fmt_data[15] << 8);

			if (chunk_size > 16) {
				port_vfs_default.seek(f,
				    chunk_size - 16,
				    PORT_VFS_SEEK_CUR);
			}
		} else if (port_memcmp(chunk_header, "data", 4) == 0) {
			data_size = chunk_size;
			data_chunk = port_malloc(data_size);
			if (data_chunk == NULL) {
				BUS_LOG(aci->bus,
				    BUS_LOG_ERROR,
				    "Failed to allocate data chunk buffer");
				port_vfs_default.close(f);
				return (PORT_NOMEM);
			}
			read_bytes = 0;
			if (port_vfs_default.read(f,
				data_chunk,
				data_size,
				&read_bytes) != 0 ||
			    read_bytes != data_size) {
				BUS_LOG(aci->bus,
				    BUS_LOG_ERROR,
				    "Error: Truncated data chunk");
				port_free(data_chunk);
				port_vfs_default.close(f);
				return (PORT_IO);
			}
			break;
		} else {
			uint32_t aligned_size = chunk_size + (chunk_size & 1);

			port_vfs_default.seek(f,
			    aligned_size,
			    PORT_VFS_SEEK_CUR);
		}
	}

	port_vfs_default.close(f);

	if (data_chunk == NULL) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Error: Missing WAV data chunk");
		return (PORT_PROTOCOL);
	}
	if (channels == 0 || sample_rate == 0 || bits_per_sample == 0) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Error: Missing WAV format details");
		port_free(data_chunk);
		return (PORT_PROTOCOL);
	}
	if (audio_format != 1 && audio_format != 3) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Error: Unsupported WAV format (only PCM and float32 "
		    "are supported)");
		port_free(data_chunk);
		return (PORT_PROTOCOL);
	}

	bytes_per_sample = bits_per_sample / 8;
	if (bytes_per_sample == 0 || data_size < bytes_per_sample * channels) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Error: Unsupported WAV sample format");
		port_free(data_chunk);
		return (PORT_PROTOCOL);
	}

	frame_count = data_size / (bytes_per_sample * channels);
	if (frame_count > UINT32_MAX / sizeof(float)) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Error: WAV file is too large (integer overflow "
		    "prevention)");
		port_free(data_chunk);
		return (PORT_RANGE);
	}
	mono_samples = port_malloc(frame_count * sizeof(float));
	if (mono_samples == NULL) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Failed to allocate mixed mono samples buffer");
		port_free(data_chunk);
		return (PORT_NOMEM);
	}

	for (i = 0; i < frame_count; i++) {
		float mixed;
		uint16_t ch;

		mixed = 0.0f;
		for (ch = 0; ch < channels; ch++) {
			uint8_t *sample_ptr;
			float value;

			sample_ptr = data_chunk +
			    (i * channels + ch) * bytes_per_sample;
			value = 0.0f;

			if (audio_format == 1 && bits_per_sample == 8) {
				value = ((int)sample_ptr[0] - 128) / 128.0f;
			} else if (audio_format == 1 && bits_per_sample == 16) {
				int16_t s = sample_ptr[0] |
				    (sample_ptr[1] << 8);

				value = (float)s / 32768.0f;
			} else if (audio_format == 1 && bits_per_sample == 24) {
				int32_t s = (sample_ptr[0]) |
				    (sample_ptr[1] << 8) |
				    (sample_ptr[2] << 16);

				if (s & 0x00800000) {
					s |= 0xFF000000;
				}
				value = (float)s / 8388608.0f;
			} else if (audio_format == 1 && bits_per_sample == 32) {
				int32_t s = sample_ptr[0] |
				    (sample_ptr[1] << 8) |
				    (sample_ptr[2] << 16) |
				    (sample_ptr[3] << 24);

				value = (float)s / 2147483648.0f;
			} else if (audio_format == 3 && bits_per_sample == 32) {
				float f_val;

				port_memcpy(&f_val, sample_ptr, sizeof(float));
				value = f_val;
			}
			mixed += value;
		}
		mono_samples[i] = mixed / (float)channels;
	}

	port_free(data_chunk);

	durations = NULL;
	count = 0;
	initial_level = false;

	if (pcm_to_durations(aci->bus,
		mono_samples,
		frame_count,
		sample_rate,
		&durations,
		&count,
		&initial_level) != PORT_OK) {
		port_free(mono_samples);
		return (PORT_ERROR);
	}

	port_free(mono_samples);

	if (aci->durations != NULL) {
		port_free(aci->durations);
	}

	aci->durations = durations;
	aci->num_durations = count;
	aci->playback_index = 0;
	aci->current_cycle = 0;
	aci->last_read_cycle = 0;
	aci->cycles_until_toggle = durations[0];
	aci->input_level = initial_level;
	aci->initial_level = initial_level;
	aci->tape_loaded = true;
	aci->tape_active = true;

	{
		char msg[256];
		port_snprintf(msg,
		    sizeof(msg),
		    "Loaded WAV tape '%s' (%u pulses, sample rate %u Hz)",
		    tape_path,
		    count,
		    sample_rate);
		BUS_LOG(aci->bus, BUS_LOG_INFO, msg);
	}
	return (PORT_OK);
}

static port_result_t
save_wav_tape(struct aci_card *aci, const char *tape_path)
{
	port_file_t f;
	uint32_t total_samples;
	uint32_t byte_rate;
	uint32_t data_size;
	uint32_t riff_size;
	uint32_t trailing_count;
	uint32_t wav_sample_rate;
	int16_t sample;
	uint8_t bytes[4];
	uint8_t word[2];
	bool level;
	uint32_t i, s;

	if (aci == (void *)0 || tape_path == (void *)0) {
		return (PORT_INVALID);
	}
	if (aci->recorded_count == 0) {
		BUS_LOG(aci->bus,
		    BUS_LOG_WARN,
		    "ACI: No tape output was recorded. Nothing to save.");
		return (PORT_ERROR);
	}

	wav_sample_rate = 44100;
	total_samples = 0;

	for (i = 0; i < aci->recorded_count; i++) {
		uint32_t duration = aci->recorded_durations[i];
		uint32_t sample_count =
		    (uint32_t)(((double)duration * (double)wav_sample_rate) /
			    (double)ACI_CLOCK +
			0.5);

		if (sample_count == 0) {
			sample_count = 1;
		}
		total_samples += sample_count;
	}

	total_samples += wav_sample_rate / 10;

	if (total_samples > UINT32_MAX / 2) {
		BUS_LOG(aci->bus,
		    BUS_LOG_ERROR,
		    "Error: Recording is too long to save as WAV");
		return (PORT_RANGE);
	}

	data_size = (uint32_t)(total_samples * sizeof(int16_t));
	riff_size = 36 + data_size;

	f = port_vfs_default.open(tape_path, PORT_VFS_WRITE);
	if (f == PORT_FILE_INVALID) {
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Error: Could not open '%s' for writing",
		    tape_path);
		BUS_LOG(aci->bus, BUS_LOG_ERROR, msg);
		return (PORT_CANTOPEN);
	}

	port_vfs_default.write(f, "RIFF", 4);

	bytes[0] = riff_size & 0xFF;
	bytes[1] = (riff_size >> 8) & 0xFF;
	bytes[2] = (riff_size >> 16) & 0xFF;
	bytes[3] = (riff_size >> 24) & 0xFF;
	port_vfs_default.write(f, bytes, 4);

	port_vfs_default.write(f, "WAVE", 4);
	port_vfs_default.write(f, "fmt ", 4);

	bytes[0] = 16;
	bytes[1] = 0;
	bytes[2] = 0;
	bytes[3] = 0;
	port_vfs_default.write(f, bytes, 4);

	word[0] = 1;
	word[1] = 0;
	port_vfs_default.write(f, word, 2);

	word[0] = 1;
	word[1] = 0;
	port_vfs_default.write(f, word, 2);

	bytes[0] = wav_sample_rate & 0xFF;
	bytes[1] = (wav_sample_rate >> 8) & 0xFF;
	bytes[2] = (wav_sample_rate >> 16) & 0xFF;
	bytes[3] = (wav_sample_rate >> 24) & 0xFF;
	port_vfs_default.write(f, bytes, 4);

	byte_rate = wav_sample_rate * (uint32_t)sizeof(int16_t);

	bytes[0] = byte_rate & 0xFF;
	bytes[1] = (byte_rate >> 8) & 0xFF;
	bytes[2] = (byte_rate >> 16) & 0xFF;
	bytes[3] = (byte_rate >> 24) & 0xFF;
	port_vfs_default.write(f, bytes, 4);

	word[0] = sizeof(int16_t);
	word[1] = 0;
	port_vfs_default.write(f, word, 2);

	word[0] = 16;
	word[1] = 0;
	port_vfs_default.write(f, word, 2);

	port_vfs_default.write(f, "data", 4);

	bytes[0] = data_size & 0xFF;
	bytes[1] = (data_size >> 8) & 0xFF;
	bytes[2] = (data_size >> 16) & 0xFF;
	bytes[3] = (data_size >> 24) & 0xFF;
	port_vfs_default.write(f, bytes, 4);

	level = aci->output_initial_level;

	for (i = 0; i < aci->recorded_count; i++) {
		uint32_t duration = aci->recorded_durations[i];
		uint32_t sample_count =
		    (uint32_t)(((double)duration * (double)wav_sample_rate) /
			    (double)ACI_CLOCK +
			0.5);

		if (sample_count == 0) {
			sample_count = 1;
		}
		sample = level ? 14000 : -14000;

		for (s = 0; s < sample_count; s++) {
			word[0] = (uint8_t)(sample & 0xFF);
			word[1] = (uint8_t)((sample >> 8) & 0xFF);
			port_vfs_default.write(f, word, 2);
		}
		level = !level;
	}

	sample = level ? 14000 : -14000;
	trailing_count = wav_sample_rate / 10;

	for (s = 0; s < trailing_count; s++) {
		word[0] = (uint8_t)(sample & 0xFF);
		word[1] = (uint8_t)((sample >> 8) & 0xFF);
		port_vfs_default.write(f, word, 2);
	}

	port_vfs_default.close(f);
	{
		char msg[512];
		port_snprintf(msg,
		    sizeof(msg),
		    "Saved ACI tape '%s' as WAV format",
		    tape_path);
		BUS_LOG(aci->bus, BUS_LOG_INFO, msg);
	}
	return (PORT_OK);
}

port_result_t
aci_load_tape(struct expansion_card *card, const char *tape_path)
{
	struct aci_card *aci;

	if (card == NULL || port_strcmp(card->name, "ACI") != 0) {
		return (PORT_INVALID);
	}
	aci = (struct aci_card *)card->ctx;
	return (load_wav_tape(aci, tape_path));
}

port_result_t
aci_save_tape(struct expansion_card *card, const char *tape_path)
{
	struct aci_card *aci;

	if (card == NULL || port_strcmp(card->name, "ACI") != 0) {
		return (PORT_INVALID);
	}
	aci = (struct aci_card *)card->ctx;
	return (save_wav_tape(aci, tape_path));
}

void
aci_free(struct expansion_card *card)
{
	struct aci_card *aci;

	if (card != NULL) {
		if (card->ctx != NULL) {
			aci = (struct aci_card *)card->ctx;
			if (aci->durations != NULL) {
				port_free(aci->durations);
			}
			if (aci->recorded_durations != NULL) {
				port_free(aci->recorded_durations);
			}
			port_free(aci);
		}
		port_free(card);
	}
}

uint32_t
aci_get_recorded_count(struct expansion_card *card)
{
	struct aci_card *aci;

	if (card == NULL || port_strcmp(card->name, "ACI") != 0) {
		return (0);
	}
	aci = (struct aci_card *)card->ctx;
	return (aci->recorded_count);
}

#endif /* APPLE1_OMIT_ACI */
