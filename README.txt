
项目功能介绍：
智能语音对话系统
语音识别ASR  ---》大语言模型推理LLM  ---》 语音播报TTS
该软件项目仅仅是个demo，不用于商业用途

硬件：
RK3588

ASR： zipformer参照
https://github.com/airockchip/rknn_model_zoo   
rknn_model_zoo-main/examples/zipformer

LLM参照：
RKLLM-Toolkit：https://github.com/airockchip/rknn-llm
rknn-llm-main/examples/rkllm_api_demo/deploy/src/llm_demo.cpp

TTS参照：
github地址：https://github.com/myshell-ai/MeloTTS  

整体工程编译和第三方库参照：
https://github.com/airockchip/rknn_model_zoo   



==================================================================================
软件大体框架

1、大体功能：
由三个进程组成，ASR 、LLM、TTS，三个进程通过管道通信，ASR负责采集麦克风音频，并把音频转成文本，然后把文本发给LLM进程，LLM进程把文本交给大语言模型（deepseek），deepseek给出回答文本，然后把回答的文本发送给TTS进程，TTS进程把接收到的文本转成音频，并播放。


2、每个进程功能详细分析：
ASR进程：
使用alsa读取音频，把音频给AI zipformer，通过NPU加速，转成文本，把文本通过管道发给LLM进程
其中还使用了唤醒词的功能，使用轻量模型 picovoice，只有当检测到特定的唤醒词时，才会启动zipformer
AI：picovoice 语音识别唤醒词 - 印象笔记
当读取完麦克风，就暂停了麦克风，等到TTS进程发来“DONE”，表示播报完毕，才会继续监听麦克风
这样做就是为了，不把自己播放的音频给采集到了，也就是说目前还不支持播报中打断
要支持打断功能，就需要使用到回音消除技术，过滤掉自己播报的音频，目前还没做

为了调试方便，同时支持从终端输入文本给它，这样就不用说话，也能获取到文本


LLM进程：
接收到ASR的文本，就开始使用deepseek推理分析，deepseek输出推理结果，把结果发送给TTS，不是等到推理全部完成才发送，是分段发送，推理出大概50个字符，就开始发送给TTS，直到推理完成，把全部文本发送完给TTS进程，再发送“FINISH”给TTS进程
deepseek模型需要进行微调：把身份换成我给它的身份
模型需要转换：需要把huggingface的格式转成rkllm的格式
AI：deepseek微调 - 印象笔记

意图识别：本来想用大模型直接做意图识别，但是1.5B的deepseek做不到，所以使用传统方法，直接字符匹配方式，目前做了“拍照”意图，识别到ASR发送过来的文本中有“拍照”字符，就认为是要拍照，就不送给deepseek推理，直接就执行相应的硬件功能，并发给TTS“好的，正在拍照”


TTS进程
接收LLM的文本，由于LLM是分批发送文本过来的，这里也需要分批去把文本转成音频，把接收到文本放入文本缓存，然后去文本换成取出文本送给AI melotts模型转成音频，把音频放入音频缓存，从音频缓存取出音频PCM数据进行播放
当收到LLM的“FINISH”信号时，表明推理完成，等到全部音频播放完，就发送“DONE”信号给ASR进程，ASR进程可以开启麦克风，进行新一轮识别

===========================================================================================




==========================================================================================
源码：

模型下载：
ASR模型下载：
examples/zipformer/model/download_model.sh  执行下载
wget -O encoder-epoch-99-avg-1.onnx https://ftrg.zbox.filez.com/v2/delivery/data/95f00b0fc900458ba134f8b180b3f7a1/examples/zipformer/encoder-epoch-99-avg-1.onnx
wget -O decoder-epoch-99-avg-1.onnx https://ftrg.zbox.filez.com/v2/delivery/data/95f00b0fc900458ba134f8b180b3f7a1/examples/zipformer/decoder-epoch-99-avg-1.onnx
wget -O joiner-epoch-99-avg-1.onnx https://ftrg.zbox.filez.com/v2/delivery/data/95f00b0fc900458ba134f8b180b3f7a1/examples/zipformer/joiner-epoch-99-avg-1.onnx

