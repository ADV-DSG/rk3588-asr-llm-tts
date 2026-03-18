#include <stdio.h>
#include <stdlib.h>
#include <sndfile.h>
#include <math.h>
#include "audio_utils.h"

// Conditional compilation for ALSA support
#ifdef HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

int read_audio(const char *path, audio_buffer_t *audio)
{
    SNDFILE *infile;
    SF_INFO sfinfo = {0};

    infile = sf_open(path, SFM_READ, &sfinfo);
    if (!infile)
    {
        fprintf(stderr, "Error: failed to open file '%s': %s\n", path, sf_strerror(NULL));
        return -1;
    }

    audio->num_frames = sfinfo.frames;
    audio->num_channels = sfinfo.channels;
    audio->sample_rate = sfinfo.samplerate;
    audio->data = (float *)malloc(audio->num_frames * audio->num_channels * sizeof(float));
    if (!audio->data)
    {
        fprintf(stderr, "Error: failed to allocate memory.\n");
        sf_close(infile);
        return -1;
    }

    sf_count_t num_read_frames = sf_readf_float(infile, audio->data, audio->num_frames);
    if (num_read_frames != audio->num_frames)
    {
        fprintf(stderr, "Error: failed to read all frames. Expected %ld, got %ld.\n", (long)audio->num_frames, (long)num_read_frames);
        free(audio->data);
        sf_close(infile);
        return -1;
    }

    sf_close(infile);

    return 0;
}

int save_audio(const char *path, float *data, int num_frames, int sample_rate, int num_channels)
{
    SNDFILE *outfile;
    SF_INFO sfinfo = {0};

    sfinfo.frames = num_frames;
    sfinfo.samplerate = sample_rate;
    sfinfo.channels = num_channels;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

    outfile = sf_open(path, SFM_WRITE, &sfinfo);
    if (!outfile)
    {
        fprintf(stderr, "Error: failed to open file '%s' for writing: %s\n", path, sf_strerror(NULL));
        return -1;
    }

    sf_count_t num_written_frames = sf_writef_float(outfile, data, num_frames);
    if (num_written_frames != num_frames)
    {
        fprintf(stderr, "Error: failed to write all frames. Expected %ld, wrote %ld.\n", (long)num_frames, (long)num_written_frames);
        sf_close(outfile);
        return -1;
    }

    sf_close(outfile);

    return 0;
}

int resample_audio(audio_buffer_t *audio, int original_sample_rate, int desired_sample_rate)
{
    int original_length = audio->num_frames;
    int out_length = round(original_length * (double)desired_sample_rate / (double)original_sample_rate);
    printf("resample_audio: %d HZ -> %d HZ \n", original_sample_rate, desired_sample_rate);

    float *resampled_data = (float *)malloc(out_length * sizeof(float));
    if (!resampled_data)
    {
        return -1;
    }

    for (int i = 0; i < out_length; ++i)
    {
        double src_index = i * (double)original_sample_rate / (double)desired_sample_rate;
        int left_index = (int)floor(src_index);
        int right_index = (left_index + 1 < original_length) ? left_index + 1 : left_index;
        double fraction = src_index - left_index;
        resampled_data[i] = (1.0f - fraction) * audio->data[left_index] + fraction * audio->data[right_index];
    }

    audio->num_frames = out_length;
    free(audio->data);
    audio->data = resampled_data;

    return 0;
}

int convert_channels(audio_buffer_t *audio)
{

    int original_num_channels = audio->num_channels;
    printf("convert_channels: %d -> %d \n", original_num_channels, 1);

    float *converted_data = (float *)malloc(audio->num_frames * sizeof(float));
    if (!converted_data)
    {
        return -1;
    }

    for (int i = 0; i < audio->num_frames; ++i)
    {
        float left = audio->data[i * 2];
        float right = audio->data[i * 2 + 1];
        converted_data[i] = (left + right) / 2.0f;
    }

    audio->num_channels = 1;
    free(audio->data);
    audio->data = converted_data;

    return 0;
}

