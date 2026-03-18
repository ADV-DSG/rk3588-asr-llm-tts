#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "audio_utils.h"

#ifdef HAVE_PORCUPINE
#include "pv_porcupine.h"
#endif

// Wake word detector context structure
#ifdef HAVE_PORCUPINE
typedef struct {
    pv_porcupine_t *porcupine;
    int sample_rate;
    int frame_length;
} wake_word_detector_context_t;
#endif

void* init_wake_word_detector(const char *model_path, const char *keyword_path, float sensitivity)
{
#ifdef HAVE_PORCUPINE
    wake_word_detector_context_t *ctx = (wake_word_detector_context_t *)malloc(sizeof(wake_word_detector_context_t));
    if (!ctx) {
        fprintf(stderr, "Error: failed to allocate wake word detector context.");
        return NULL;
    }

    pv_status_t status;
    const char *access_key = "byTPnYdGzK8pvgMw9ZyAYcW2ax+UpoV2M/LMBJN30IR6anRsQdaMHQ==";
    const char *model_path_used = model_path;
    const char *device = "cpu";
    const char *keyword_paths[] = {keyword_path};
    const float sensitivities[] = {sensitivity};

    // Include necessary headers for file existence check
    #include <unistd.h>

    // Check if model file exists
    if (!model_path_used || access(model_path_used, F_OK) == -1) {
        fprintf(stderr, "Error: model file not found: %s\n", model_path_used);
        free(ctx);
        return NULL;
    }

    // Check if keyword file exists
    if (!keyword_path || access(keyword_path, F_OK) == -1) {
        fprintf(stderr, "Error: keyword file not found: %s\n", keyword_path);
        free(ctx);
        return NULL;
    }

    // Print initialization parameters for debugging
    fprintf(stderr, "Initializing Porcupine with:\n");
    fprintf(stderr, "  Access key: %s\n", access_key);
    fprintf(stderr, "  Model path: %s\n", model_path_used);
    fprintf(stderr, "  Device: %s\n", device);
    fprintf(stderr, "  Keyword path: %s\n", keyword_path);
    fprintf(stderr, "  Sensitivity: %f\n", sensitivity);

    // Initialize Porcupine
    status = pv_porcupine_init(
        access_key,
        model_path_used,
        device,
        1,
        keyword_paths,
        sensitivities,
        &ctx->porcupine
    );

    if (status != PV_STATUS_SUCCESS) {
        fprintf(stderr, "Error: failed to initialize Porcupine: %s\n", pv_status_to_string(status));
        
        // Get detailed error messages
        char **message_stack = NULL;
        int32_t message_stack_depth = 0;
        pv_status_t error_status = pv_get_error_stack(&message_stack, &message_stack_depth);
        if (error_status == PV_STATUS_SUCCESS && message_stack != NULL) {
            fprintf(stderr, "Detailed error messages:\n");
            for (int32_t i = 0; i < message_stack_depth; i++) {
                fprintf(stderr, "  %s\n", message_stack[i]);
            }
            pv_free_error_stack(message_stack);
        }
        
        free(ctx);
        return NULL;
    }

    fprintf(stderr, "Porcupine initialized successfully!\n");

    // Get Porcupine required parameters
    ctx->sample_rate = pv_sample_rate();
    ctx->frame_length = pv_porcupine_frame_length();

    printf("Wake word detector initialized successfully.\n");
    printf("  Sample rate: %d Hz\n", ctx->sample_rate);
    printf("  Frame length: %d\n", ctx->frame_length);

    return ctx;
#else
    fprintf(stderr, "Error: Porcupine support is not enabled.");
    return NULL;
#endif
}

int detect_wake_word(void* detector_ctx, audio_buffer_t *audio)
{
#ifdef HAVE_PORCUPINE
    if (!detector_ctx || !audio) {
        fprintf(stderr, "Error: invalid detector context or audio buffer.\n");
        return -1;
    }

    wake_word_detector_context_t *ctx = (wake_word_detector_context_t *)detector_ctx;
    int result = -1;

    // 检查音频参数
    // fprintf(stderr, "Audio buffer: %d frames, %d Hz, %d channels\n", 
    //         audio->num_frames, audio->sample_rate, audio->num_channels);
    
    if (audio->sample_rate != ctx->sample_rate) {
        fprintf(stderr, "Error: sample rate mismatch: %d vs %d\n", 
                audio->sample_rate, ctx->sample_rate);
        return -1;
    }

    if (audio->num_channels != 1) {
        fprintf(stderr, "Error: Porcupine requires mono audio\n");
        return -1;
    }

    // 检查音频数据类型
    // fprintf(stderr, "Audio data size: %zu bytes per sample\n", sizeof(audio->data[0]));
    
    // 转换音频数据为16位整数格式（如果需要）
    int16_t *int16_audio = NULL;
    if (sizeof(audio->data[0]) == sizeof(float)) {
        // 从float转换为int16
        int16_audio = (int16_t *)malloc(audio->num_frames * sizeof(int16_t));
        if (!int16_audio) {
            fprintf(stderr, "Error: failed to allocate memory for audio conversion\n");
            return -1;
        }
        
        for (int i = 0; i < audio->num_frames; i++) {
            // 将float [-1.0, 1.0]转换为int16 [-32768, 32767]
            float sample = audio->data[i];
            sample = fmaxf(-1.0f, fminf(1.0f, sample));
            int16_audio[i] = (int16_t)(sample * 32767.0f);
        }
        // fprintf(stderr, "Converted float audio to int16\n");
    } else if (sizeof(audio->data[0]) == sizeof(int16_t)) {
        // 直接使用int16数据
        int16_audio = (int16_t *)audio->data;
        fprintf(stderr, "Using existing int16 audio data\n");
    } else {
        fprintf(stderr, "Error: unsupported audio data type\n");
        return -1;
    }

    // 处理音频数据
    int num_frames = audio->num_frames / ctx->frame_length;
    // fprintf(stderr, "Processing %d frames for wake word detection\n", num_frames);
    
    for (int i = 0; i < num_frames; i++) {
        int frame_start = i * ctx->frame_length;
        int frame_end = frame_start + ctx->frame_length;

        if (frame_end > audio->num_frames) {
            break;
        }

        // 运行 Porcupine 检测
        int keyword_index;
        pv_status_t status = pv_porcupine_process(
            ctx->porcupine,
            &int16_audio[frame_start],
            &keyword_index
        );

        if (status != PV_STATUS_SUCCESS) {
            fprintf(stderr, "Error: Porcupine process failed: %s\n", pv_status_to_string(status));
            if (int16_audio != (int16_t *)audio->data) {
                free(int16_audio);
            }
            return -1;
        }

        if (keyword_index >= 0) {
            // 唤醒词检测到
            fprintf(stderr, "Wake word detected! Index: %d\n", keyword_index);
            printf("\nWake word detected!\n");
            result = keyword_index;
            break;
        }
    }

    // 释放临时内存
    if (int16_audio != (int16_t *)audio->data) {
        free(int16_audio);
    }

    return result;
#else
    fprintf(stderr, "Error: Porcupine support is not enabled.\n");
    return -1;
#endif
}

int close_wake_word_detector(void* detector_ctx)
{
#ifdef HAVE_PORCUPINE
    if (!detector_ctx) {
        return 0;
    }

    wake_word_detector_context_t *ctx = (wake_word_detector_context_t *)detector_ctx;

    // Destroy Porcupine
    pv_porcupine_delete(ctx->porcupine);

    // Free context
    free(ctx);

    return 0;
#else
    return 0;
#endif
}
