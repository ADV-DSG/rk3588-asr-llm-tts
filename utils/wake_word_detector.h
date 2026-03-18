#ifndef WAKE_WORD_DETECTOR_H
#define WAKE_WORD_DETECTOR_H

#include "audio_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize wake word detector using Porcupine
 * 
 * @param model_path Path to Porcupine model file
 * @param keyword_path Path to keyword file (.ppn)
 * @param sensitivity Sensitivity for wake word detection (0.0-1.0)
 * @return Detector context or NULL on failure
 */
void* init_wake_word_detector(const char *model_path, const char *keyword_path, float sensitivity);

/**
 * Detect wake word in audio buffer
 * 
 * @param detector_ctx Detector context from init_wake_word_detector
 * @param audio Audio buffer containing audio data
 * @return Keyword index if detected, -1 otherwise
 */
int detect_wake_word(void* detector_ctx, audio_buffer_t *audio);

/**
 * Close wake word detector and free resources
 * 
 * @param detector_ctx Detector context from init_wake_word_detector
 * @return 0 on success, -1 on failure
 */
int close_wake_word_detector(void* detector_ctx);

#ifdef __cplusplus
}
#endif

#endif // WAKE_WORD_DETECTOR_H
