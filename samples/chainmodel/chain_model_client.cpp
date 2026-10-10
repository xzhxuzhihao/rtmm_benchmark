#include "chain_model_client.h"
#include <atomic>
#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <vector>
#include <thread>

std::shared_ptr<DISB::InferResult> ModelClient::produceResult(){
	res_data = std::make_shared<DISB::InferResult>(s_num, 0);
	return res_data;
}

void ModelClient::consumePrevResults(const std::map<std::string, std::shared_ptr<DISB::InferResult>> &prevResults){
	for(auto &pair : prevResults){
		/* todo: 处理过去的输出，并作为输入 */
	}
}

int64_t ModelClient::get_s_num(){ 
	return s_num;
}

void ModelClient::set_s_num(int64_t num){
	s_num = num;
}

/* 建立优先级流、初始化CUDA上下文、分配显存 */
bool ModelClient::init_act(){
	cudaStreamCreateWithPriority(&stream, cudaStreamNonBlocking, priority);

    // 1. 初始化 Runtime
    // 方式 A：使用 .reset() 接收裸指针（最推荐，语义最清晰）
    runtime.reset(nvinfer1::createInferRuntime(Logger::getInstance()));
    if (!runtime) {
        std::cerr << "创建 TensorRT Runtime 失败!\n";
        return false;
    }

    // 2. 加载并反序列化 Engine
    // loadEngine 返回的是 TRTUniquePtr<ICudaEngine> 智能指针，直接移动赋值即可
    engine = loadEngine(this->engine_path, runtime.get());
    if (!engine) {
        std::cerr << "加载 Engine 失败!\n";
        return false;
    }

    // 3. 创建 Execution Context
    // 方式 B：构造临时智能指针进行移动赋值
    context = TRTUniquePtr<nvinfer1::IExecutionContext>{engine->createExecutionContext()};
    if (!context) {
        std::cerr << "创建 Execution Context 失败!\n";
        return false;
    }

	// 1. 获取模型总的 I/O Tensor 数量
    int32_t num_io = engine->getNbIOTensors();

    for (int32_t i = 0; i < num_io; i++) {
        const char* tensor_name = engine->getIOTensorName(i);
        nvinfer1::TensorIOMode mode = engine->getTensorIOMode(tensor_name);
        nvinfer1::Dims dims = engine->getTensorShape(tensor_name);

        // 2. 为当前 Tensor 分配 GPU 显存
        size_t total_elements = getSizeByDim(dims);
        void* device_ptr = nullptr;
        cudaMalloc(&device_ptr, total_elements * sizeof(float));

        // 3. 为 TensorRT 绑定该 Tensor 的显存地址
        context->setTensorAddress(tensor_name, device_ptr);
		
        // 4. 记录到成员变量中（根据输入/输出类型保存）
        if (mode == nvinfer1::TensorIOMode::kINPUT) {
			in_buffer.push_back(TensorBuffer{tensor_name, dims, device_ptr});	

            // 生成随机输入数据并拷贝到 GPU
            generateRandomInput((float *)device_ptr, total_elements);
			std::cout << "input_elements(float) = " << total_elements << std::endl;
        } 
        else if (mode == nvinfer1::TensorIOMode::kOUTPUT) {
			out_buffer.push_back(TensorBuffer{tensor_name, dims, device_ptr});	
			cpu_output.resize(cpu_output.size()+1);
			cpu_output.rbegin()->resize(total_elements);
			std::cout << "output_elements(float) = " << total_elements << std::endl;
        }
    }	

	return true;
}

bool ModelClient::infer_act(){
    // 执行推理 (使用 TensorRT 现代排队 API enqueueV3)
    bool status = context->enqueueV3(stream);
    if (!status) {
        std::cerr << log_timestamp() << getName() << " TensorRT 推理失败!\n";
        return false;
    }

    // 5. 等待流同步并写回输出结果
    cudaStreamSynchronize(stream);

	return true;
}

size_t ModelClient::getSizeByDim(const nvinfer1::Dims& dims)
{
	size_t size = 1;
	for (size_t i = 0; i < dims.nbDims; ++i) {
		size *= dims.d[i];
	}
	return size;
}

void ModelClient::generateRandomInput(float* gpu_input, size_t size)
{
	std::vector<float> cpu_input_tmp;
	cpu_input_tmp.resize(size);

    // 使用随机数发生器生成 [0.0, 1.0] 之间的模拟图像数据
	auto seed = std::chrono::system_clock::now().time_since_epoch().count();
	std::mt19937 gen(seed); 
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    for (size_t i = 0; i < size; ++i) {
        cpu_input_tmp[i] = dis(gen);
    }
	cpu_input.push_back(cpu_input_tmp);
}

// 辅助函数：从本地加载反序列化 .engine 文件
TRTUniquePtr<nvinfer1::ICudaEngine> ModelClient::loadEngine(const std::string& engine_path, nvinfer1::IRuntime* runtime)
{
    std::ifstream file(engine_path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "ERROR: 无法打开 Engine 文件: " << engine_path << "\n";
        return nullptr;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> engine_data(size);
    file.read(engine_data.data(), size);
    file.close();

    return TRTUniquePtr<nvinfer1::ICudaEngine>{
        runtime->deserializeCudaEngine(engine_data.data(), size)
    };
}

void ModelClient::postprocessResults()
{
	int32_t out_num = out_buffer.size();
	for(int32_t i=0; i<out_num; i++){
		int64_t total_elements = cpu_output[i].size();
		cudaMemcpy(cpu_output[i].data(), (float *)out_buffer[i].ptr, total_elements*sizeof(float), cudaMemcpyDeviceToHost);
	}
}

void ModelClient::init(){
	init_act();
}

void ModelClient::preprocess(){
	/* CPU 固定处理时间 */
    std::this_thread::sleep_for(std::chrono::microseconds(10));

    // 将随机生成的 Host 数据直接拷贝至 GPU 内存
	int32_t in_num = in_buffer.size();
	for(int32_t i=0; i<in_num; i++){
		int64_t total_elements = cpu_input[i].size();
		cudaMemcpy((float *)in_buffer[i].ptr, cpu_input[i].data(), total_elements*sizeof(float), cudaMemcpyHostToDevice);
	}
}

void ModelClient::infer(){
	infer_act();
}

void ModelClient::postprocess(){
    postprocessResults();
	/* CPU 固定处理时间 */
    std::this_thread::sleep_for(std::chrono::microseconds(20));
}

std::shared_ptr<DISB::Client> ModelClientFactory(const Json::Value &config, const Json::Value &config_extra)
{
    std::string name = config["name"].asString();
    std::string engine_path = config["engine_path"].asString();
	std::shared_ptr<DISB::Client> client;
	int priority_; 
	if(!config_extra.empty()){
		priority_ = config_extra["priority"].asInt();
	}

    if (name == "yolov8s") {
		client = std::make_shared<Yolov8sClient>("yolov8s", engine_path, priority_);
    } else if (name == "BiSeNetV2") {
		client = std::make_shared<Yolov8sClient>("BiSeNetV2", engine_path, priority_);
    } else if (name == "SuperPoint") {
		client = std::make_shared<Yolov8sClient>("SuperPoint", engine_path, priority_);
    } else if (name == "DeiT-Tiny") {
		client = std::make_shared<Yolov8sClient>("DeiT-Tiny", engine_path, priority_);
	} else {
        std::cerr << "Config file error!\n";
		return nullptr;
	}

	return client;
}