melotts模型下载：参考
test.py(参考例程)
from melo.api import TTS
speed = 1.0
device = 'cpu' # or cuda:0
text = "你好，我是智能语音助手TTS"
model = TTS(language='ZH', device=device)

speaker_ids = model.hps.data.spk2id
output_path = 'zh.wav'
model.tts_to_file(text, speaker_ids['ZH'], output_path, speed=speed)



LLM模型下载：
https://www.modelscope.cn 下载  safetensors 格式的模型文件
https://huggingface.co/  下载  safetensors 格式的模型文件





模型转换：
ASR：examples/zipformer/python/convert.py

TTS：
examples/melotts/python/convert.py


LLM：
from rkllm.api import RKLLM
import os
os.environ['CUDA_VISIBLE_DEVICES']='0'

'''
https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B

Download the DeepSeek R1 model from the above url.
'''

modelpath = '/path/to/DeepSeek-R1-Distill-Qwen-1.5B'
llm = RKLLM()

# Load model
# Use 'export CUDA_VISIBLE_DEVICES=0' to specify GPU device
# device options ['cpu', 'cuda']
# dtype  options ['float32', 'float16', 'bfloat16']
# Using 'bfloat16' or 'float16' can significantly reduce memory consumption but at the cost of lower precision  
# compared to 'float32'. Choose the appropriate dtype based on your hardware and model requirements. 
ret = llm.load_huggingface(model=modelpath, model_lora = None, device='cuda', dtype="float32", custom_config=None, load_weight=True)
# ret = llm.load_gguf(model = modelpath)
if ret != 0:
    print('Load model failed!')
    exit(ret)

# Build model
dataset = "./data_quant.json"
# Json file format, please note to add prompt in the input，like this:
# [{"input":"Human: 你好！\nAssistant: ", "target": "你好！我是人工智能助手KK！"},...]
# Different quantization methods are optimized for different algorithms:  
# w8a8/w8a8_gx   is recommended to use the normal algorithm.  
# w4a16/w4a16_gx is recommended to use the grq algorithm.
qparams = None # Use extra_qparams
target_platform = "RK3588"
optimization_level = 1
quantized_dtype = "W8A8"
quantized_algorithm = "normal"
num_npu_core = 3

ret = llm.build(do_quantization=True, optimization_level=optimization_level, quantized_dtype=quantized_dtype,
                quantized_algorithm=quantized_algorithm, target_platform=target_platform, num_npu_core=num_npu_core, extra_qparams=qparams, dataset=dataset, hybrid_rate=0, max_context=4096)
if ret != 0:
    print('Build model failed!')
    exit(ret)

# Export rkllm model
ret = llm.export_rkllm(f"./{os.path.basename(modelpath)}_{quantized_dtype}_{target_platform}.rkllm")
if ret != 0:
    print('Export model failed!')
    exit(ret)







编译流程：
编译工具：
ASR和TTS
aarch64-linux-gnu-gcc   通用编译器

LLM：
aarch64-buildroot-linux-gnu-gcc  在RK3588 Linux SDK中 
rk3588_linux_sdk/buildroot/output/rockchip_atk_dlrk3588/host

ASR：
examples/zipformer/cpp/build.sh  运行编译

TTS：
examples/melotts/cpp/cross-compile.sh

LLM：
examples/rkllm_demo/build-linux.sh

分别会在它们各自的目录下生成install文件夹，install包含了可执行文件和模型
推送到RK3588板卡上即可运行



运行：
ASR：
examples/zipformer/cpp/install/run_wakeword_demo.sh
TTS：
examples/melotts/cpp/install_cross/run_melotts_demo.sh
LLM：
examples/rkllm_demo/install/deepseek_rkllm_demo/run_demo.sh

对着麦克风说话就可以进行识别并播报了，也可以在ASR终端输入字符



