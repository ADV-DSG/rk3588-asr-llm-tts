#ifndef _RKNN_MODEL_ZOO_AUDIO_UTILS_H_
#define _RKNN_MODEL_ZOO_AUDIO_UTILS_H_

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    float *data;
    int num_frames;
    int num_channels;
    int sample_rate;
} audio_buffer_t;

/**
 * @brief Reads an audio file into a buffer.
 *
 * @param path [in] Path to the audio file.
 * @param audio [out] Pointer to the audio buffer structure that will store the read data.
 * @return int 0 on success, -1 on error.
 */
int read_audio(const char *path, audio_buffer_t *audio);

/**
 * @brief Saves audio data to a WAV file.
 *
 * @param path [in] Path to the output WAV file.
 * @param data [in] Pointer to the audio data to be saved.
 * @param num_frames [in] Number of frames in the audio data.
 * @param sample_rate [in] Sampling rate of the audio data.
 * @param num_channels [in] Number of channels in the audio data.
 * @return int 0 on success, -1 on error.
 */
int save_audio(const char *path, float *data, int num_frames, int sample_rate, int num_channels);

/**
 * @brief Resamples audio data to a desired sample rate.
 *
 * This function adjusts the sample rate of the provided audio data from 
 * the original sample rate to the desired sample rate. The audio data 
 * is assumed to be in a format compatible with the processing logic.
 *
 * @param audio [in/out] Pointer to the audio buffer structure containing 
 *                       the audio data to be resampled.
 * @param original_sample_rate [in] The original sample rate of the audio data.
 * @param desired_sample_rate [in] The target sample rate to resample the audio data to.
 * @return int 0 on success, -1 on error.
 */
int resample_audio(audio_buffer_t *audio, int original_sample_rate, int desired_sample_rate);

/**
 * @brief Converts audio data to a single channel (mono).
 *
 * This function takes two-channel audio data and converts it to single 
 * channel (mono) by averaging the channels or using another merging strategy.
 * The audio data will be modified in place.
 *
 * @param audio [in/out] Pointer to the audio buffer structure containing 
 *                       the audio data to be converted.
 * @return int 0 on success, -1 on error.
 */
int convert_channels(audio_buffer_t *audio);



/**
 * @brief Initializes the microphone for audio capture.
 *
 * @param sample_rate [in] Desired sample rate for the microphone.
 * @param num_channels [in] Desired number of channels (1 for mono, 2 for stereo).
 * @return void* Pointer to the microphone context on success, NULL on error.
 */
void* init_microphone(int sample_rate, int num_channels);

/**
 * @brief Reads audio data from the microphone.
 *
 * @param mic_ctx [in] Pointer to the microphone context.
 * @param buffer [out] Pointer to the buffer where audio data will be stored.
 * @param num_frames [in] Number of frames to read from the microphone.
 * @return int Number of frames actually read, or -1 on error.
 */
int read_microphone(void* mic_ctx, audio_buffer_t *buffer);

/**
 * @brief Closes the microphone and releases resources.
 *
 * @param mic_ctx [in] Pointer to the microphone context to be closed.
 * @return int 0 on success, -1 on error.
 */
int close_microphone(void* mic_ctx);

/**
 * @brief Pauses the microphone capture.
 *
 * @param mic_ctx [in] Pointer to the microphone context to be paused.
 * @return int 0 on success, -1 on error.
 */
int pause_microphone(void* mic_ctx);

/**
 * @brief Resumes the microphone capture.
 *
 * @param mic_ctx [in] Pointer to the microphone context to be resumed.
 * @return int 0 on success, -1 on error.
 */
int resume_microphone(void* mic_ctx);

/**
 * @brief Sets the ALSA volume using the mixer interface.
 *
 * @param volume [in] Volume level (0.0 to 1.0).
 * @return int 0 on success, -1 on error.
 */
int set_alsa_volume(float volume);

/**
 * @brief Plays audio data directly without saving to file.
 *
 * @param data [in] Pointer to the audio data to be played.
 * @param num_frames [in] Number of frames in the audio data.
 * @param sample_rate [in] Sampling rate of the audio data.
 * @param num_channels [in] Number of channels in the audio data.
 * @param volume [in] Volume level (0.0 to 1.0).
 * @return int 0 on success, -1 on error.
 */
int play_audio(float *data, int num_frames, int sample_rate, int num_channels, float volume);

// Wake word detection operations
#include "wake_word_detector.h"

#ifdef __cplusplus
} // extern "C"
#endif

#endif // _RKNN_MODEL_ZOO_AUDIO_UTILS_H_