// Microphone context structure for ALSA
#ifdef HAVE_ALSA
typedef struct {
    snd_pcm_t *handle;
    snd_pcm_format_t format;
    unsigned int sample_rate;
    unsigned int channels;
    snd_pcm_uframes_t frames_per_buffer;
    float *temp_buffer;
} microphone_context_t;
#endif

void* init_microphone(int sample_rate, int num_channels)
{
#ifdef HAVE_ALSA
    microphone_context_t *mic_ctx = (microphone_context_t *)malloc(sizeof(microphone_context_t));
    if (!mic_ctx) {
        fprintf(stderr, "Error: failed to allocate microphone context.\n");
        return NULL;
    }

    // Initialize ALSA
    int err;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *hw_params;

    // Open PCM device for recording
    if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0)) < 0) {
        fprintf(stderr, "Error: cannot open PCM device: %s\n", snd_strerror(err));
        free(mic_ctx);
        return NULL;
    }

    // Allocate a hardware parameters object
    snd_pcm_hw_params_alloca(&hw_params);

    // Fill it in with default values
    if ((err = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Set the desired hardware parameters
    // Interleaved mode
    if ((err = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Error: cannot set access type: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Float format
    // Try different float format constants based on ALSA version
    int format;
    #ifdef SND_PCM_FORMAT_FLOAT32
        format = SND_PCM_FORMAT_FLOAT32;
    #elif defined(SND_PCM_FORMAT_FLOAT)
        format = SND_PCM_FORMAT_FLOAT;
    #else
        // Fallback to signed 16-bit integer format if float is not supported
        format = SND_PCM_FORMAT_S16_LE;
    #endif
    
    if ((err = snd_pcm_hw_params_set_format(handle, hw_params, format)) < 0) {
        fprintf(stderr, "Error: cannot set sample format: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Sample rate
    unsigned int actual_rate = sample_rate;
    if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &actual_rate, 0)) < 0) {
        fprintf(stderr, "Error: cannot set sample rate: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }
    if (actual_rate != sample_rate) {
        fprintf(stderr, "Warning: requested sample rate %d Hz, got %d Hz\n", sample_rate, actual_rate);
        sample_rate = actual_rate;
    }

    // Channels
    if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, num_channels)) < 0) {
        fprintf(stderr, "Error: cannot set channel count: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Set period size
    snd_pcm_uframes_t frames_per_buffer = 4096;
    if ((err = snd_pcm_hw_params_set_period_size_near(handle, hw_params, &frames_per_buffer, 0)) < 0) {
        fprintf(stderr, "Error: cannot set period size: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Write the parameters to the driver
    if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot set hardware parameters: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Prepare the interface for use
    if ((err = snd_pcm_prepare(handle)) < 0) {
        fprintf(stderr, "Error: cannot prepare audio interface for use: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    // Initialize microphone context
    mic_ctx->handle = handle;
    mic_ctx->format = format;
    mic_ctx->sample_rate = sample_rate;
    mic_ctx->channels = num_channels;
    mic_ctx->frames_per_buffer = frames_per_buffer;
    
    // Allocate appropriate buffer size based on format
    size_t buffer_size;
    if (format == SND_PCM_FORMAT_S16_LE) {
        // For 16-bit integer format
        buffer_size = frames_per_buffer * num_channels * sizeof(int16_t);
    } else {
        // For float formats
        buffer_size = frames_per_buffer * num_channels * sizeof(float);
    }
    
    mic_ctx->temp_buffer = (void *)malloc(buffer_size);
    if (!mic_ctx->temp_buffer) {
        fprintf(stderr, "Error: failed to allocate temporary buffer.\n");
        snd_pcm_close(handle);
        free(mic_ctx);
        return NULL;
    }

    return mic_ctx;
#else
    fprintf(stderr, "Error: ALSA support is not enabled.\n");
    return NULL;
#endif
}

int read_microphone(void* mic_ctx, audio_buffer_t *buffer)
{
#ifdef HAVE_ALSA
    if (!mic_ctx || !buffer) {
        fprintf(stderr, "Error: invalid microphone context or buffer.\n");
        return -1;
    }

    microphone_context_t *context = (microphone_context_t *)mic_ctx;
    int err;
    snd_pcm_uframes_t frames;

    // Read frames from microphone
    frames = snd_pcm_readi(context->handle, context->temp_buffer, context->frames_per_buffer);
    if (frames < 0) {
        frames = snd_pcm_recover(context->handle, frames, 0);
    }
    if (frames < 0) {
        fprintf(stderr, "Error: cannot read from microphone: %s\n", snd_strerror(frames));
        return -1;
    }

    // Allocate buffer for audio data
    buffer->data = (float *)malloc(frames * context->channels * sizeof(float));
    if (!buffer->data) {
        fprintf(stderr, "Error: failed to allocate audio buffer.\n");
        return -1;
    }

    // Convert data to float if needed
    if (context->format == SND_PCM_FORMAT_S16_LE) {
        // Convert int16_t to float
        int16_t *int_buffer = (int16_t *)context->temp_buffer;
        for (snd_pcm_uframes_t i = 0; i < frames * context->channels; i++) {
            buffer->data[i] = (float)int_buffer[i] / 32768.0f; // Normalize to [-1.0, 1.0]
        }
    } else {
        // Direct copy for float formats
        memcpy(buffer->data, context->temp_buffer, frames * context->channels * sizeof(float));
    }
    
    buffer->num_frames = frames;
    buffer->num_channels = context->channels;
    buffer->sample_rate = context->sample_rate;

    return frames;
#else
    fprintf(stderr, "Error: ALSA support is not enabled.\n");
    return -1;
#endif
}

int close_microphone(void* mic_ctx)
{
#ifdef HAVE_ALSA
    if (!mic_ctx) {
        return 0;
    }

    microphone_context_t *context = (microphone_context_t *)mic_ctx;
    
    // Close ALSA device
    snd_pcm_close(context->handle);
    
    // Free temporary buffer
    if (context->temp_buffer) {
        free(context->temp_buffer);
    }
    
    // Free microphone context
    free(context);
    
    return 0;
#else
    return 0;
#endif
}

int pause_microphone(void* mic_ctx)
{
#ifdef HAVE_ALSA
    if (!mic_ctx) {
        return 0;
    }

    microphone_context_t *context = (microphone_context_t *)mic_ctx;
    
    // Drop the stream to pause capture
    int err = snd_pcm_drop(context->handle);
    if (err < 0) {
        fprintf(stderr, "Error: cannot pause microphone: %s\n", snd_strerror(err));
        return -1;
    }
    
    return 0;
#else
    return 0;
#endif
}

int resume_microphone(void* mic_ctx)
{
#ifdef HAVE_ALSA
    if (!mic_ctx) {
        return 0;
    }

    microphone_context_t *context = (microphone_context_t *)mic_ctx;
    
    // Prepare the stream to resume capture
    int err = snd_pcm_prepare(context->handle);
    if (err < 0) {
        fprintf(stderr, "Error: cannot resume microphone: %s\n", snd_strerror(err));
        return -1;
    }
    
    return 0;
#else
    return 0;
#endif
}

int set_alsa_volume(float volume)
{
#ifdef HAVE_ALSA
    int err;
    snd_mixer_t *mixer;
    snd_mixer_selem_id_t *sid;
    
    // Clamp volume to valid range [0.0, 1.0]
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    
    // Open mixer
    if ((err = snd_mixer_open(&mixer, 0)) < 0) {
        fprintf(stderr, "Error: cannot open mixer: %s\n", snd_strerror(err));
        return -1;
    }
    
    // Attach to default device
    if ((err = snd_mixer_attach(mixer, "default")) < 0) {
        fprintf(stderr, "Error: cannot attach mixer to default device: %s\n", snd_strerror(err));
        snd_mixer_close(mixer);
        return -1;
    }
    
    // Register simple element class
    if ((err = snd_mixer_selem_register(mixer, NULL, NULL)) < 0) {
        fprintf(stderr, "Error: cannot register mixer element: %s\n", snd_strerror(err));
        snd_mixer_close(mixer);
        return -1;
    }
    
    // Load mixer elements
    if ((err = snd_mixer_load(mixer)) < 0) {
        fprintf(stderr, "Error: cannot load mixer elements: %s\n", snd_strerror(err));
        snd_mixer_close(mixer);
        return -1;
    }
    
    // Allocate and initialize simple element identifier
    snd_mixer_selem_id_alloca(&sid);
    
    // Try different mixer elements in order of priority
    const char *mixer_names[] = {"PCM", "Master", "Speaker", "Headphone", NULL};
    snd_mixer_elem_t *elem = NULL;
    
    for (int i = 0; mixer_names[i] && !elem; i++) {
        snd_mixer_selem_id_set_index(sid, 0);
        snd_mixer_selem_id_set_name(sid, mixer_names[i]);
        elem = snd_mixer_find_selem(mixer, sid);
        if (elem && snd_mixer_selem_has_playback_volume(elem)) {
            // fprintf(stderr, "Found mixer element: %s\n", mixer_names[i]);
            break;
        }
    }
    
    if (!elem) {
        // Try to find any playback element if named elements not found
        for (snd_mixer_elem_t *e = snd_mixer_first_elem(mixer); e && !elem; e = snd_mixer_elem_next(e)) {
            if (snd_mixer_selem_has_playback_volume(e)) {
                elem = e;
                fprintf(stderr, "Found generic playback element\n");
                break;
            }
        }
        if (!elem) {
            fprintf(stderr, "Error: cannot find any playback element\n");
            snd_mixer_close(mixer);
            return -1;
        }
    }
    
    // Get volume range
    long min, max;
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
    // fprintf(stderr, "Volume range: %ld - %ld\n", min, max);
    
    // Calculate volume in ALSA format
    long alsa_volume = min + (long)((max - min) * volume);
    // fprintf(stderr, "Setting volume: %.2f -> %ld\n", volume, alsa_volume);
    
    // Set volume for all channels
    if ((err = snd_mixer_selem_set_playback_volume_all(elem, alsa_volume)) < 0) {
        fprintf(stderr, "Error: cannot set playback volume: %s\n", snd_strerror(err));
        
        // Try setting volume for front left and right channels individually
        if ((err = snd_mixer_selem_set_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, alsa_volume)) < 0) {
            fprintf(stderr, "Error: cannot set front left volume: %s\n", snd_strerror(err));
        }
        if ((err = snd_mixer_selem_set_playback_volume(elem, SND_MIXER_SCHN_FRONT_RIGHT, alsa_volume)) < 0) {
            fprintf(stderr, "Error: cannot set front right volume: %s\n", snd_strerror(err));
        }
    }
    
    // Verify the volume was set correctly
    long current_volume;
    if ((err = snd_mixer_selem_get_playback_volume(elem, SND_MIXER_SCHN_FRONT_LEFT, &current_volume)) == 0) {
        // fprintf(stderr, "Verified volume: %ld\n", current_volume);
    }
    
    // Close mixer
    snd_mixer_close(mixer);
    
    return 0;
#else
    fprintf(stderr, "Error: ALSA support is not enabled. Cannot set volume.\n");
    return -1;
#endif
}

int play_audio(float *data, int num_frames, int sample_rate, int num_channels, float volume)
{
#ifdef HAVE_ALSA
    int err;
    snd_pcm_t *handle;
    snd_pcm_hw_params_t *hw_params;
    
    // Set ALSA volume using mixer interface
    if (set_alsa_volume(volume) < 0) {
        fprintf(stderr, "Warning: cannot set ALSA volume, using default\n");
    }
    
    // Open PCM device for playback
    if ((err = snd_pcm_open(&handle, "default", SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        fprintf(stderr, "Error: cannot open PCM device for playback: %s\n", snd_strerror(err));
        return -1;
    }
    
    // Allocate a hardware parameters object
    snd_pcm_hw_params_alloca(&hw_params);
    
    // Fill it in with default values
    if ((err = snd_pcm_hw_params_any(handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot initialize hardware parameter structure: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Set the desired hardware parameters
    // Interleaved mode
    if ((err = snd_pcm_hw_params_set_access(handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) {
        fprintf(stderr, "Error: cannot set access type: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Float format
    int format;
    #ifdef SND_PCM_FORMAT_FLOAT32
        format = SND_PCM_FORMAT_FLOAT32;
    #elif defined(SND_PCM_FORMAT_FLOAT)
        format = SND_PCM_FORMAT_FLOAT;
    #else
        // Fallback to signed 16-bit integer format if float is not supported
        format = SND_PCM_FORMAT_S16_LE;
    #endif
    
    if ((err = snd_pcm_hw_params_set_format(handle, hw_params, format)) < 0) {
        fprintf(stderr, "Error: cannot set sample format: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Sample rate
    unsigned int actual_rate = sample_rate;
    if ((err = snd_pcm_hw_params_set_rate_near(handle, hw_params, &actual_rate, 0)) < 0) {
        fprintf(stderr, "Error: cannot set sample rate: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    if (actual_rate != sample_rate) {
        fprintf(stderr, "Warning: requested sample rate %d Hz, got %d Hz\n", sample_rate, actual_rate);
    }
    
    // Channels
    if ((err = snd_pcm_hw_params_set_channels(handle, hw_params, num_channels)) < 0) {
        fprintf(stderr, "Error: cannot set channel count: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Set period size
    snd_pcm_uframes_t frames_per_buffer = 4096;
    if ((err = snd_pcm_hw_params_set_period_size_near(handle, hw_params, &frames_per_buffer, 0)) < 0) {
        fprintf(stderr, "Error: cannot set period size: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Write the parameters to the driver
    if ((err = snd_pcm_hw_params(handle, hw_params)) < 0) {
        fprintf(stderr, "Error: cannot set hardware parameters: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Prepare the interface for use
    if ((err = snd_pcm_prepare(handle)) < 0) {
        fprintf(stderr, "Error: cannot prepare audio interface for use: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Play audio data
    if (format == SND_PCM_FORMAT_S16_LE) {
        // Convert float to int16_t without volume scaling (using ALSA mixer instead)
        int16_t *int_buffer = (int16_t *)malloc(num_frames * num_channels * sizeof(int16_t));
        if (!int_buffer) {
            fprintf(stderr, "Error: failed to allocate buffer for conversion\n");
            snd_pcm_close(handle);
            return -1;
        }
        
        for (int i = 0; i < num_frames * num_channels; i++) {
            int_buffer[i] = (int16_t)(data[i] * 32767.0f); // Just convert, no volume scaling
        }
        
        // Write data to PCM device
        snd_pcm_uframes_t frames_written = 0;
        while (frames_written < num_frames) {
            snd_pcm_uframes_t frames = snd_pcm_writei(handle, int_buffer + frames_written * num_channels, num_frames - frames_written);
            if (frames < 0) {
                frames = snd_pcm_recover(handle, frames, 0);
            }
            if (frames < 0) {
                fprintf(stderr, "Error: cannot write to PCM device: %s\n", snd_strerror(frames));
                free(int_buffer);
                snd_pcm_close(handle);
                return -1;
            }
            frames_written += frames;
        }
        
        free(int_buffer);
    } else {
        // Write float data directly without volume scaling (using ALSA mixer instead)
        snd_pcm_uframes_t frames_written = 0;
        while (frames_written < num_frames) {
            snd_pcm_uframes_t frames = snd_pcm_writei(handle, data + frames_written * num_channels, num_frames - frames_written);
            if (frames < 0) {
                frames = snd_pcm_recover(handle, frames, 0);
            }
            if (frames < 0) {
                fprintf(stderr, "Error: cannot write to PCM device: %s\n", snd_strerror(frames));
                snd_pcm_close(handle);
                return -1;
            }
            frames_written += frames;
        }
    }
    
    // Drain the PCM device
    if ((err = snd_pcm_drain(handle)) < 0) {
        fprintf(stderr, "Error: cannot drain PCM device: %s\n", snd_strerror(err));
        snd_pcm_close(handle);
        return -1;
    }
    
    // Close PCM device
    snd_pcm_close(handle);
    
    return 0;
#else
    fprintf(stderr, "Error: ALSA support is not enabled. Cannot play audio.\n");
    return -1;
#endif
}


